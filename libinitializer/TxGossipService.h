/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file TxGossipService.h
 * @brief Ethereum L1 EL-mode transaction gossip (eth/68): dedicated long-lived
 *        outbound sessions that exchange Transactions / NewPooledTransactionHashes /
 *        GetPooledTransactions / PooledTransactions with bootnode peers and admit what
 *        arrives into the engine mempool through the same pipeline eth_sendRawTransaction
 *        uses (wrapper decode + KZG batch proof for blob transactions, TxValidator
 *        admission, MemPoolImpl::tryAdd).
 *
 *        These sessions are SEPARATE from the block-sync sessions (EthereumSyncInitializer's
 *        download rounds): the sync request/response loops stay byte-for-byte untouched, and
 *        a gossip peer failure never aborts a download. The cost is a second connection to a
 *        few bootnodes; geth treats them as ordinary peers.
 *
 *        Layering for tests: start()/pumpLoop() own the transport (RlpxClient sessions);
 *        handlePeerMessage()/announceLocalTransaction() are transport-free — a PeerState's
 *        `send` is a std::function, so unit tests drive the whole message flow with capture
 *        lambdas and never open a socket.
 * @date 2026/9/22
 */
#pragma once

#include "Common.h"
#include <bcos-crypto/kzg/Kzg4844.h>
#include <bcos-devp2p/eth/Protocol.h>
#include <bcos-devp2p/rlpx/Client.h>
#include <bcos-framework/engine/RawTransactionDispatch.h>
#include <bcos-mempool/MemPoolImpl.h>
#include <bcos-rlp-protocol/Web3BlobTxWrapper.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/Web3RawTransaction.h>
#include <bcos-task/Wait.h>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bcos::initializer
{
class TxGossipService
{
public:
    /// The one admission judgement, injected so tests can stub it. Production wraps
    /// TxValidator::verify(tx, PoolAdmission, SignaturePolicy::Required) — which also
    /// recovers the sender and clears the taint MemPoolImpl::tryAdd refuses.
    using ValidateFn =
        std::function<task::Task<protocol::TransactionStatus>(protocol::Transaction&)>;
    /// One outbound frame. Production binds the session under its send mutex; tests a
    /// capture lambda. May THROW on I/O failure — callers treat that as a dead peer.
    using SendFn = std::function<void(uint16_t frameId, bcos::bytes payload)>;
    /// Fills the resume-dependent PeerConfig fields (head hash, fork-id) fresh at every
    /// (re)connect — the sync loop computes them from the local head.
    using PeerConfigFactory = std::function<devp2p::rlpx::PeerConfig(
        devp2p::rlpx::PeerConfig const& bootnode)>;

    /// What handlePeerMessage settled on: Closed = the peer said Disconnect (or breached
    /// the protocol) and the pump should tear the session down.
    enum class PeerVerdict
    {
        Ok,
        Closed,
    };

    /// One connected peer's pump-visible state. `send` is bound by the transport.
    struct PeerState
    {
        SendFn send;
        uint64_t nextRequestId{1};
        std::string describe;  // host:port, for logs
    };

    /// Gossip DoS/resource ceilings (wire-shape caps live in eth::Protocol's decoders).
    static constexpr size_t c_maxGossipPeers = 4;
    /// An offered-hash list is requested at most this deep; geth caps its own serving at
    /// the same 256 (eth::kMaxGetPooledTransactions).
    static constexpr size_t c_maxHashesPerRequest = devp2p::eth::kMaxGetPooledTransactions;
    /// Largest envelope admitted from the wire: the pool's own per-transaction blob cap
    /// (6) plus the tx body — a larger frame is dropped unread.
    static constexpr size_t c_maxEnvelopeBytes = 6 * crypto::kzg::BlobSize + 64 * 1024;
    /// Dedup LRU of offered/requested hashes.
    static constexpr size_t c_seenHashCapacity = 32768;
    /// Raw-envelope egress cache (serving GetPooledTransactions): entry-count and total-byte
    /// budgets — blob wrappers are >= 131KB each, so the byte budget is what really binds.
    static constexpr size_t c_rawCacheMaxEntries = 1024;
    static constexpr size_t c_rawCacheMaxBytes = 128ull << 20;

    TxGossipService(bcos::txpool::MemPoolImpl& memPool, ValidateFn validate)
      : m_memPool(memPool), m_validate(std::move(validate))
    {}

    ~TxGossipService() { stop(); }

    TxGossipService(TxGossipService const&) = delete;
    TxGossipService& operator=(TxGossipService const&) = delete;

    bool running() const { return m_running.load(); }

    /// Spawn the peer pump threads (at most c_maxGossipPeers, one per bootnode slot,
    /// round-robin over the rest on failure). No-op when already running or the bootnode
    /// list is empty.
    void start(devp2p::rlpx::EccKeyPair localKey,
        std::vector<devp2p::rlpx::PeerConfig> bootnodes, PeerConfigFactory configFactory)
    {
        if (bootnodes.empty() || m_running.exchange(true))
        {
            return;
        }
        size_t const slots = std::min(c_maxGossipPeers, bootnodes.size());
        m_threads.reserve(slots);
        for (size_t slot = 0; slot < slots; ++slot)
        {
            m_threads.emplace_back([this, localKey, bootnodes, configFactory, slot, slots]() {
                pumpLoop(slot, slots, localKey, bootnodes, configFactory);
            });
        }
        INITIALIZER_LOG(INFO) << LOG_DESC("TxGossip: started")
                              << LOG_KV("peers", slots) << LOG_KV("bootnodes", bootnodes.size());
    }

    /// Stop the pumps and join. A pump parked in recvMessage wakes at the socket I/O
    /// timeout (15s) at the latest — same shutdown cadence the sync loop already has.
    void stop()
    {
        m_running.store(false);
        for (auto& thread : m_threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        m_threads.clear();
        std::unique_lock lock(m_peersMutex);
        m_peers.clear();
    }

    /// eth_sendRawTransaction's post-admission hook (any thread): cache the exact wire
    /// envelope for serving and announce hash/type/size to every connected peer. A peer
    /// whose write fails is logged and skipped — announcement is best-effort, the next
    /// admission announces again. Not gated on running(): stop() clears the peer list, so
    /// a late call simply finds nobody to tell.
    void announceLocalTransaction(crypto::HashType const& txHash, uint8_t type, uint64_t size,
        bcos::bytes envelope)
    {
        cacheRaw(txHash, std::move(envelope));
        devp2p::eth::NewPooledTransactionHashesMessage announce{
            .types = {static_cast<bcos::byte>(type)}, .sizes = {size}, .hashes = {txHash}};
        auto payload = devp2p::eth::encodeNewPooledTransactionHashes(announce);
        std::shared_lock lock(m_peersMutex);
        for (auto const& peer : m_peers)
        {
            try
            {
                peer->send(devp2p::eth::frameId(devp2p::eth::msg::NewPooledTransactionHashes),
                    payload);
            }
            catch (std::exception const& e)
            {
                INITIALIZER_LOG(WARNING)
                    << LOG_DESC("TxGossip: announce to peer failed")
                    << LOG_KV("peer", peer->describe) << LOG_KV("error", e.what());
            }
        }
    }

    /// The pump's per-frame entry (also the test seam): decode and dispatch one inbound
    /// message. Never throws on malformed payloads — a bad frame is logged and the session
    /// kept; only a clean Disconnect (or a send failure, which the caller catches) ends it.
    PeerVerdict handlePeerMessage(PeerState& peer, uint16_t frameId, bcos::bytesConstRef payload)
    {
        namespace eth = devp2p::eth;
        // devp2p base protocol first: a Ping MUST get its Pong (geth drops silent peers).
        if (frameId == devp2p::rlpx::baseMsg::Ping)
        {
            peer.send(devp2p::rlpx::baseMsg::Pong, {});
            return PeerVerdict::Ok;
        }
        if (frameId == devp2p::rlpx::baseMsg::Pong)
        {
            return PeerVerdict::Ok;
        }
        if (frameId == devp2p::rlpx::baseMsg::Disconnect)
        {
            INITIALIZER_LOG(INFO)
                << LOG_DESC("TxGossip: peer disconnected") << LOG_KV("peer", peer.describe);
            return PeerVerdict::Closed;
        }
        if (frameId == eth::frameId(eth::msg::Transactions))
        {
            if (auto decoded = eth::decodeTransactions(payload))
            {
                for (auto const& raw : decoded->transactions)
                {
                    ingestTransaction(peer, bcos::bytesConstRef(raw.data(), raw.size()));
                }
            }
            else
            {
                logMalformed(peer, "Transactions", decoded.error().message);
            }
            return PeerVerdict::Ok;
        }
        if (frameId == eth::frameId(eth::msg::PooledTransactions))
        {
            if (auto decoded = eth::decodePooledTransactions(payload))
            {
                for (auto const& raw : decoded->transactions)
                {
                    ingestTransaction(peer, bcos::bytesConstRef(raw.data(), raw.size()));
                }
            }
            else
            {
                logMalformed(peer, "PooledTransactions", decoded.error().message);
            }
            return PeerVerdict::Ok;
        }
        if (frameId == eth::frameId(eth::msg::NewPooledTransactionHashes))
        {
            auto decoded = eth::decodeNewPooledTransactionHashes(payload);
            if (!decoded)
            {
                logMalformed(peer, "NewPooledTransactionHashes", decoded.error().message);
                return PeerVerdict::Ok;
            }
            // Request the hashes we neither hold nor already asked for. types/sizes ride
            // along (eth/68 shape, validated by the decoder) but the admission pipeline
            // re-judges everything from the envelope itself, so neither is consulted here.
            std::vector<bcos::h256> wanted;
            wanted.reserve(std::min(decoded->hashes.size(), c_maxHashesPerRequest));
            for (auto const& hash : decoded->hashes)
            {
                if (wanted.size() >= c_maxHashesPerRequest)
                {
                    break;
                }
                if (markSeen(hash))
                {
                    continue;  // already offered/requested
                }
                if (!m_memPool.get(std::vector<crypto::HashType>{hash}).front())
                {
                    wanted.push_back(hash);
                }
            }
            if (!wanted.empty())
            {
                INITIALIZER_LOG(DEBUG)
                    << LOG_DESC("TxGossip: requesting pooled transactions")
                    << LOG_KV("peer", peer.describe) << LOG_KV("count", wanted.size());
                peer.send(eth::frameId(eth::msg::GetPooledTransactions),
                    eth::encodeGetPooledTransactions(eth::GetPooledTransactionsMessage{
                        .requestId = peer.nextRequestId++, .hashes = std::move(wanted)}));
            }
            return PeerVerdict::Ok;
        }
        if (frameId == eth::frameId(eth::msg::GetPooledTransactions))
        {
            auto decoded = eth::decodeGetPooledTransactions(payload);
            if (!decoded)
            {
                logMalformed(peer, "GetPooledTransactions", decoded.error().message);
                return PeerVerdict::Ok;
            }
            eth::PooledTransactionsMessage reply{.requestId = decoded->requestId, .transactions = {}};
            for (auto const& hash : decoded->hashes)
            {
                if (reply.transactions.size() >= c_maxHashesPerRequest)
                {
                    break;
                }
                if (auto raw = rawEnvelopeFor(hash))
                {
                    reply.transactions.push_back(std::move(*raw));
                }
            }
            peer.send(eth::frameId(eth::msg::PooledTransactions),
                eth::encodePooledTransactions(reply));
            return PeerVerdict::Ok;
        }
        // Every other frame (block-sync codes, NewBlock, receipts, unknown): this session
        // never requests blocks and never announces them, so anything else is ignored.
        return PeerVerdict::Ok;
    }

    /// Test/inspection handle: register a peer without a transport (the pumps do this on
    /// connect; tests do it by hand).
    void addPeer(std::shared_ptr<PeerState> peer)
    {
        std::unique_lock lock(m_peersMutex);
        m_peers.push_back(std::move(peer));
    }

    size_t peerCount() const
    {
        std::shared_lock lock(m_peersMutex);
        return m_peers.size();
    }

private:
    /// True when the hash was already in the seen LRU (inserts it otherwise). The LRU
    /// bounds re-requests: a hash is asked for once per capacity window no matter how
    /// many peers re-announce it.
    bool markSeen(crypto::HashType const& hash)
    {
        std::unique_lock lock(m_seenMutex);
        if (!m_seenSet.insert(hash).second)
        {
            return true;
        }
        m_seenOrder.push_back(hash);
        if (m_seenOrder.size() > c_seenHashCapacity)
        {
            m_seenSet.erase(m_seenOrder.front());
            m_seenOrder.pop_front();
        }
        return false;
    }

    /// The serving half of GetPooledTransactions: the exact wire envelope, wrapper form
    /// for a blob transaction. The raw cache answers first; a miss falls back to
    /// re-encoding from the pool (reassemble the stripped envelope, then — for a blob
    /// transaction whose sidecar is still pooled — re-wrap it). A blob transaction whose
    /// sidecar is gone is simply not served (the reply may be short).
    std::optional<bcos::bytes> rawEnvelopeFor(crypto::HashType const& hash)
    {
        {
            std::unique_lock lock(m_rawCacheMutex);
            if (auto it = m_rawCache.find(hash); it != m_rawCache.end())
            {
                return it->second;
            }
        }
        auto tx = m_memPool.get(std::vector<crypto::HashType>{hash}).front();
        if (!tx || tx->type() !=
                       static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
        {
            return std::nullopt;
        }
        bcos::bytes stripped;
        try
        {
            stripped = bcostars::protocol::reassembleWeb3RawTransaction(
                tx->extraTransactionBytes(), tx->signatureData());
        }
        catch (std::exception const& e)
        {
            INITIALIZER_LOG(WARNING) << LOG_DESC("TxGossip: cannot reassemble pooled transaction")
                                     << LOG_KV("hash", hash.abridged())
                                     << LOG_KV("error", e.what());
            return std::nullopt;
        }
        if (tx->blobVersionedHashes().empty())
        {
            return stripped;
        }
        auto sidecar = m_memPool.blobSidecar(hash);
        if (!sidecar)
        {
            return std::nullopt;
        }
        rpc::Web3Transaction web3Tx;
        bcos::bytesRef ref = bcos::ref(stripped);
        if (auto result = web3Tx.tryDecode(ref); !result.has_value())
        {
            return std::nullopt;
        }
        bcos::bytes wrapper;
        try
        {
            wrapper = rpc::encodeBlobTxNetworkWrapper(web3Tx, *sidecar);
        }
        catch (std::exception const& e)
        {
            INITIALIZER_LOG(WARNING)
                << LOG_DESC("TxGossip: cannot re-wrap pooled blob transaction")
                << LOG_KV("hash", hash.abridged()) << LOG_KV("error", e.what());
            return std::nullopt;
        }
        cacheRaw(hash, wrapper);
        return wrapper;
    }

    void cacheRaw(crypto::HashType const& hash, bcos::bytes raw)
    {
        std::unique_lock lock(m_rawCacheMutex);
        if (auto it = m_rawCache.find(hash); it != m_rawCache.end())
        {
            // Re-cache replaces the payload in place: the hash keeps its eviction-order
            // slot and only the byte delta is accounted. Pushing a second order entry
            // would double-count one hash against both budgets, and the stale slot would
            // later pop as a map miss — with enough re-caches the order deque could drain
            // empty while the byte counter still reads over budget (front() on an empty
            // deque is UB).
            m_rawCacheBytes -= it->second.size();
            m_rawCacheBytes += raw.size();
            it->second = std::move(raw);
        }
        else
        {
            m_rawCacheBytes += raw.size();
            m_rawCacheOrder.push_back(hash);
            m_rawCache.emplace(hash, std::move(raw));
        }
        while (!m_rawCacheOrder.empty() &&
               (m_rawCacheOrder.size() > c_rawCacheMaxEntries ||
                   m_rawCacheBytes > c_rawCacheMaxBytes))
        {
            auto it = m_rawCache.find(m_rawCacheOrder.front());
            if (it != m_rawCache.end())
            {
                m_rawCacheBytes -= it->second.size();
                m_rawCache.erase(it);
            }
            m_rawCacheOrder.pop_front();
        }
    }

    /// One gossiped transaction through the eth_sendRawTransaction pipeline: blob
    /// transactions must arrive in the EIP-4844 network wrapper (the only gossip form
    /// that carries the sidecar) and are KZG-checked like the RPC gate; everything else
    /// decodes as a plain envelope. Admission verdicts are the injected TxValidator's;
    /// failures are counted and rate-limited in the log, never escalated against the peer
    /// (it may simply be ahead of our fork view).
    bool ingestTransaction(PeerState& peer, bcos::bytesConstRef raw)
    {
        if (raw.size() > c_maxEnvelopeBytes) [[unlikely]]
        {
            ++m_ingressDrops;
            INITIALIZER_LOG(WARNING)
                << LOG_DESC("TxGossip: dropping oversized transaction envelope")
                << LOG_KV("peer", peer.describe) << LOG_KV("size", raw.size());
            return false;
        }
        rpc::Web3Transaction web3Tx;
        std::optional<engine::BlobTxSidecar> sidecar;
        auto const kind = engine::dispatchRawTransaction(raw);
        if (kind == engine::RawTransactionKind::Blob)
        {
            if (!rpc::isBlobTxNetworkWrapper(raw))
            {
                return countDrop(peer, "blob transaction not in network wrapper form");
            }
            try
            {
                rpc::decodeBlobTxNetworkWrapper(raw, web3Tx, sidecar.emplace());
            }
            catch (codec::rlp::RlpDecodeException const& e)
            {
                return countDrop(peer, std::string("malformed blob wrapper: ") + e.what());
            }
            for (std::size_t i = 0; i < sidecar->commitments.size(); ++i)
            {
                if (crypto::kzg::versionedHashFromCommitment(
                        bcos::ref(sidecar->commitments[i])) != web3Tx.blobVersionedHashes[i])
                {
                    return countDrop(peer, "blob sidecar commitment/versioned hash mismatch");
                }
            }
            if (!crypto::kzg::verifyBlobKzgProofBatch(
                    sidecar->blobs, sidecar->commitments, sidecar->proofs))
            {
                return countDrop(peer, "blob sidecar KZG proof invalid");
            }
        }
        else if (kind != engine::RawTransactionKind::Legacy &&
                 kind != engine::RawTransactionKind::AccessList &&
                 kind != engine::RawTransactionKind::DynamicFee &&
                 kind != engine::RawTransactionKind::SetCode)
        {
            // Deposits are Engine-API-only and an unknown type byte decodes to nothing.
            return countDrop(peer, "unsupported transaction kind on gossip");
        }
        else
        {
            bcos::bytes copy(raw.data(), raw.data() + raw.size());
            bcos::bytesRef ref = bcos::ref(copy);
            if (auto result = web3Tx.tryDecode(ref); !result.has_value())
            {
                return countDrop(peer, "undecodable transaction: " + result.error().message);
            }
        }
        if (web3Tx.type == rpc::TransactionType::Deposit) [[unlikely]]
        {
            return countDrop(peer, "deposit transaction on gossip");
        }
        auto const txHash = web3Tx.txHash();
        // Already pooled: no re-verify (the pool's copy was judged once), but refresh the
        // serving cache — the raw may have arrived here before the RPC hook did.
        if (m_memPool.get(std::vector<crypto::HashType>{txHash}).front())
        {
            cacheRaw(txHash, bcos::bytes(raw.data(), raw.data() + raw.size()));
            return true;
        }
        auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
            [moved = web3Tx.takeToTarsTransaction()]() mutable { return &moved; });
        tx->mutableInner().extraTransactionHash.assign(txHash.begin(), txHash.end());
        protocol::TransactionStatus status;
        try
        {
            status = task::syncWait(m_validate(*tx));
        }
        catch (std::exception const& e)
        {
            // verify() throws when the data it needs cannot be read — a node fault, not a
            // verdict on the transaction (same split as eth_sendRawTransaction).
            INITIALIZER_LOG(WARNING)
                << LOG_DESC("TxGossip: admission could not be decided")
                << LOG_KV("txHash", txHash.abridged()) << LOG_KV("error", e.what());
            return false;
        }
        if (status != protocol::TransactionStatus::None)
        {
            return countDrop(peer,
                "admission refused: " + std::string(protocol::toString(status)));
        }
        try
        {
            if (auto const taken = m_memPool.tryAdd(std::move(tx), std::move(sidecar));
                taken != protocol::TransactionStatus::None &&
                taken != protocol::TransactionStatus::AlreadyInTxPool)
            {
                return countDrop(
                    peer, "pool refused: " + std::string(protocol::toString(taken)));
            }
        }
        catch (std::exception const& e)
        {
            return countDrop(peer, std::string("pool admission fault: ") + e.what());
        }
        cacheRaw(txHash, bcos::bytes(raw.data(), raw.data() + raw.size()));
        INITIALIZER_LOG(DEBUG) << LOG_DESC("TxGossip: transaction admitted from gossip")
                               << LOG_KV("txHash", txHash.abridged())
                               << LOG_KV("peer", peer.describe);
        return true;
    }

    bool countDrop(PeerState& peer, std::string const& reason)
    {
        ++m_ingressDrops;
        INITIALIZER_LOG(DEBUG) << LOG_DESC("TxGossip: transaction dropped")
                               << LOG_KV("peer", peer.describe) << LOG_KV("reason", reason);
        return false;
    }

    void logMalformed(PeerState& peer, std::string_view what, std::string_view error)
    {
        INITIALIZER_LOG(DEBUG) << LOG_DESC("TxGossip: malformed message")
                               << LOG_KV("peer", peer.describe) << LOG_KV("msg", what)
                               << LOG_KV("error", error);
    }

    /// One slot's connect/pump/reconnect life. The socket read timeout (15s of peer
    /// silence) is HEALTHY — an idle peer is not a dead one — so a timeout restarts the
    /// receive loop; any other failure reconnects after a backoff.
    void pumpLoop(size_t slot, size_t slots, devp2p::rlpx::EccKeyPair const& localKey,
        std::vector<devp2p::rlpx::PeerConfig> const& bootnodes,
        PeerConfigFactory const& configFactory)
    {
        size_t next = slot;
        while (m_running.load())
        {
            auto config = configFactory(bootnodes[next % bootnodes.size()]);
            next += slots;
            std::string const describe = config.host + ":" + std::to_string(config.port);
            try
            {
                devp2p::rlpx::RlpxClient client(localKey, config);
                auto established =
                    std::make_shared<devp2p::rlpx::EstablishedSession>(client.connect());
                INITIALIZER_LOG(INFO)
                    << LOG_DESC("TxGossip: peer connected") << LOG_KV("peer", describe);
                auto peer = std::make_shared<PeerState>();
                peer->describe = describe;
                auto sendMutex = std::make_shared<std::mutex>();
                std::weak_ptr<devp2p::rlpx::EstablishedSession> weakSession = established;
                peer->send = [weakSession, sendMutex](uint16_t frameId, bcos::bytes payload) {
                    if (auto session = weakSession.lock())
                    {
                        std::unique_lock lock(*sendMutex);
                        session->session.sendMessage(devp2p::rlpx::Message{
                            static_cast<uint8_t>(frameId), std::move(payload)});
                    }
                };
                addPeer(peer);
                while (m_running.load())
                {
                    try
                    {
                        auto message = established->session.recvMessage();
                        if (!message)
                        {
                            INITIALIZER_LOG(WARNING)
                                << LOG_DESC("TxGossip: undecodable frame; dropping peer")
                                << LOG_KV("peer", describe)
                                << LOG_KV("error", message.error().message);
                            break;
                        }
                        if (handlePeerMessage(*peer, message->id,
                                bcos::bytesConstRef(
                                    message->data.data(), message->data.size())) ==
                            PeerVerdict::Closed)
                        {
                            break;
                        }
                    }
                    catch (std::exception const& e)
                    {
                        if (std::string_view(e.what()).find("timed out") != std::string_view::npos)
                        {
                            continue;  // 15s of silence is an idle peer, not a dead one
                        }
                        throw;
                    }
                }
                std::unique_lock lock(m_peersMutex);
                std::erase(m_peers, peer);
            }
            catch (std::exception const& e)
            {
                INITIALIZER_LOG(WARNING) << LOG_DESC("TxGossip: peer failed, will reconnect")
                                         << LOG_KV("peer", describe) << LOG_KV("error", e.what());
            }
            // Reconnect backoff, wakeable by stop().
            for (int i = 0; i < 50 && m_running.load(); ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    bcos::txpool::MemPoolImpl& m_memPool;
    ValidateFn m_validate;

    std::atomic<bool> m_running{false};
    std::vector<std::thread> m_threads;

    mutable std::shared_mutex m_peersMutex;
    std::vector<std::shared_ptr<PeerState>> m_peers;

    std::mutex m_seenMutex;
    std::unordered_set<crypto::HashType> m_seenSet;
    std::deque<crypto::HashType> m_seenOrder;

    std::mutex m_rawCacheMutex;
    std::unordered_map<crypto::HashType, bcos::bytes> m_rawCache;
    std::deque<crypto::HashType> m_rawCacheOrder;
    size_t m_rawCacheBytes{0};

    std::atomic<size_t> m_ingressDrops{0};
};
}  // namespace bcos::initializer
