// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
// @file TxGossipServiceTest.cpp
// @brief Transport-free tests of the eth/68 transaction gossip message flow: ingress
// (Transactions / PooledTransactions -> admission pipeline -> mempool), the
// NewPooledTransactionHashes <-> GetPooledTransactions exchange, and the local
// announcement fan-out. PeerState::send is a capture lambda, so no socket opens.

#include "libinitializer/TxGossipService.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-rlp-protocol/Web3BlobTxWrapper.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::initializer;

namespace bcos::test
{
namespace
{
namespace eth = devp2p::eth;

// A capturing peer: every outbound frame the service writes lands in `frames`.
struct GossipTestPeer
{
    std::shared_ptr<TxGossipService::PeerState> state;
    std::vector<std::pair<uint16_t, bcos::bytes>> frames;

    GossipTestPeer()
    {
        state = std::make_shared<TxGossipService::PeerState>();
        state->describe = "test-peer";
        state->send = [this](uint16_t frameId, bcos::bytes payload) {
            frames.emplace_back(frameId, std::move(payload));
        };
    }
};

struct GossipTestFixture
{
    GossipTestFixture()
      : hashImpl(std::make_shared<crypto::Keccak256>()),
        memPool(txpool::MemPoolConfig{.chainKind = txpool::ChainKind::L1}),
        service(memPool,
            [this](protocol::Transaction& tx) -> task::Task<protocol::TransactionStatus> {
                // The production wrapper is TxValidator::verify(Required); the stub keeps
                // the part the pool depends on (sender recovery + taint clear) and admits.
                crypto::Secp256k1Crypto secp;
                tx.verify(*hashImpl, secp);
                co_return protocol::TransactionStatus::None;
            })
    {}

