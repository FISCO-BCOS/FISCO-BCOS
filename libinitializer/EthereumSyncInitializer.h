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
 * @file EthereumSyncInitializer.h
 * @brief Ethereum L1 EL-mode self-sync driver: connect to RLPx bootnodes, download
 *        blocks with BlockExchange (PoS header validation), verify + commit each block
 *        with EthereumBlockVerifier (execute -> roots -> MPT state root -> ledger), in
 *        a dedicated background thread. Wired from AirNodeInitializer when
 *        [ethereum] mode=el; the config file is the source of truth.
 * @date 2026/8/19
 */
#pragma once

#include "libinitializer/Common.h"
#include "libinitializer/GlobalStateStorageInitializer.h"
#include "bcos-devp2p/eth/ForkId.h"
#include "bcos-devp2p/rlpx/Client.h"
#include "bcos-devp2p/sync/BlockExchange.h"
#include "bcos-devp2p/sync/Bootnodes.h"
#include "bcos-devp2p/sync/HeaderValidator.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-tool/NodeConfig.h"
#include "bcos-transaction-scheduler/EthereumBlockVerifier.h"
#include "bcos-transaction-scheduler/SchedulerSerialImpl.h"
#include "bcos-rlp-protocol/EthBlockHeader.h"
#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"  // complete type for shared_ptr upcast in decodeRaw()
#include "bcos-task/Wait.h"
#include "ethereum-executor/EthereumExecutor.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/algorithm/string/trim.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace bcos::initializer
{

/// Self-contained Ethereum L1 EL-mode sync client. Owns a background thread that
/// repeatedly: (1) loads the bootnode list, (2) connects to each in turn, (3)
/// downloads blocks from the local head onward and verifies + commits each through
/// EthereumBlockVerifier, and (4) loops forever (catching transient network errors).
///
/// Current limits: sync is serial and no full-Sepolia time/disk benchmark is
/// published yet; the trusted-bootnode model does not verify PoW/TD or consensus-layer
/// finality; and no rollback tool ships, so a fatal fork/checkpoint stop requires
/// a full resync until the follow-up recovery work lands.
class EthereumSyncInitializer
{
public:
    // _globalStateStorage: production MultiLayerStorage (GlobalStateStorage).
    EthereumSyncInitializer(bcos::tool::NodeConfig::Ptr _nodeConfig,
        bcos::ledger::LedgerInterface::Ptr _ledger,
        bcos::protocol::BlockFactory::Ptr _blockFactory,
        std::shared_ptr<scheduler_v1::SchedulerSerialImpl> _scheduler,
        std::shared_ptr<executor_v1::eth::EthereumExecutor> _executor,
        GlobalStateStorageInitializer::Ptr _globalStateStorageInitializer,
        bcos::IOServicePool::Ptr _ioServicePool)
      : m_nodeConfig(std::move(_nodeConfig)),
        m_ledger(std::move(_ledger)),
        m_blockFactory(std::move(_blockFactory)),
        m_scheduler(std::move(_scheduler)),
        m_executor(std::move(_executor)),
        m_globalStateStorageInitializer(std::move(_globalStateStorageInitializer)),
        m_ioServicePool(std::move(_ioServicePool))
    {}

    ~EthereumSyncInitializer() { stop(); }

    EthereumSyncInitializer(EthereumSyncInitializer const&) = delete;
    EthereumSyncInitializer& operator=(EthereumSyncInitializer const&) = delete;

    /// Validate that the EL-mode prerequisites hold (executor v2, fork schedule, bootnode
    /// file readable, genesis anchor present). Throws InvalidConfig on failure.
    void validateConfig() const
    {
        if (!m_nodeConfig->ethereumELModeEnabled())
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "EthereumSyncInitializer: [ethereum].mode != el"));
        }
        if (m_nodeConfig->executorVersion() < ledger::ETHEREUM_EXECUTOR_VERSION)
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "Ethereum L1 EL mode requires executor.version >= 2 "
                                      "(the pure-Ethereum executor); set [executor] version=2 "
                                      "in config.genesis"));
        }
        if (!m_nodeConfig->genesisConfig().m_ethGenesisHeader.has_value())
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "Ethereum L1 EL mode requires an [eth_genesis_header] "
                                      "section in config.genesis (sync anchor)"));
        }
        // Bootnode file must exist and parse (validates the enode list eagerly).
        auto nodes = bcos::devp2p::sync::loadBootnodes(m_nodeConfig->ethereumBootnodesFile());
        if (nodes.empty())
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "Ethereum L1 EL mode: no bootnodes in " +
                                      m_nodeConfig->ethereumBootnodesFile()));
        }
    }

    /// Start the background sync loop. No-op if already started. Throws if the node
    /// key cannot be loaded/persisted (a misconfigured identity must fail startup,
    /// not kill the sync thread later); on a throw nothing has started.
    void start()
    {
        if (m_running.load())
        {
            return;
        }
        auto localKey = loadNodeKey();
        if (m_running.exchange(true))
        {
            return;
        }
        m_localKey = std::move(localKey);
        INITIALIZER_LOG(INFO) << LOG_DESC("EL sync: starting self-sync loop")
                              << LOG_KV("bootnodes", m_nodeConfig->ethereumBootnodesFile())
                              << LOG_KV("maxBatch", m_nodeConfig->ethereumMaxBatchSize());
        m_thread = std::thread([this]() { syncLoop(); });
    }

    /// Stop the background thread and join it. The join is unconditional (not gated
    /// on m_running): the sync thread can also exit on its own after a fatal error
    /// (reorg detection / checkpoint mismatch) with m_running already false, and a
    /// joinable thread that is never joined terminates the process.
    void stop()
    {
        m_running.store(false);
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    bool running() const { return m_running.load(); }

private:
    /// The chain-genesis anchor header, from [eth_genesis_header]. Used as the download
    /// anchor on a fresh node and pinned as the RLPx handshake genesisHash on every
    /// resume. Fork-gated fields copy through only when the genesis header carries them,
    /// so the anchor re-encodes to the byte-exact genesis RLP.
    bcos::protocol::EthBlockHeaderData genesisAnchorHeader() const
    {
        auto const& genesis = m_nodeConfig->genesisConfig().m_ethGenesisHeader.value();
        bcos::protocol::EthBlockHeaderData h;
        h.parentInfo.blockHash = genesis.m_parentHash;
        h.uncleHash = genesis.m_sha3Uncles;
        h.stateRoot = genesis.m_stateRoot;
        h.txsRoot = genesis.m_transactionsRoot;
        h.receiptsRoot = genesis.m_receiptsRoot;
        std::copy(genesis.m_logsBloom.begin(), genesis.m_logsBloom.end(), h.logsBloom.begin());
        h.difficulty = genesis.m_difficulty;
        h.gasLimit = genesis.m_gasLimit;
        h.gasUsed = genesis.m_gasUsed;
        h.number = 0;
        h.timestamp = genesis.m_timestamp;
        h.extraData = genesis.m_extraData;
        std::copy(genesis.m_mixHash.begin(), genesis.m_mixHash.end(), h.prevRandao.begin());
        std::copy(genesis.m_nonce.begin(), genesis.m_nonce.end(), h.nonce.begin());
        h.coinbase = genesis.m_miner;
        // Fork-gated fields: copy through only the ones the genesis header
        // actually carries (nullopt stays nullopt), so the anchor re-encodes
        // to the same byte-exact RLP as the committed genesis block.
        h.baseFee = genesis.m_baseFeePerGas;
        h.withdrawalsHash = genesis.m_withdrawalsRoot;
        h.blobGasUsed = genesis.m_blobGasUsed;
        h.excessBlobGas = genesis.m_excessBlobGas;
        h.parentBeaconRoot = genesis.m_parentBeaconBlockRoot;
        h.requestsHash = genesis.m_requestsHash;
        return h;
    }

    /// Resume point computed once per sync round: where to start downloading and
    /// which header anchors the download. A fresh ledger (only the genesis block)
    /// starts at 1 anchored on the genesis header; a ledger that already has blocks
    /// resumes from localHead + 1 anchored on the local head header (checkpoint
    /// resume). genesisHeader is always the chain genesis — it is what the RLPx
    /// handshake pins as the Status genesisHash, independent of the resume point.
    struct ResumePoint
    {
        uint64_t startNumber;
        bcos::protocol::EthBlockHeaderData anchor;        // local head (or genesis)
        bcos::protocol::EthBlockHeaderData prevHeader;    // parent for the first download
        bcos::protocol::EthBlockHeaderData genesisHeader; // chain genesis (handshake pin)
    };

    ResumePoint resumePoint() const
    {
        auto genesisHeader = genesisAnchorHeader();
        auto current = task::syncWait(ledger::getCurrentBlockNumber(*m_ledger));
        if (current <= 0)
        {
            // Fresh node: only the genesis block exists; start downloading at 1.
            return {1, genesisHeader, genesisHeader, genesisHeader};
        }
        // Resume from the local head: anchor = local head header, start at head + 1.
        auto headBlock = task::syncWait(
            ledger::getBlockData(*m_ledger, current, bcos::ledger::HEADER));
        if (!headBlock || !headBlock->blockHeader())
        {
            BOOST_THROW_EXCEPTION(std::runtime_error(
                "EL sync: cannot read local head block " + std::to_string(current) +
                " for resume"));
        }
        // Convert the stored Tars header back to the Ethereum header domain. The
        // EthBlockHeader constructor copies the fork-gated optionals only when the
        // stored header carries them, so the anchor re-encodes to the same RLP that
        // was committed — byte-exact resume.
        bcos::protocol::EthBlockHeader localHead(*headBlock->blockHeader());
        auto head = localHead.data();
        // The execution path (makeExecutionBlockHeader) stores the Tars timestamp in
        // FISCO milliseconds (the EVM divides by 1000), but the Ethereum header domain
        // — and therefore the resume anchor's RLP re-encoding — is seconds. The
        // EthBlockHeader constructor above already converts (m_data.timestamp =
        // ms / 1000), so `head.timestamp` is wire seconds here — do NOT divide again
        // or the anchor re-encodes to a wrong RLP and the resume parent-hash check
        // fails with "parent hash mismatch (fork or reorg)".
        INITIALIZER_LOG(INFO) << LOG_DESC("EL sync: resuming from local head")
                              << LOG_KV("headNumber", current)
                              << LOG_KV("headHash",
                                  bcos::protocol::ethHeaderHash(head).hex().substr(0, 18))
                              << LOG_KV("resumeFrom", current + 1);
        return {
            static_cast<uint64_t>(current + 1), head, head, genesisHeader};
    }

    bcos::devp2p::sync::ChainConfig devp2pChainConfig() const
    {
        bcos::devp2p::sync::ChainConfig config;
        config.chainId = m_nodeConfig->ethereumChainId();
        config.londonTime = m_nodeConfig->ethereumForkLondonTime();
        config.shanghaiTime = m_nodeConfig->ethereumForkShanghaiTime();
        config.cancunTime = m_nodeConfig->ethereumForkCancunTime();
        config.pragueTime = m_nodeConfig->ethereumForkPragueTime();
        // Post-Prague tail: drives the EIP-7840 blob schedule (BPO1/BPO2) and the
        // EIP-7918 excess-blob-gas update rule from Osaka on. NodeConfig returns
        // UINT64_MAX ("not yet active") for unscheduled tail forks, which
        // ChainConfig interprets as "pre-Osaka rules".
        config.osakaTime = m_nodeConfig->ethereumForkOsakaTime();
        config.bpo1Time = m_nodeConfig->ethereumForkBpo1Time();
        config.bpo2Time = m_nodeConfig->ethereumForkBpo2Time();
        // Blocks before the merge are PoW (non-zero difficulty, ommers allowed);
        // from the merge block onward the chain is PoS. 0 = PoS from genesis.
        config.mergeBlock = m_nodeConfig->ethereumMergeBlock();
        return config;
    }

    /// EIP-2124 fork-id announced in the eth Status handshake. The checksum chains
    /// the genesis hash with every fork point that the LOCAL chain has already passed
    /// (crc32 over the 8-byte big-endian fork value, exactly geth's checksumUpdate),
    /// and next is the first fork point the local chain has NOT passed yet (or 0 if
    /// all known forks are active). This mirrors geth's forkid.NewID(config, genesis,
    /// localHead.Number, localHead.Time) — the fork-id reflects the local head, NOT
    /// wall-clock time. A fresh node (head = genesis) therefore announces
    /// {crc32(genesisHash), mergeBlock}, which every remote accepts via EIP-2124
    /// rule #2 (our checksum is the remote's genesis-sum and next matches its first
    /// fork) — unlike a wall-clock-derived all-forks checksum, which older remotes
    /// (e.g. geth 1.14.x, which only knows forks up to Cancun) reject outright.
    bcos::devp2p::eth::ForkId computeForkId(uint64_t _localHeadNumber, uint64_t _localHeadTime) const
    {
        auto const& genesis = m_nodeConfig->genesisConfig().m_ethGenesisHeader.value();
        uint32_t hash = bcos::devp2p::eth::crc32(
            bcos::bytesConstRef(genesis.m_hash.data(), genesis.m_hash.size()));
        // The chain's only block-based EL fork: the merge (terminal total
        // difficulty) block, from the chain-level [fork_timestamps].merge_block
        // declaration. (London is active at genesis and is skipped by geth's
        // gatherForks; a merge block of 0 — PoS from genesis — is likewise not
        // chained into the fork-id.)
        auto const mergeBlock = m_nodeConfig->ethereumMergeBlock();
        if (mergeBlock > 0)
        {
            if (mergeBlock <= _localHeadNumber)
            {
                hash = bcos::devp2p::eth::forkIdAddForkPoint(hash, mergeBlock);
            }
            else
            {
                // Merge not yet passed locally (fresh node): announce it as next.
                return {hash, mergeBlock};
            }
        }
        // Timestamp-based forks, chained in activation order; an unscheduled tail
        // fork (UINT64_MAX) ends the ladder with next = 0 (see forkIdFromTimeLadder).
        return bcos::devp2p::eth::forkIdFromTimeLadder(hash, _localHeadTime,
            {m_nodeConfig->ethereumForkShanghaiTime(), m_nodeConfig->ethereumForkCancunTime(),
                m_nodeConfig->ethereumForkPragueTime(), m_nodeConfig->ethereumForkOsakaTime(),
                m_nodeConfig->ethereumForkBpo1Time(), m_nodeConfig->ethereumForkBpo2Time()});
    }

    scheduler_v1::EvmcForkTimestamps evmcForkSchedule() const
    {
        scheduler_v1::EvmcForkTimestamps forks;
        forks.londonTime = m_nodeConfig->ethereumForkLondonTime();
        // Paris (The Merge): timestamp from [fork_timestamps] paris_time. Chains
        // with a PoW phase (Sepolia) must set it (1661128380) so pre-merge blocks
        // run at LONDON (DIFFICULTY semantics); pure-PoS chains set it to 0
        // explicitly (0 = active from genesis; the key itself is required).
        forks.parisTime = m_nodeConfig->ethereumForkParisTime();
        forks.shanghaiTime = m_nodeConfig->ethereumForkShanghaiTime();
        forks.cancunTime = m_nodeConfig->ethereumForkCancunTime();
        forks.pragueTime = m_nodeConfig->ethereumForkPragueTime();
        forks.osakaTime = m_nodeConfig->ethereumForkOsakaTime();
        // BPO1/BPO2 don't change the EVM revision but do bump the EIP-7840 blob
        // schedule; the verifier stamps the resolved schedule into the ledger
        // config so the executor picks it up.
        forks.bpo1Time = m_nodeConfig->ethereumForkBpo1Time();
        forks.bpo2Time = m_nodeConfig->ethereumForkBpo2Time();
        return forks;
    }

    /// The shared raw->Transaction decoder (eth_sendRawTransaction / devp2p / verifier all
    /// use the same decodeWeb3RawTransaction path).
    bcos::protocol::Transaction::Ptr decodeRaw(bcos::bytes const& raw) const
    {
        auto cryptoSuite = m_blockFactory->cryptoSuite();
        return bcos::rpc::decodeWeb3RawTransaction(
            bcos::bytesConstRef(raw.data(), raw.size()), *cryptoSuite->hashImpl());
    }

    /// Fatal, non-retryable sync failure: thrown past the per-bootnode catch so the
    /// sync loop STOPS (operator intervention required) instead of trying the next
    /// bootnode — used for finalized-checkpoint mismatches.
    struct FatalSyncError : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    /// Shutdown cancellation: thrown by the per-block download callback when stop()
    /// cleared m_running, so downloadRange(UINT64_MAX) unwinds immediately instead of
    /// streaming blocks until the peer's tip (which would hang the join in stop()).
    /// Caught at the per-bootnode catch site — a normal shutdown, not a sync failure.
    struct SyncCancelled : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    /// Read a 32-byte secp256k1 private key from _path: hex text with an optional
    /// 0x prefix, surrounding whitespace ignored. Every failure names the file.
    static bcos::bytes readNodeKeyFile(std::string const& _path)
    {
        std::ifstream in(_path);
        if (!in)
        {
            throw std::runtime_error("EL sync: cannot open node key file " + _path);
        }
        std::stringstream ss;
        ss << in.rdbuf();
        auto hex = ss.str();
        boost::algorithm::trim(hex);
        if (hex.rfind("0x", 0) == 0 || hex.rfind("0X", 0) == 0)
        {
            hex.erase(0, 2);
        }
        // Exactly 64 hex chars: fromHex pads odd-length input with a leading '0',
        // which would silently shift a 63-char typo into a wrong-but-valid key.
        if (hex.size() != 64 ||
            !std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c); }))
        {
            throw std::runtime_error(
                "EL sync: node key file " + _path + " must hold exactly 64 hex chars " +
                "(a 32-byte secp256k1 private key, optional 0x prefix)");
        }
        return bcos::fromHex(hex);
    }

    /// The node's RLPx identity key. With [ethereum].node_key_file set, load the key
    /// from that file. Empty: persist an auto-generated random key at
    /// node.rlpx.key next to the FISCO node key (private_key_path's directory) on
    /// first start and reuse it afterwards, so the node identity — bootnodes
    /// authenticate us by public key — is stable across restarts.
    bcos::devp2p::rlpx::EccKeyPair loadNodeKey() const
    {
        auto const& configured = m_nodeConfig->ethereumNodeKeyFile();
        if (!configured.empty())
        {
            auto key = readNodeKeyFile(configured);
            INITIALIZER_LOG(INFO)
                << LOG_DESC("EL sync: loaded node key") << LOG_KV("file", configured);
            return makeNodeKeyPair(std::move(key), configured);
        }
        auto const dir = std::filesystem::path(m_nodeConfig->privateKeyPath()).parent_path();
        auto const path = (dir.empty() ? std::filesystem::path(".") : dir) / "node.rlpx.key";
        if (std::filesystem::exists(path))
        {
            auto key = readNodeKeyFile(path.string());
            INITIALIZER_LOG(INFO)
                << LOG_DESC("EL sync: loaded persisted node key") << LOG_KV("file", path);
            return makeNodeKeyPair(std::move(key), path.string());
        }
        bcos::devp2p::rlpx::EccKeyPair generated;  // random keypair
        // Create the (empty) file first and narrow its permissions BEFORE writing
        // the key material: std::ofstream creates with 0666 & ~umask (typically
        // 0644), so writing first would leave the private key world-readable in
        // between.
        {
            std::ofstream create(path);
            if (!create)
            {
                throw std::runtime_error(
                    "EL sync: cannot persist generated node key to " + path.string());
            }
        }
        std::filesystem::permissions(path, std::filesystem::perms::owner_read |
                                               std::filesystem::perms::owner_write);
        {
            std::ofstream out(path, std::ios::trunc);
            if (!out)
            {
                throw std::runtime_error(
                    "EL sync: cannot persist generated node key to " + path.string());
            }
            out << bcos::toHexStringWithPrefix(generated.privateKey()) << '\n';
        }
        INITIALIZER_LOG(INFO) << LOG_DESC("EL sync: generated and persisted node key")
                              << LOG_KV("file", path);
        return generated;
    }

    /// Construct the keypair from file-loaded key material, naming the source file
    /// on an invalid scalar (an out-of-range secp256k1 key must not surface as a
    /// bare invalid_argument from deep inside the crypto layer).
    static bcos::devp2p::rlpx::EccKeyPair makeNodeKeyPair(
        bcos::bytes _privateKey, std::string const& _path)
    {
        try
        {
            return bcos::devp2p::rlpx::EccKeyPair(std::move(_privateKey));
        }
        catch (std::exception const& e)
        {
            throw std::runtime_error(
                "EL sync: invalid secp256k1 private key in " + _path + ": " + e.what());
        }
    }

    /// Startup half of the finalized-checkpoint check: when the local chain already
    /// reaches past the pinned checkpoint height, the committed block there must
    /// carry the pinned hash. Returns false (after a FATAL log) on a mismatch; true
    /// when the checkpoint holds or the local chain has not reached it yet — the
    /// crossing block is then checked as it downloads (see the syncLoop callback).
    /// A storage-level read failure (missing/archived header, backend error)
    /// THROWS — getBlockData fails closed (NotFoundBlockHeader) rather than
    /// returning null — and is converted to a FATAL stop by syncLoop's outer catch.
    bool verifyLocalCheckpoint(
        bcos::tool::NodeConfig::EthereumFinalizedCheckpoint const& _checkpoint) const
    {
        auto current = task::syncWait(ledger::getCurrentBlockNumber(*m_ledger));
        if (current < static_cast<int64_t>(_checkpoint.number))
        {
            return true;
        }
        auto block = task::syncWait(ledger::getBlockData(
            *m_ledger, static_cast<int64_t>(_checkpoint.number), bcos::ledger::HEADER));
        // getBlockData throws NotFoundBlockHeader when the header row is absent, so
        // a returned block always carries the header. The stored Tars header
        // re-encodes to the byte-exact committed RLP (the same invariant the resume
        // anchor relies on), so this hash IS the committed Ethereum block hash.
        bcos::protocol::EthBlockHeader localHeader(*block->blockHeader());
        auto const localHash = bcos::protocol::ethHeaderHash(localHeader.data());
        if (localHash != _checkpoint.hash)
        {
            INITIALIZER_LOG(FATAL)
                << LOG_DESC("EL sync: finalized checkpoint mismatch — the local chain is on "
                            "a wrong fork; refusing to start the sync loop")
                << LOG_KV("checkpointNumber", _checkpoint.number)
                << LOG_KV("expectedHash", _checkpoint.hash.hex())
                << LOG_KV("localHash", localHash.hex())
                << LOG_KV("action",
                    "no chain-rollback tool ships yet, so the only supported recovery is "
                    "a full resync from scratch; verify the bootnode list / "
                    "finalized_checkpoint setting, then restart");
            return false;
        }
        INITIALIZER_LOG(INFO) << LOG_DESC("EL sync: finalized checkpoint verified")
                              << LOG_KV("number", _checkpoint.number)
                              << LOG_KV("hash", _checkpoint.hash.hex());
        return true;
    }

    /// Thread entry point. syncLoopImpl's startup section (checkpoint verification
    /// reads the ledger) sits OUTSIDE its round-level try/catch, so an escaping
    /// exception would reach the std::thread entry and call std::terminate,
    /// aborting the whole node. Convert ANY escape into a FATAL log + stopped
    /// loop instead; the round-level handlers inside syncLoopImpl are unchanged.
    void syncLoop()
    {
        try
        {
            syncLoopImpl();
        }
        catch (std::exception const& e)
        {
            INITIALIZER_LOG(FATAL)
                << LOG_DESC("EL sync: unhandled error escaped the sync loop; stopping")
                << LOG_KV("error", e.what())
                << LOG_KV("diag", boost::current_exception_diagnostic_information());
            m_running.store(false);
        }
    }

    void syncLoopImpl()
    {
        // The verifier runs on the shared v2 scheduler + EthereumExecutor.
        using Verifier = bcos::scheduler_v1::EthereumBlockVerifier<scheduler_v1::SchedulerSerialImpl,
            executor_v1::eth::EthereumExecutor>;
        Verifier verifier(*m_scheduler, *m_executor, *m_blockFactory);
        auto forks = evmcForkSchedule();
        auto chainId = m_nodeConfig->ethereumChainId();
        // v2 always computes the MPT state root itself; the injected calculator must never run.
        using ViewType = GlobalStateStorage::ViewType;
        typename Verifier::template StateRootCalculator<ViewType> stateRootCalc =
            [](ViewType&, uint32_t) -> task::Task<bcos::crypto::HashType> {
            BOOST_THROW_EXCEPTION(std::runtime_error{
                "legacy state-root fold must not run for executor v2 (Ethereum L1 EL mode)"});
        };

        // The node's own identity for the RLPx handshake: loaded/persisted by start()
        // (see loadNodeKey) so bootnodes can authenticate us by a stable public key.
        auto& localKey = *m_localKey;
        auto const mergeBlock = m_nodeConfig->ethereumMergeBlock();
        auto const& checkpoint = m_nodeConfig->ethereumFinalizedCheckpoint();

        // Operator-pinned finalized checkpoint: if the local chain already reaches
        // past it, the committed block at that height MUST carry the pinned hash —
        // a mismatch means the local chain sits on a wrong fork; refuse to sync on.
        if (checkpoint && !verifyLocalCheckpoint(*checkpoint))
        {
            m_running.store(false);
            return;
        }

        // Reorg detection: a parent-hash mismatch means the bootnode's chain does not
        // build on our committed local head. Committed blocks are NOT rolled back (no
        // reorg handling yet), so every following round re-anchors on the same
        // wrong-fork head and fails identically — detect the streak at one anchor and
        // stop with operator guidance instead of spinning forever.
        constexpr size_t c_maxAnchorMismatchStreak = 3;
        int64_t mismatchAnchor = -1;
        size_t mismatchStreak = 0;

        while (m_running.load())
        {
            try
            {
                // Compute the resume point fresh on EVERY round (not once per process):
                // the previous round may have committed blocks, so the next round must
                // resume from the new local head instead of re-downloading what we
                // already have. The chain genesis is pinned separately for the RLPx
                // handshake — it must never change.
                auto resume = resumePoint();
                if (resume.anchor.number != mismatchAnchor)
                {
                    // Anchor advanced (or first round): the mismatch streak resets —
                    // only REPEATED failures at the SAME anchor indicate a reorg.
                    mismatchAnchor = resume.anchor.number;
                    mismatchStreak = 0;
                }
                auto const& anchor = resume.anchor;
                auto const& genesisHeader = resume.genesisHeader;
                auto devp2pConfig = devp2pChainConfig();
                auto bootnodes =
                    bcos::devp2p::sync::loadBootnodes(m_nodeConfig->ethereumBootnodesFile());
                for (auto const& peer : bootnodes)
                {
                    if (!m_running.load())
                    {
                        return;
                    }
                    // A single unreachable bootnode must not stall the round: each peer's
                    // connect + download is fault-isolated so the loop moves on to the next
                    // bootnode (online fallback) and retries the whole list next round.
                    try
                    {
                        // Fill the Status/fork-id fields the peer expects. genesisHash is
                        // ALWAYS the chain genesis (the handshake rejects a peer on a
                        // different chain), headHash reflects the local resume anchor.
                        auto clientConfig = peer;
                        clientConfig.clientId = "FISCO-BCOS-EL/v0.1.0";
                        clientConfig.networkId = chainId;
                        clientConfig.genesisHash = bcos::protocol::ethHeaderHash(genesisHeader);
                        clientConfig.headHash = bcos::protocol::ethHeaderHash(anchor);
                        // totalDifficulty: minimal big-endian u256(0). An EMPTY byte
                        // string RLP-encodes as 0x80 (the canonical RLP integer 0); a
                        // single {0} would encode as 0x00 (non-canonical, rejected by
                        // strict peers). The peer does not use TD for fork choice here.
                        clientConfig.totalDifficulty = {};
                        // EIP-2124 fork-id: mirrors geth's forkid.NewID over the
                        // LOCAL head (genesis on a fresh node), so any Sepolia node
                        // (old or new) accepts us (see computeForkId).
                        clientConfig.forkId = computeForkId(
                            static_cast<uint64_t>(resume.anchor.number),
                            static_cast<uint64_t>(resume.anchor.timestamp));

                        bcos::devp2p::rlpx::RlpxClient client(localKey, clientConfig);
                        INITIALIZER_LOG(INFO)
                            << LOG_DESC("EL sync: connecting to bootnode")
                            << LOG_KV("host", peer.host) << LOG_KV("port", peer.port)
                            << LOG_KV("startNumber", resume.startNumber);
                        auto established = client.connect();
                        INITIALIZER_LOG(INFO)
                            << LOG_DESC("EL sync: handshake OK")
                            << LOG_KV("host", peer.host) << LOG_KV("port", peer.port)
                            << LOG_KV("peerHead",
                                established.peerStatus.headHash.hex().substr(0, 18));

                        // Download from the local head onward: startNumber = anchor + 1,
                        // anchor = local head header (genesis on a fresh node). Blocks are
                        // verified in strictly ascending order, which the verifier's
                        // incremental MPT requires — hence the resume point, never a jump.
                        bcos::devp2p::sync::BlockExchange exchange(
                            resume.startNumber, anchor, devp2pConfig,
                            m_nodeConfig->ethereumMaxBatchSize());

                        auto prevHeader = resume.prevHeader;
                        // Resolve the peer's head NUMBER from its announced head hash
                        // (eth/68 hands us only the hash; one GetBlockHeaders-by-hash
                        // round trip, amount 1 — shares the exchange's request ids).
                        auto peerHead = exchange.requestHeaderByHash(
                            established.session, established.peerStatus.headHash);
                        // Finality lag (two epochs): download only up to 64 blocks
                        // behind the peer head. A block committed at the RAW tip is
                        // vulnerable to a routine 1-2-block tip reorg — which the
                        // three-strike detector below would then turn into a FATAL
                        // stop plus a manual rollback on the next round. Keeping the
                        // committed anchor under the finality lag makes a routine
                        // reorg harmless (the next round simply downloads the new
                        // tip). Real rollback stays a follow-up.
                        constexpr uint64_t c_finalityLag = 64;
                        uint64_t downloadEnd = 0;
                        if (peerHead)
                        {
                            downloadEnd = peerHead->number() > c_finalityLag ?
                                              peerHead->number() - c_finalityLag :
                                              0;
                        }
                        if (!peerHead || downloadEnd < resume.startNumber)
                        {
                            // No safe download window: the peer is behind us, did not
                            // serve the by-hash lookup, or we are already inside the
                            // finality window (caught up). Leave the committed chain
                            // untouched and try the next bootnode / retry next round.
                            INITIALIZER_LOG(INFO)
                                << LOG_DESC("EL sync: no safe download window")
                                << LOG_KV("startNumber", resume.startNumber)
                                << LOG_KV("peerHeadNumber", peerHead ? peerHead->number() : 0)
                                << LOG_KV("peerHeadHash",
                                    established.peerStatus.headHash.hex().substr(0, 18));
                            continue;
                        }
                        uint64_t const downloadCount = downloadEnd - resume.startNumber + 1;
                        INITIALIZER_LOG(INFO)
                            << LOG_DESC("EL sync: starting bounded download")
                            << LOG_KV("startNumber", resume.startNumber)
                            << LOG_KV("downloadEnd", downloadEnd)
                            << LOG_KV("downloadCount", downloadCount)
                            << LOG_KV("peerHead", peerHead->number())
                            << LOG_KV("finalityLag", c_finalityLag)
                            << LOG_KV("batch", m_nodeConfig->ethereumMaxBatchSize());

                        exchange.downloadRange(established.session, downloadCount,
                            [&](bcos::devp2p::sync::Block const& block) {
                                if (!m_running.load())
                                {
                                    // stop() cleared m_running: throw past downloadRange
                                    // (whose remaining > 0 loop has no cancel mechanism)
                                    // so the join in stop() does not wait for the
                                    // peer's tip. Caught below as a normal shutdown.
                                    BOOST_THROW_EXCEPTION(
                                        SyncCancelled("EL sync: cancelled by stop()"));
                                }
                                // Operator-pinned finalized checkpoint: the block that
                                // CROSSES the checkpoint height must carry the pinned
                                // hash. A mismatch means the bootnodes serve a chain
                                // that conflicts with the operator's trust anchor —
                                // fatal, do NOT try the next bootnode.
                                if (checkpoint &&
                                    block.header.number ==
                                        static_cast<int64_t>(checkpoint->number) &&
                                    block.hash != checkpoint->hash)
                                {
                                    BOOST_THROW_EXCEPTION(FatalSyncError(
                                        "EL sync: finalized checkpoint mismatch at block " +
                                        std::to_string(checkpoint->number) + " (downloaded hash " +
                                        block.hash.hex() + " != configured " +
                                        checkpoint->hash.hex() +
                                        "): the bootnodes serve a wrong fork — refusing to "
                                        "commit; verify the bootnode list and the "
                                        "finalized_checkpoint setting"));
                                }
                                auto result = task::syncWait(verifier.verifyAndCommit(
                                    m_globalStateStorageInitializer->storage(), *m_ledger,
                                    block.header, prevHeader, block.transactions,
                                    block.withdrawals, forks, chainId, block.uncles,
                                    mergeBlock,
                                    [this](bcos::bytes const& raw) { return decodeRaw(raw); },
                                    stateRootCalc));
                                if (!result.valid)
                                {
                                    BOOST_THROW_EXCEPTION(std::runtime_error(
                                        "EL sync: block " +
                                        std::to_string(block.header.number) +
                                        " verification failed: " + result.error +
                                        " (computedStateRoot=" + result.stateRoot.hex() +
                                        " headerStateRoot=" + block.header.stateRoot.hex() +
                                        " coinbase=" + block.header.coinbase.hex() +
                                        " difficulty=" + block.header.difficulty.str() +
                                        " gasUsed=" + block.header.gasUsed.str() + ")"));
                                }
                                prevHeader = block.header;
                                INITIALIZER_LOG(INFO)
                                    << LOG_DESC("EL sync: committed block")
                                    << LOG_KV("number", block.header.number)
                                    << LOG_KV("hash", block.hash.hex().substr(0, 18))
                                    << LOG_KV("stateRoot",
                                        result.stateRoot.hex().substr(0, 18));
                            });
                    }
                    catch (SyncCancelled const&)
                    {
                        // stop() requested shutdown mid-download: leave the sync loop
                        // quietly (a normal shutdown, not a sync failure).
                        return;
                    }
                    catch (FatalSyncError const& e)
                    {
                        INITIALIZER_LOG(FATAL)
                            << LOG_DESC("EL sync: fatal error, stopping the sync loop")
                            << LOG_KV("error", e.what());
                        m_running.store(false);
                        return;
                    }
                    catch (std::exception const& e)
                    {
                        if (std::string(e.what()).find("parent hash mismatch") !=
                            std::string::npos)
                        {
                            ++mismatchStreak;
                        }
                        if (mismatchStreak >= c_maxAnchorMismatchStreak)
                        {
                            INITIALIZER_LOG(FATAL)
                                << LOG_DESC("EL sync: repeated parent hash mismatch at the same "
                                            "anchor — the committed local chain is on a fork the "
                                            "bootnodes rejected (reorg); stopping the sync loop")
                                << LOG_KV("anchorNumber", mismatchAnchor)
                                << LOG_KV("streak", mismatchStreak)
                                << LOG_KV("action",
                                    "automatic reorg rollback is not implemented yet and no "
                                    "chain-rollback tool ships, so the only supported "
                                    "recovery is a full resync from scratch; verify the "
                                    "bootnode list / finalized_checkpoint setting, then "
                                    "restart");
                            m_running.store(false);
                            return;
                        }
                        INITIALIZER_LOG(WARNING)
                            << LOG_DESC("EL sync: bootnode failed, trying next")
                            << LOG_KV("host", peer.host) << LOG_KV("port", peer.port)
                            << LOG_KV("error", e.what())
                            << LOG_KV("diag",
                                boost::current_exception_diagnostic_information());
                    }
                }
                // One full pass over the bootnode list: pause briefly before checking for
                // new blocks again (a successful download already advanced the local head;
                // the next round resumes from there).
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
            catch (std::exception const& e)
            {
                if (!m_running.load())
                {
                    return;
                }
                INITIALIZER_LOG(WARNING)
                    << LOG_DESC("EL sync: sync round failed, retrying")
                    << LOG_KV("error", e.what());
                // Back off briefly before retrying the next bootnode / round.
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
        }
    }

    bcos::tool::NodeConfig::Ptr m_nodeConfig;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    std::shared_ptr<scheduler_v1::SchedulerSerialImpl> m_scheduler;
    std::shared_ptr<executor_v1::eth::EthereumExecutor> m_executor;
    GlobalStateStorageInitializer::Ptr m_globalStateStorageInitializer;
    bcos::IOServicePool::Ptr m_ioServicePool;

    std::atomic_bool m_running{false};
    std::thread m_thread;
    // The RLPx identity key, loaded/persisted by start() before the thread spawns.
    std::optional<bcos::devp2p::rlpx::EccKeyPair> m_localKey;
};

}  // namespace bcos::initializer