    std::shared_ptr<crypto::Keccak256> hashImpl;
    txpool::MemPoolImpl memPool;
    TxGossipService service;
};

// Deliver one encoded frame. The payload is taken by value so the bytes outlive the call
// (handlePeerMessage never retains the view; bcos::ref cannot bind a temporary).
TxGossipService::PeerVerdict gossipDeliver(
    TxGossipService& service, TxGossipService::PeerState& peer, uint16_t frameId, bcos::bytes payload)
{
    return service.handlePeerMessage(peer, frameId, bcos::ref(payload));
}

// Sign the EIP-2718 signing hash with a real secp256k1 key (same recipe as the engine
// tests' el1bSign) and return the raw envelope.
bcos::bytes gossipSign(rpc::Web3Transaction& web3Tx, crypto::KeyPairInterface const& keyPair)
{
    crypto::Secp256k1Crypto secp;
    auto const sig = secp.sign(keyPair, web3Tx.hashForSign(), false);
    BOOST_REQUIRE(sig);
    BOOST_REQUIRE_EQUAL(sig->size(), 65);
    web3Tx.signatureR.assign(sig->begin(), sig->begin() + 32);
    web3Tx.signatureS.assign(sig->begin() + 32, sig->begin() + 64);
    web3Tx.signatureV = (*sig)[64];
    return web3Tx.encode();
}

rpc::Web3Transaction gossipMakeTransfer(uint64_t nonce, bcos::Address recipient)
{
    rpc::Web3Transaction tx;
    tx.type = rpc::TransactionType::EIP1559;
    tx.chainId = 1;
    tx.nonce = nonce;
    tx.maxPriorityFeePerGas = u256(100);
    tx.maxFeePerGas = u256(200);
    tx.gasLimit = 21000;
    tx.to = recipient;
    tx.value = u256(7);
    return tx;
}

// A one-blob sidecar with a genuine KZG commitment/proof (seed in the last byte, so every
// 32-byte field element stays below the BLS12-381 scalar modulus).
engine::BlobTxSidecar gossipMakeSidecar(uint8_t seed)
{
    engine::BlobTxSidecar sidecar;
    bcos::bytes blob(crypto::kzg::BlobSize, bcos::byte{0});
    blob.back() = bcos::byte{seed};
    bcos::bytes commitment, proof;
    BOOST_REQUIRE(crypto::kzg::blobToKzgCommitment(bcos::ref(blob), commitment));
    BOOST_REQUIRE(crypto::kzg::computeBlobKzgProof(bcos::ref(blob), bcos::ref(commitment), proof));
    sidecar.blobs.push_back(std::move(blob));
    sidecar.commitments.push_back(std::move(commitment));
    sidecar.proofs.push_back(std::move(proof));
    return sidecar;
}

rpc::Web3Transaction gossipMakeBlobTx(uint64_t nonce, bcos::Address recipient, h256 versionedHash)
{
    auto tx = gossipMakeTransfer(nonce, recipient);
    tx.type = rpc::TransactionType::EIP4844;
    tx.maxFeePerBlobGas = u256(50);
    tx.blobVersionedHashes = {versionedHash};
    return tx;
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(TxGossipServiceTest, GossipTestFixture)

BOOST_AUTO_TEST_CASE(transactionsIngressAdmittedToPool)
{
    crypto::Secp256k1Crypto secp;
    auto key = secp.generateKeyPair();
    auto web3Tx = gossipMakeTransfer(0, Address("1111111111111111111111111111111111111111"));
    auto raw = gossipSign(web3Tx, *key);
    auto const txHash = web3Tx.txHash();

    GossipTestPeer peer;
    auto verdict = gossipDeliver(service, *peer.state, eth::frameId(eth::msg::Transactions),
        eth::encodeTransactions(eth::TransactionsMessage{.transactions = {raw}}));
    BOOST_CHECK(verdict == TxGossipService::PeerVerdict::Ok);

    auto pooled = memPool.get(std::vector<crypto::HashType>{txHash}).front();
    BOOST_REQUIRE(pooled);
    BOOST_CHECK(!pooled->sender().empty());
    // A repeat delivery of the same transaction is a no-op (pool dedup), not an error.
    gossipDeliver(service, *peer.state, eth::frameId(eth::msg::Transactions),
        eth::encodeTransactions(eth::TransactionsMessage{.transactions = {raw}}));
    BOOST_CHECK(memPool.get(std::vector<crypto::HashType>{txHash}).front());
}

BOOST_AUTO_TEST_CASE(blobWrapperIngressAdmittedWithSidecar)
{
    crypto::Secp256k1Crypto secp;
    auto key = secp.generateKeyPair();
    auto sidecar = gossipMakeSidecar(0x2a);
    auto const versionedHash =
        crypto::kzg::versionedHashFromCommitment(bcos::ref(sidecar.commitments.front()));

    auto web3Tx = gossipMakeBlobTx(0, Address("2222222222222222222222222222222222222222"),
        versionedHash);
    auto stripped = gossipSign(web3Tx, *key);
    auto const txHash = web3Tx.txHash();
    // Re-decode the signed stripped form so the wrapper embeds the signature.
    rpc::Web3Transaction signedTx;
    bcos::bytesRef strippedRef = bcos::ref(stripped);
    BOOST_REQUIRE(signedTx.tryDecode(strippedRef).has_value());
    auto wrapper = rpc::encodeBlobTxNetworkWrapper(signedTx, sidecar);

    GossipTestPeer peer;
    gossipDeliver(service, *peer.state, eth::frameId(eth::msg::Transactions),
        eth::encodeTransactions(eth::TransactionsMessage{.transactions = {wrapper}}));

    BOOST_CHECK(memPool.get(std::vector<crypto::HashType>{txHash}).front());
    auto pooledSidecar = memPool.blobSidecar(txHash);
    BOOST_REQUIRE(pooledSidecar.has_value());
    BOOST_CHECK(pooledSidecar->commitments == sidecar.commitments);
    BOOST_CHECK(pooledSidecar->blobs == sidecar.blobs);

    // The STRIPPED form (no sidecar) is not a gossip shape: dropped, never pooled.
    auto otherTx = gossipMakeBlobTx(1, Address("3333333333333333333333333333333333333333"),
        versionedHash);
    auto otherStripped = gossipSign(otherTx, *key);
    auto const otherHash = otherTx.txHash();
    gossipDeliver(service, *peer.state, eth::frameId(eth::msg::Transactions),
        eth::encodeTransactions(eth::TransactionsMessage{.transactions = {otherStripped}}));
    BOOST_CHECK(!memPool.get(std::vector<crypto::HashType>{otherHash}).front());

    // A tampered sidecar (KZG proof no longer matches) is dropped too.
    auto thirdTx = gossipMakeBlobTx(2, Address("3636363636363636363636363636363636363636"),
        versionedHash);
    auto thirdStripped = gossipSign(thirdTx, *key);
    auto const thirdHash = thirdTx.txHash();
    rpc::Web3Transaction thirdDecoded;
    bcos::bytesRef thirdRef = bcos::ref(thirdStripped);
    BOOST_REQUIRE(thirdDecoded.tryDecode(thirdRef).has_value());
    auto badSidecar = gossipMakeSidecar(0x2a);
    badSidecar.proofs.front().back() ^= bcos::byte{0x01};
    auto badWrapper = rpc::encodeBlobTxNetworkWrapper(thirdDecoded, badSidecar);
    gossipDeliver(service, *peer.state, eth::frameId(eth::msg::Transactions),
        eth::encodeTransactions(eth::TransactionsMessage{.transactions = {badWrapper}}));
    BOOST_CHECK(!memPool.get(std::vector<crypto::HashType>{thirdHash}).front());
}

BOOST_AUTO_TEST_CASE(newPooledTransactionHashesTriggersSingleRequest)
{
    GossipTestPeer peer;
    auto const hash1 = h256("abababababababababababababababababababababababababababababababab");
    auto const hash2 = h256("cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");

    eth::NewPooledTransactionHashesMessage announce{
        .types = {0x02}, .sizes = {120}, .hashes = {hash1}};
    gossipDeliver(service, *peer.state, eth::frameId(eth::msg::NewPooledTransactionHashes),
        eth::encodeNewPooledTransactionHashes(announce));

    BOOST_REQUIRE_EQUAL(peer.frames.size(), 1);
    BOOST_CHECK_EQUAL(peer.frames.front().first,
        static_cast<uint16_t>(eth::frameId(eth::msg::GetPooledTransactions)));
    auto request = eth::decodeGetPooledTransactions(bcos::ref(peer.frames.front().second));
    BOOST_REQUIRE(request.has_value());
    BOOST_REQUIRE_EQUAL(request->hashes.size(), 1);
    BOOST_CHECK(request->hashes.front() == hash1);

    // A re-announcement of the same hash is deduplicated: no second request.
    gossipDeliver(service, *peer.state, eth::frameId(eth::msg::NewPooledTransactionHashes),
        eth::encodeNewPooledTransactionHashes(announce));
    BOOST_CHECK_EQUAL(peer.frames.size(), 1);

    // A hash the pool already holds is not requested either.
    crypto::Secp256k1Crypto secp;
    auto key = secp.generateKeyPair();
    auto web3Tx = gossipMakeTransfer(0, Address("4444444444444444444444444444444444444444"));
    auto raw = gossipSign(web3Tx, *key);
    gossipDeliver(service, *peer.state, eth::frameId(eth::msg::Transactions),
        eth::encodeTransactions(eth::TransactionsMessage{.transactions = {raw}}));
    auto const pooledHash = web3Tx.txHash();
    eth::NewPooledTransactionHashesMessage announcePooled{
        .types = {0x02, 0x02}, .sizes = {120, 121}, .hashes = {pooledHash, hash2}};
    gossipDeliver(service, *peer.state, eth::frameId(eth::msg::NewPooledTransactionHashes),
        eth::encodeNewPooledTransactionHashes(announcePooled));
    BOOST_REQUIRE_EQUAL(peer.frames.size(), 2);
    auto request2 = eth::decodeGetPooledTransactions(bcos::ref(peer.frames.back().second));
    BOOST_REQUIRE(request2.has_value());
    BOOST_REQUIRE_EQUAL(request2->hashes.size(), 1);
    BOOST_CHECK(request2->hashes.front() == hash2);
}

BOOST_AUTO_TEST_CASE(getPooledTransactionsServedWithExactEnvelope)
{
    crypto::Secp256k1Crypto secp;
    auto key = secp.generateKeyPair();
    auto web3Tx = gossipMakeTransfer(0, Address("5555555555555555555555555555555555555555"));
    auto raw = gossipSign(web3Tx, *key);
    auto const txHash = web3Tx.txHash();

    GossipTestPeer peer;
    // Ingress fills the serving cache with the exact wire bytes.
    gossipDeliver(service, *peer.state, eth::frameId(eth::msg::Transactions),
        eth::encodeTransactions(eth::TransactionsMessage{.transactions = {raw}}));
    BOOST_REQUIRE(memPool.get(std::vector<crypto::HashType>{txHash}).front());

    auto const unknown = h256("9999999999999999999999999999999999999999999999999999999999999999");
    eth::GetPooledTransactionsMessage request{.requestId = 42, .hashes = {txHash, unknown}};
    gossipDeliver(service, *peer.state, eth::frameId(eth::msg::GetPooledTransactions),
        eth::encodeGetPooledTransactions(request));

    BOOST_REQUIRE_EQUAL(peer.frames.size(), 1);
    BOOST_CHECK_EQUAL(peer.frames.front().first,
        static_cast<uint16_t>(eth::frameId(eth::msg::PooledTransactions)));
    auto reply = eth::decodePooledTransactions(bcos::ref(peer.frames.front().second));
    BOOST_REQUIRE(reply.has_value());
    BOOST_CHECK_EQUAL(reply->requestId, 42);
    // The held hash is served byte-exact; the unknown one is simply omitted.
    BOOST_REQUIRE_EQUAL(reply->transactions.size(), 1);
    BOOST_CHECK(reply->transactions.front() == raw);
}

BOOST_AUTO_TEST_CASE(blobTransactionServedAsWrapperFromPoolFallback)
{
    // The raw cache is bypassed when the transaction never passed through gossip or the
    // RPC hook: the serving path reassembles the stripped envelope from the pool and
    // re-wraps it with the pooled sidecar.
    crypto::Secp256k1Crypto secp;
    auto key = secp.generateKeyPair();
    auto sidecar = gossipMakeSidecar(0x5e);
    auto const versionedHash =
        crypto::kzg::versionedHashFromCommitment(bcos::ref(sidecar.commitments.front()));
    auto web3Tx = gossipMakeBlobTx(0, Address("6666666666666666666666666666666666666666"),
        versionedHash);
    auto stripped = gossipSign(web3Tx, *key);
    auto const txHash = web3Tx.txHash();

    // Admit directly (as the engine tests' el1bPoolAdd does), skipping the gossip cache.
    rpc::Web3Transaction decodedTx;
    bcos::bytesRef strippedRef = bcos::ref(stripped);
    BOOST_REQUIRE(decodedTx.tryDecode(strippedRef).has_value());
    auto tarsTx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [moved = decodedTx.takeToTarsTransaction()]() mutable { return &moved; });
    tarsTx->mutableInner().extraTransactionHash.assign(txHash.begin(), txHash.end());
    tarsTx->verify(*hashImpl, secp);
    BOOST_REQUIRE(memPool.tryAdd(std::move(tarsTx), std::move(sidecar)) ==
                  protocol::TransactionStatus::None);

    GossipTestPeer asker;
    eth::GetPooledTransactionsMessage request{.requestId = 9, .hashes = {txHash}};
    gossipDeliver(service, *asker.state, eth::frameId(eth::msg::GetPooledTransactions),
        eth::encodeGetPooledTransactions(request));
    BOOST_REQUIRE_EQUAL(asker.frames.size(), 1);
    auto reply = eth::decodePooledTransactions(bcos::ref(asker.frames.front().second));
    BOOST_REQUIRE(reply.has_value());
    BOOST_REQUIRE_EQUAL(reply->transactions.size(), 1);
    // The served bytes are the network wrapper: they decode back to the same tx + sidecar.
    rpc::Web3Transaction servedTx;
    engine::BlobTxSidecar servedSidecar;
    BOOST_REQUIRE(rpc::isBlobTxNetworkWrapper(
        bcos::bytesConstRef(reply->transactions.front().data(), reply->transactions.front().size())));
    rpc::decodeBlobTxNetworkWrapper(bcos::bytesConstRef(reply->transactions.front().data(),
                                        reply->transactions.front().size()),
        servedTx, servedSidecar);
    BOOST_CHECK(servedTx.txHash() == txHash);
    BOOST_REQUIRE_EQUAL(servedSidecar.commitments.size(), 1);
    BOOST_CHECK(crypto::kzg::verifyBlobKzgProof(bcos::ref(servedSidecar.blobs.front()),
        bcos::ref(servedSidecar.commitments.front()), bcos::ref(servedSidecar.proofs.front())));
}

BOOST_AUTO_TEST_CASE(announceLocalTransactionFansOutToAllPeers)
{
    GossipTestPeer peerA, peerB;
    service.addPeer(peerA.state);
    service.addPeer(peerB.state);
    BOOST_CHECK_EQUAL(service.peerCount(), 2);

    auto const hash = h256("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    bcos::bytes const envelope{0x02, 0xc4, 0x01, 0x02, 0x03, 0x04};
    service.announceLocalTransaction(hash, 0x02, envelope.size(), envelope);

    for (auto const* peer : {&peerA, &peerB})
    {
        BOOST_REQUIRE_EQUAL(peer->frames.size(), 1);
        BOOST_CHECK_EQUAL(peer->frames.front().first,
            static_cast<uint16_t>(eth::frameId(eth::msg::NewPooledTransactionHashes)));
        auto announce =
            eth::decodeNewPooledTransactionHashes(bcos::ref(peer->frames.front().second));
        BOOST_REQUIRE(announce.has_value());
        BOOST_CHECK(announce->types == bcos::bytes{0x02});
        BOOST_CHECK(announce->sizes == std::vector<uint64_t>{envelope.size()});
        BOOST_CHECK(announce->hashes == std::vector<bcos::h256>{hash});
    }

    // And the envelope is now servable: a peer asking for the hash gets it byte-exact.
    GossipTestPeer asker;
    eth::GetPooledTransactionsMessage request{.requestId = 1, .hashes = {hash}};
    gossipDeliver(service, *asker.state, eth::frameId(eth::msg::GetPooledTransactions),
        eth::encodeGetPooledTransactions(request));
    BOOST_REQUIRE_EQUAL(asker.frames.size(), 1);
    auto reply = eth::decodePooledTransactions(bcos::ref(asker.frames.front().second));
    BOOST_REQUIRE(reply.has_value());
    BOOST_REQUIRE_EQUAL(reply->transactions.size(), 1);
    BOOST_CHECK(reply->transactions.front() == envelope);
}

BOOST_AUTO_TEST_CASE(pingAnsweredAndDisconnectCloses)
{
    GossipTestPeer peer;
    auto verdict = service.handlePeerMessage(*peer.state, devp2p::rlpx::baseMsg::Ping, {});
    BOOST_CHECK(verdict == TxGossipService::PeerVerdict::Ok);
    BOOST_REQUIRE_EQUAL(peer.frames.size(), 1);
    BOOST_CHECK_EQUAL(peer.frames.front().first,
        static_cast<uint16_t>(devp2p::rlpx::baseMsg::Pong));

    BOOST_CHECK(service.handlePeerMessage(*peer.state, devp2p::rlpx::baseMsg::Disconnect, {}) ==
                TxGossipService::PeerVerdict::Closed);
    // An unknown/block-sync frame is ignored, never fatal.
    BOOST_CHECK(service.handlePeerMessage(*peer.state, eth::frameId(eth::msg::BlockHeaders),
                    bcos::bytesConstRef()) == TxGossipService::PeerVerdict::Ok);
}

BOOST_AUTO_TEST_CASE(malformedIngressDroppedWithoutEscalation)
{
    GossipTestPeer peer;
    bcos::bytes garbage{0x02, 0xff, 0xff, 0xff};  // 0x02 || truncated payload
    auto verdict = gossipDeliver(service, *peer.state, eth::frameId(eth::msg::Transactions),
        eth::encodeTransactions(eth::TransactionsMessage{.transactions = {garbage}}));
    BOOST_CHECK(verdict == TxGossipService::PeerVerdict::Ok);
    BOOST_CHECK(memPool.get(std::vector<crypto::HashType>{}).empty());

    // A malformed message envelope is logged-and-kept, not a session failure.
    bcos::bytes notRlp{0xff, 0xff, 0xff};
    BOOST_CHECK(service.handlePeerMessage(*peer.state, eth::frameId(eth::msg::Transactions),
                    bcos::ref(notRlp)) == TxGossipService::PeerVerdict::Ok);
}

BOOST_AUTO_TEST_CASE(rawCacheReannounceKeepsSingleEntry)
{
    // Re-announcing the same hash must refresh the cached envelope in place: a second
    // eviction-order entry would double-count the hash against the 1024-entry budget and
    // evict it (plus every older entry) once the deque overflows.
    auto const hash = h256("f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1f1");
    bcos::bytes const envelope{0x02, 0xc4, 0x01, 0x02, 0x03, 0x04};
    for (int i = 0; i < 1100; ++i)
    {
        service.announceLocalTransaction(hash, 0x02, envelope.size(), envelope);
    }

    GossipTestPeer asker;
    eth::GetPooledTransactionsMessage request{.requestId = 7, .hashes = {hash}};
    gossipDeliver(service, *asker.state, eth::frameId(eth::msg::GetPooledTransactions),
        eth::encodeGetPooledTransactions(request));
    BOOST_REQUIRE_EQUAL(asker.frames.size(), 1);
    auto reply = eth::decodePooledTransactions(bcos::ref(asker.frames.front().second));
    BOOST_REQUIRE(reply.has_value());
    BOOST_REQUIRE_EQUAL(reply->transactions.size(), 1);
    BOOST_CHECK(reply->transactions.front() == envelope);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
