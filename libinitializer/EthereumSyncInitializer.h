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
#include "libinitializer/TxGossipService.h"
#include "bcos-devp2p/eth/ForkId.h"
#include "bcos-devp2p/rlpx/Client.h"
#include "bcos-devp2p/sync/BlockExchange.h"
#include "bcos-devp2p/sync/Bootnodes.h"
#include "bcos-devp2p/sync/HeaderValidator.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/mpt/CommitObserver.h"
#include "bcos-tool/NodeConfig.h"
#include "bcos-transaction-scheduler/EthereumBlockVerifier.h"
#include "bcos-transaction-scheduler/SchedulerSerialImpl.h"
#include "bcos-tx-validator/TxValidator.h"
#include "engine/bcos-engine/ClSyncCoordination.h"
#include "bcos-rlp-protocol/EthBlockHeader.h"
#include "bcos-rlp-protocol/EthGenesisHeader.h"
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
/// With the [engine_rpc] wiring the loop carries the shared ClSyncCoordination and is
/// demoted to a CL-driven BACKFILLER: autonomous advance (peer tip minus the finality
/// lag) is only the bootstrap before the first forkchoiceUpdated; from then on the
/// loop idles until the Engine API answers a SYNCING, whose missing hash becomes the
/// backfill target — a bounded forward download straight to that hash (no finality
/// lag: the CL is the finality authority), committed block-by-block like any other
/// download. Without [engine_rpc] the loop stays fully autonomous.
///
/// Current limits: sync is serial and no full-Sepolia time/disk benchmark is
/// published yet; the trusted-bootnode model does not verify PoW/TD or consensus-layer
/// finality; and no rollback tool ships, so a forked committed chain (reorg detection)
/// suspends autonomous advance with an ERROR and waits for the CL / operator instead
/// of stopping the loop — only a finalized-checkpoint mismatch still refuses to sync.
class EthereumSyncInitializer
{
public:
    // The verifier type is a class-level alias so Initializer can construct the shared
    // instance (the Engine API external-payload lane and this sync loop serialize on the
    // verifier's m_commitMutex) without re-spelling the template.
    using Verifier = bcos::scheduler_v1::EthereumBlockVerifier<scheduler_v1::SchedulerSerialImpl,
        executor_v1::eth::EthereumExecutor>;

    // _globalStateStorage: production MultiLayerStorage (GlobalStateStorage).
    // _commitObserver: the shared MPT pruner (storage.mpt_prune_window > 0), forwarded to
    // the verifier so devp2p-synced commits feed pruning like every other commit path;
    // null keeps the verifier's built-in NoopCommitObserver.
    // _sharedVerifier: when set (the [engine_rpc] EL wiring), the sync loop verifies and
    // commits through this shared instance instead of constructing its own.
    // _clSync: the CL-driven coordination state shared with the Engine API service
    // (same [engine_rpc] wiring); null keeps the loop fully autonomous.
    // _gossipMemPool/_gossipValidator: the engine mempool + admission validator for
    // eth/68 transaction gossip ([ethereum] tx_gossip). Both set starts a
    // TxGossipService on dedicated sessions alongside the sync loop; either null
    // (non-EL / OP / engine_rpc-less wiring) keeps gossip off.
    EthereumSyncInitializer(bcos::tool::NodeConfig::Ptr _nodeConfig,
        bcos::ledger::LedgerInterface::Ptr _ledger,
        bcos::protocol::BlockFactory::Ptr _blockFactory,
        std::shared_ptr<scheduler_v1::SchedulerSerialImpl> _scheduler,
        std::shared_ptr<executor_v1::eth::EthereumExecutor> _executor,
        GlobalStateStorageInitializer::Ptr _globalStateStorageInitializer,
        bcos::IOServicePool::Ptr _ioServicePool,
        std::shared_ptr<ledger::mpt::CommitObserver> _commitObserver = nullptr,
        std::shared_ptr<Verifier> _sharedVerifier = nullptr,
        std::shared_ptr<engine::engine_common::ClSyncCoordination> _clSync = nullptr,
        bcos::txpool::MemPoolImpl* _gossipMemPool = nullptr,
        std::shared_ptr<bcos::txvalidator::TxValidator> _gossipValidator = nullptr)
      : m_nodeConfig(std::move(_nodeConfig)),
        m_ledger(std::move(_ledger)),
        m_blockFactory(std::move(_blockFactory)),
        m_scheduler(std::move(_scheduler)),
        m_executor(std::move(_executor)),
        m_globalStateStorageInitializer(std::move(_globalStateStorageInitializer)),
        m_ioServicePool(std::move(_ioServicePool)),
        m_commitObserver(std::move(_commitObserver)),
        m_sharedVerifier(std::move(_sharedVerifier)),
        m_clSync(std::move(_clSync)),
        m_gossipMemPool(_gossipMemPool),
        m_gossipValidator(std::move(_gossipValidator))
    {
        // Construct the gossip service eagerly (cheap, no I/O) so AirNodeInitializer can
        // wire the RPC announce hook BEFORE start() spawns anything; the pumps only run
        // after start(). Constructing needs the config flag too: an off switch must read
        // as "no service" to the wiring, not "a service that idles".
        if (m_gossipMemPool && m_gossipValidator && m_nodeConfig->ethereumTxGossipEnabled())
        {
            TxGossipService::ValidateFn validate =
                [validator = m_gossipValidator](
                    protocol::Transaction& tx) -> task::Task<protocol::TransactionStatus> {
                co_return co_await validator->verify(tx,
                    txvalidator::AdmissionContext::PoolAdmission,
                    txvalidator::SignaturePolicy::Required);
            };
            m_txGossip = std::make_shared<TxGossipService>(*m_gossipMemPool, std::move(validate));
        }
    }

    ~EthereumSyncInitializer() { stop(); }

    EthereumSyncInitializer(EthereumSyncInitializer const&) = delete;
    EthereumSyncInitializer& operator=(EthereumSyncInitializer const&) = delete;

    /// Validate that the EL-mode prerequisites hold (executor v2, fork schedule, bootnode
    /// file readable, genesis anchor present and hashing to the configured
    /// [eth_genesis_header].hash). Throws InvalidConfig on failure.
    void validateConfig() const { validateNodeConfig(*m_nodeConfig); }

    /// NodeConfig-only validation of the EL-mode prerequisites (mode, executor version,
    /// [eth_genesis_header] presence and hash, bootnode file). Static on purpose: every
    /// check reads only NodeConfig, so AirNodeInitializer runs this BEFORE the core node
    /// init — which performs the MPT pruner's boot-time window walk and, with
    /// storage.mpt_prune_sweep_garbage, a whole-keyspace garbage sweep — keeping a config
    /// error fail-fast instead of slow and side-effectful (the same placement rule as the
    /// OP-mode mpt_prune_window refusal, Initializer.cpp). validateConfig() delegates here.
    static void validateNodeConfig(bcos::tool::NodeConfig const& nodeConfig)
    {
        if (!nodeConfig.ethereumELModeEnabled())
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "EthereumSyncInitializer: [ethereum].mode != el"));
        }
        if (nodeConfig.executorVersion() < ledger::ETHEREUM_EXECUTOR_VERSION)
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "Ethereum L1 EL mode requires executor.version >= 2 "
                                      "(the pure-Ethereum executor); set [executor] version=2 "
                                      "in config.genesis"));
        }
        if (!nodeConfig.genesisConfig().m_ethGenesisHeader.has_value())
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "Ethereum L1 EL mode requires an [eth_genesis_header] "
                                      "section in config.genesis (sync anchor)"));
        }
        // The initializer's own genesis projection must hash to the configured
        // [eth_genesis_header].hash. Ledger refuses to start when ITS projection's hash
        // differs from m_hash, but the RLPx Status handshake (genesisHash) and the
        // EIP-2124 fork-id both derive from THIS projection — a drift between the two
        // would disconnect every bootnode with no config error.
        auto const& ethGenesisHeader = nodeConfig.genesisConfig().m_ethGenesisHeader;
        auto const projectedHash = bcos::protocol::ethHeaderHash(
            bcos::protocol::toEthBlockHeaderData(ethGenesisHeader.value()));
        if (projectedHash != ethGenesisHeader->m_hash)
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "Ethereum L1 EL mode: [eth_genesis_header].hash " +
                                      ethGenesisHeader->m_hash.hex() +
                                      " does not match the re-computed genesis hash " +
                                      projectedHash.hex()));
        }
        // Bootnode file must exist and parse (validates the enode list eagerly).
        auto nodes = bcos::devp2p::sync::loadBootnodes(nodeConfig.ethereumBootnodesFile());
        if (nodes.empty())
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "Ethereum L1 EL mode: no bootnodes in " +
                                      nodeConfig.ethereumBootnodesFile()));
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
        // Transaction gossip first: its pumps reference this initializer only through the
        // peer-config factory, and stop() tears it down before the sync thread joins.
        startTxGossip();
        INITIALIZER_LOG(INFO) << LOG_DESC("EL sync: starting self-sync loop")
                              << LOG_KV("bootnodes", m_nodeConfig->ethereumBootnodesFile())
                              << LOG_KV("maxBatch", m_nodeConfig->ethereumMaxBatchSize());
        m_thread = std::thread([this]() { syncLoop(); });
    }

    /// Stop the background thread and join it. The join is unconditional (not gated
    /// on m_running): the sync thread can also exit on its own after a fatal error
    /// (checkpoint mismatch / unhandled escape) with m_running already false, and a
    /// joinable thread that is never joined terminates the process.
    void stop()
    {
        m_running.store(false);
        if (m_txGossip)
        {
            m_txGossip->stop();
        }
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    bool running() const { return m_running.load(); }

    /// The transaction gossip service (null when not wired or [ethereum] tx_gossip=false).
    /// AirNodeInitializer reads it to hook eth_sendRawTransaction's post-admission
    /// announcement.
    std::shared_ptr<TxGossipService> const& txGossip() const { return m_txGossip; }

    // Static: the Engine API external lane (ExternalPayloadVerifier.h) derives the same
    // schedule from NodeConfig, so the schedule has exactly one construction site.
    static scheduler_v1::EvmcForkTimestamps evmcForkSchedule(bcos::tool::NodeConfig const& config)
    {
        scheduler_v1::EvmcForkTimestamps forks;
        forks.londonTime = config.ethereumForkLondonTime();
        // Paris (The Merge): timestamp from [fork_timestamps] paris_time. Chains
        // with a PoW phase (Sepolia) must set it (1661128380) so pre-merge blocks
        // run at LONDON (DIFFICULTY semantics); pure-PoS chains set it to 0
        // explicitly (0 = active from genesis; the key itself is required).
        forks.parisTime = config.ethereumForkParisTime();
        forks.shanghaiTime = config.ethereumForkShanghaiTime();
        forks.cancunTime = config.ethereumForkCancunTime();
        forks.pragueTime = config.ethereumForkPragueTime();
        forks.osakaTime = config.ethereumForkOsakaTime();
        // BPO1/BPO2 don't change the EVM revision but do bump the EIP-7840 blob
        // schedule; the verifier stamps the resolved schedule into the ledger
        // config so the executor picks it up.
        forks.bpo1Time = config.ethereumForkBpo1Time();
        forks.bpo2Time = config.ethereumForkBpo2Time();
        // EIP-6110 deposit contract (Prague+ requestsHash cross-check); NodeConfig
        // defaults it to the mainnet address when [ethereum] deposit_contract_address
        // is unset.
        forks.depositContractAddress = config.ethereumDepositContractAddress();
        return forks;
    }

private:
    /// The chain-genesis anchor header, from [eth_genesis_header]. Used as the download
    /// anchor on a fresh node and pinned as the RLPx handshake genesisHash on every
    /// resume. The field mapping (fork-gated fields copy through only when the genesis
    /// header carries them, so the anchor re-encodes to the byte-exact genesis RLP)
    /// lives in rlp-protocol's toEthBlockHeaderData.
    bcos::protocol::EthBlockHeaderData genesisAnchorHeader() const
    {
        return bcos::protocol::toEthBlockHeaderData(
            m_nodeConfig->genesisConfig().m_ethGenesisHeader.value());
    }

    /// Resume point, refreshed per bootnode attempt within a sync round (a peer's
    /// download may commit blocks and advance the local head mid-round): where to
    /// start downloading and which header anchors the download. A fresh ledger
    /// (only the genesis block) starts at 1 anchored on the genesis header; a
    /// ledger that already has blocks resumes from localHead + 1 anchored on the
    /// local head header (checkpoint resume). genesisHeader is always the chain
    /// genesis — it is what the RLPx handshake pins as the Status genesisHash,
    /// independent of the resume point.
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
        // No logging here: the sync loop calls this per bootnode, so the
        // "resuming from local head" INFO is emitted at the call site, once per
        // head advance.
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
        return bcos::devp2p::eth::forkIdFromTimeLadder(hash,
            static_cast<uint64_t>(genesis.m_timestamp), _localHeadTime,
            {m_nodeConfig->ethereumForkShanghaiTime(), m_nodeConfig->ethereumForkCancunTime(),
                m_nodeConfig->ethereumForkPragueTime(), m_nodeConfig->ethereumForkOsakaTime(),
                m_nodeConfig->ethereumForkBpo1Time(), m_nodeConfig->ethereumForkBpo2Time()});
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

    /// The re-executed block does not match its header (state/receipts/... root
    /// mismatch). Deterministic: every honest peer serves the same block, so the
    /// verification re-fails identically for all of them — the sync loop counts
    /// these toward the deterministic-failure stall, not toward the reorg stop.
    struct BlockVerificationFailed : public std::runtime_error
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
        // The verifier runs on the shared v2 scheduler + EthereumExecutor. The commit
        // observer (the shared MPT pruner when storage.mpt_prune_window > 0, else null)
        // is forwarded so synced commits feed pruning exactly like the PBFT and Engine
        // API commit paths. When Initializer wired the Engine API external lane
        // ([engine_rpc] EL mode), the lanes share ONE verifier instance — the reference
        // below keeps the call sites unchanged either way.
        auto verifierHolder = m_sharedVerifier ?
                                  m_sharedVerifier :
                                  std::make_shared<Verifier>(
                                      *m_scheduler, *m_executor, *m_blockFactory,
                                      m_commitObserver, m_nodeConfig->ethereumReorgWindow());
        Verifier& verifier = *verifierHolder;
        auto forks = evmcForkSchedule(*m_nodeConfig);
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

        // Same-anchor failure streaks, classified by failure ORIGIN: a
        // ParentHashMismatch means the bootnode's chain does not build on our
        // committed local head (a reorg) — three of them at one anchor SUSPEND
        // autonomous advance, because committed blocks are NOT rolled back (no reorg
        // handling yet) and every following round fails identically. The loop itself
        // keeps running: on the [engine_rpc] wiring it still serves CL-directed
        // backfill targets, and everywhere the node stays up for RPC. A
        // HeaderRuleViolation / BlockVerificationFailed is deterministic — the block
        // re-fails identically for every peer — so its streak drops to a slow probing
        // cadence instead of dialing every bootnode every 3s forever. Every
        // other failure (disconnects, timeouts, empty or malformed replies) is
        // transient: WARNING and try the next bootnode, no streak.
        constexpr size_t c_maxAnchorFailureStreak = 3;
        int64_t streakAnchor = -1;
        size_t mismatchStreak = 0;
        size_t deterministicStreak = 0;
        bool deterministicStall = false;
        // Set when the parent-mismatch streak tripped: autonomous advance stays off
        // for the rest of this process lifetime (restart or CL-directed backfill is
        // the recovery; there is no automatic rollback).
        bool autonomousSuspended = false;
        // The CL-driven transition is logged once (first forkchoiceUpdated served),
        // not per round.
        bool clDrivingLogged = false;
        // Last head the "resuming from local head" INFO was emitted for — the
        // line is logged once per head ADVANCE, not once per resumePoint() call.
        int64_t lastLoggedHead = -1;

        // Caught-up backoff: when a whole round over the bootnode list yields no
        // download window (and no peer failure) AND at least one bootnode actually
        // served its head lookup, the local head sits inside that peer's finality
        // window — retry after ~one block interval instead of the 3s behind-cadence,
        // or a synced node re-dials ~14 bootnodes every 3s and trips geth's per-IP
        // inbound dial throttle into WARNING spam. A round where NO bootnode served
        // the by-hash head lookup is not "caught up" — it is a connectivity signal
        // and logs a WARNING instead (same backoff).
        constexpr std::chrono::seconds c_caughtUpBackoff{12};

        while (m_running.load())
        {
            try
            {
                // CL-driven coordination ([engine_rpc] wiring only): a pending
                // backfill target always wins, and once the first forkchoiceUpdated
                // latched CL-driven mode — or the parent-mismatch streak suspended
                // autonomous advance — the autonomous lane stays off. An idling round
                // polls on the 3s retry cadence so a fresh target is picked up fast.
                bool const clDriving = m_clSync && m_clSync->clDriving();
                if (clDriving && !clDrivingLogged)
                {
                    INITIALIZER_LOG(INFO)
                        << LOG_DESC("EL sync: first forkchoiceUpdated served — the CL now "
                                    "drives the chain; autonomous advance stays off for the "
                                    "rest of this process lifetime");
                    clDrivingLogged = true;
                }
                bool const autonomousAllowed = !clDriving && !autonomousSuspended;
                // Refreshed per bootnode below (a target can arrive mid-round).
                std::optional<bcos::h256> backfillTarget =
                    m_clSync ? m_clSync->backfillTarget() : std::nullopt;
                if (!autonomousAllowed && !backfillTarget)
                {
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                    continue;
                }
                auto devp2pConfig = devp2pChainConfig();
                auto bootnodes =
                    bcos::devp2p::sync::loadBootnodes(m_nodeConfig->ethereumBootnodesFile());
                // Round progress tracking: a caught-up round (no peer offered a safe
                // download window) backs off to c_caughtUpBackoff and stays quiet; a
                // round with a download — or with peer failures, which already log a
                // WARNING each — keeps the 3s retry cadence. headLookupMisses counts
                // the peers that declined the by-hash lookup for their own announced
                // head: a round where EVERY bootnode missed is not "caught up" and
                // logs a WARNING at round end (a peer that is merely behind us still
                // counts as served).
                bool madeProgress = false;
                bool anyPeerFailed = false;
                size_t headLookupMisses = 0;
                int64_t lastHeadNumber = -1;
                for (auto const& peer : bootnodes)
                {
                    if (!m_running.load())
                    {
                        return;
                    }
                    // Re-read the CL's backfill target per peer: a SYNCING answer can
                    // post a fresh target mid-round, and a backfill completed by an
                    // earlier peer clears it. The target always wins over autonomous
                    // advance; with no target and the autonomous lane off, the round
                    // is over.
                    backfillTarget = m_clSync ? m_clSync->backfillTarget() : std::nullopt;
                    if (!backfillTarget && !autonomousAllowed)
                    {
                        break;
                    }
                    // Refresh the resume point for EVERY bootnode, not once per
                    // round: a peer whose download commits blocks — fully, or
                    // partially before a mid-range throw — advances the local
                    // head, and the next peer must anchor on the NEW head instead
                    // of re-downloading committed blocks only to be rejected by
                    // the verifier's head+1 guard. The chain genesis is pinned
                    // separately for the RLPx handshake — it must never change.
                    auto resume = resumePoint();
                    lastHeadNumber = resume.anchor.number;
                    if (static_cast<int64_t>(resume.anchor.number) != lastLoggedHead)
                    {
                        // Log the resume line once per head ADVANCE, not once per
                        // resumePoint() call — the per-bootnode refresh would
                        // otherwise emit it 14x per round in the caught-up steady
                        // state, drowning the single round-level status line.
                        INITIALIZER_LOG(INFO)
                            << LOG_DESC("EL sync: resuming from local head")
                            << LOG_KV("headNumber", resume.anchor.number)
                            << LOG_KV("headHash",
                                bcos::protocol::ethHeaderHash(resume.anchor)
                                    .hex()
                                    .substr(0, 18))
                            << LOG_KV("resumeFrom", resume.startNumber);
                        lastLoggedHead = static_cast<int64_t>(resume.anchor.number);
                    }
                    if (resume.anchor.number != streakAnchor)
                    {
                        // Anchor advanced (or first attempt): the streaks reset —
                        // only REPEATED failures at the SAME anchor cannot be
                        // resolved by retrying.
                        streakAnchor = resume.anchor.number;
                        mismatchStreak = 0;
                        deterministicStreak = 0;
                        deterministicStall = false;
                    }
                    auto const& anchor = resume.anchor;
                    auto const& genesisHeader = resume.genesisHeader;
                    auto logPeerFailure = [&](std::exception const& e) {
                        INITIALIZER_LOG(WARNING)
                            << LOG_DESC("EL sync: bootnode failed, trying next")
                            << LOG_KV("host", peer.host) << LOG_KV("port", peer.port)
                            << LOG_KV("error", e.what())
                            << LOG_KV("diag",
                                boost::current_exception_diagnostic_information());
                    };
                    // Shared handling for deterministic failures (the typed
                    // catches below): the same block fails identically for every
                    // bootnode at this anchor, so a streak of them stops
                    // hammering the bootnode list — one ERROR, then slow probing
                    // until the anchor moves. Returns true to break the peer loop.
                    auto onDeterministicFailure = [&](std::exception const& e) {
                        anyPeerFailed = true;
                        ++deterministicStreak;
                        if (deterministicStreak < c_maxAnchorFailureStreak)
                        {
                            logPeerFailure(e);
                            return false;
                        }
                        if (!deterministicStall)
                        {
                            INITIALIZER_LOG(ERROR)
                                << LOG_DESC("EL sync: cannot advance past the local head with "
                                            "any bootnode; backing off and retrying")
                                << LOG_KV("headNumber", streakAnchor)
                                << LOG_KV("streak", deterministicStreak)
                                << LOG_KV("error", e.what());
                            deterministicStall = true;
                        }
                        return true;
                    };
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
                        // DEBUG: in steady state (caught up) the loop re-dials every
                        // bootnode each round — per-peer connect/handshake noise at
                        // INFO would drown the log; the download path below logs its
                        // own INFO with the peer's host/port.
                        INITIALIZER_LOG(DEBUG)
                            << LOG_DESC("EL sync: connecting to bootnode")
                            << LOG_KV("host", peer.host) << LOG_KV("port", peer.port)
                            << LOG_KV("startNumber", resume.startNumber);
                        auto established = client.connect();
                        INITIALIZER_LOG(DEBUG)
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
                        uint64_t downloadEnd = 0;
                        // Backfill only: the exact hash the chain tip must carry when the
                        // download completes (the CL-announced target). Nullopt on the
                        // autonomous lane, whose window tip is not hash-pinned.
                        std::optional<bcos::h256> expectedTipHash;
                        if (backfillTarget)
                        {
                            // CL-directed backfill: pull the chain toward the exact hash the
                            // CL named. NO finality lag — the CL is the finality authority.
                            // The blocks commit through the same per-block callback as the
                            // autonomous lane, so the finalized-checkpoint guard still
                            // applies and a concurrent Engine newPayload commit is absorbed
                            // by the verifier's height guard (StaleOrOutOfOrder below).
                            auto targetHeader = exchange.requestHeaderByHash(
                                established.session, *backfillTarget);
                            if (!targetHeader)
                            {
                                // The peer does not know the target (a very fresh block, or
                                // a peer on another fork): not a peer failure, but counted
                                // for the round-end log like a declined head lookup.
                                ++headLookupMisses;
                                INITIALIZER_LOG(DEBUG)
                                    << LOG_DESC("EL sync: bootnode does not know the "
                                                "CL-announced backfill target")
                                    << LOG_KV("host", peer.host) << LOG_KV("port", peer.port)
                                    << LOG_KV("targetHash", backfillTarget->hex().substr(0, 18));
                                continue;
                            }
                            if (targetHeader->number() <=
                                static_cast<uint64_t>(resume.anchor.number))
                            {
                                // The target sits at/below the committed tip. Either it IS
                                // the committed block there (a concurrent newPayload commit
                                // beat the backfill — nothing to do) or it names a
                                // non-canonical block (a side fork: download cannot serve
                                // it, rollback is a later phase). Both clear the target —
                                // the CL's next SYNCING answer re-arms it if still needed.
                                auto localBlock = task::syncWait(ledger::getBlockData(*m_ledger,
                                    static_cast<int64_t>(targetHeader->number()),
                                    bcos::ledger::HEADER));
                                bcos::protocol::EthBlockHeader localHeader(
                                    *localBlock->blockHeader());
                                auto const localHash =
                                    bcos::protocol::ethHeaderHash(localHeader.data());
                                if (localHash == *backfillTarget)
                                {
                                    INITIALIZER_LOG(INFO)
                                        << LOG_DESC(
                                               "EL sync: backfill target already committed")
                                        << LOG_KV("number", targetHeader->number())
                                        << LOG_KV("hash", backfillTarget->hex().substr(0, 18));
                                }
                                else
                                {
                                    // Phase 3 shallow reorg (EthereumChainRollback.h): the
                                    // CL names a non-canonical block at/below the tip — try
                                    // to rewind the committed chain to just BELOW the target
                                    // and KEEP the target armed, so the next round downloads
                                    // the CL's fork onto the new anchor. When the fork point
                                    // is deeper, the download's first-header mismatch feeds
                                    // the iterated one-block rollback in the
                                    // ParentHashMismatch handler below; a refusal (deeper
                                    // than the reorg window / missing journal) keeps the
                                    // original give-up behavior.
                                    bool rolledBack = false;
                                    if (targetHeader->number() >= 1)
                                    {
                                        try
                                        {
                                            task::syncWait(verifier.rollbackChain(
                                                m_globalStateStorageInitializer->storage(),
                                                static_cast<int64_t>(targetHeader->number()) -
                                                    1));
                                            rolledBack = true;
                                        }
                                        catch (scheduler_v1::RollbackRefused const& refusal)
                                        {
                                            INITIALIZER_LOG(ERROR)
                                                << LOG_DESC("EL sync: reorg rollback toward "
                                                            "the CL-announced target refused")
                                                << LOG_KV("number", targetHeader->number())
                                                << LOG_KV("reason", refusal.what());
                                        }
                                    }
                                    if (rolledBack)
                                    {
                                        madeProgress = true;
                                        INITIALIZER_LOG(INFO)
                                            << LOG_DESC("EL sync: CL-announced target names a "
                                                        "non-canonical block below the tip — "
                                                        "rewound the committed chain; "
                                                        "downloading the CL's fork next round")
                                            << LOG_KV("number", targetHeader->number())
                                            << LOG_KV("targetHash",
                                                backfillTarget->hex().substr(0, 18))
                                            << LOG_KV("localHash", localHash.hex());
                                        // Keep backfillTarget armed: the next round's
                                        // resume anchor sits at target-1 and the download
                                        // runs to the CL's hash.
                                        break;
                                    }
                                    // Not a transient peer failure — but not progress
                                    // either; keep the 3s cadence without a false
                                    // "caught up" round-end line.
                                    anyPeerFailed = true;
                                    INITIALIZER_LOG(ERROR)
                                        << LOG_DESC(
                                               "EL sync: CL-announced target names a "
                                               "non-canonical block at/below the committed tip "
                                               "and the reorg is too deep to roll back; "
                                               "cannot backfill it")
                                        << LOG_KV("number", targetHeader->number())
                                        << LOG_KV("targetHash", backfillTarget->hex())
                                        << LOG_KV("localHash", localHash.hex());
                                }
                                m_clSync->clearBackfillTarget(*backfillTarget);
                                backfillTarget = std::nullopt;
                                break;
                            }
                            downloadEnd = targetHeader->number();
                            expectedTipHash = *backfillTarget;
                            madeProgress = true;
                            INITIALIZER_LOG(INFO)
                                << LOG_DESC("EL sync: starting CL-directed backfill")
                                << LOG_KV("host", peer.host) << LOG_KV("port", peer.port)
                                << LOG_KV("startNumber", resume.startNumber)
                                << LOG_KV("targetNumber", downloadEnd)
                                << LOG_KV("targetHash", backfillTarget->hex().substr(0, 18))
                                << LOG_KV("batch", m_nodeConfig->ethereumMaxBatchSize());
                        }
                        else
                        {
                            // Resolve the peer's head NUMBER from its announced head hash
                            // (eth/68 hands us only the hash; one GetBlockHeaders-by-hash
                            // round trip, amount 1 — shares the exchange's request ids).
                            auto peerHead = exchange.requestHeaderByHash(
                                established.session, established.peerStatus.headHash);
                            // Finality lag (two epochs): download only up to 64 blocks
                            // behind the peer head. A block committed at the RAW tip is
                            // vulnerable to a routine 1-2-block tip reorg — which the
                            // three-strike detector below would then suspend autonomous
                            // advance over. Keeping the committed anchor under the finality
                            // lag makes a routine reorg harmless (the next round simply
                            // downloads the new tip). Real rollback stays a follow-up.
                            constexpr uint64_t c_finalityLag = 64;
                            if (peerHead)
                            {
                                downloadEnd = peerHead->number() > c_finalityLag ?
                                                  peerHead->number() - c_finalityLag :
                                                  0;
                            }
                            else
                            {
                                // The peer declined the by-hash lookup for its own
                                // announced head: not a peer failure, but NOT a "caught
                                // up" signal either — counted for the round-end log.
                                ++headLookupMisses;
                            }
                            if (!peerHead || downloadEnd < resume.startNumber)
                            {
                                // No safe download window: the peer is behind us, did not
                                // serve the by-hash lookup, or we are already inside the
                                // finality window (caught up). Leave the committed chain
                                // untouched and try the next bootnode / retry next round.
                                // DEBUG: this is the steady-state path — the caught-up
                                // state itself is logged once per round below.
                                INITIALIZER_LOG(DEBUG)
                                    << LOG_DESC("EL sync: no safe download window")
                                    << LOG_KV("startNumber", resume.startNumber)
                                    << LOG_KV("peerHeadNumber", peerHead ? peerHead->number() : 0)
                                    << LOG_KV("peerHeadHash",
                                        established.peerStatus.headHash.hex().substr(0, 18));
                                continue;
                            }
                            madeProgress = true;
                            INITIALIZER_LOG(INFO)
                                << LOG_DESC("EL sync: starting bounded download")
                                << LOG_KV("host", peer.host) << LOG_KV("port", peer.port)
                                << LOG_KV("startNumber", resume.startNumber)
                                << LOG_KV("downloadEnd", downloadEnd)
                                << LOG_KV("downloadCount", downloadEnd - resume.startNumber + 1)
                                << LOG_KV("peerHead", peerHead->number())
                                << LOG_KV("finalityLag", c_finalityLag)
                                << LOG_KV("batch", m_nodeConfig->ethereumMaxBatchSize());
                        }
                        uint64_t const downloadCount = downloadEnd - resume.startNumber + 1;
                        bcos::h256 lastDownloadedHash;
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
                                    BOOST_THROW_EXCEPTION(BlockVerificationFailed(
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
                                lastDownloadedHash = block.hash;
                                INITIALIZER_LOG(INFO)
                                    << LOG_DESC("EL sync: committed block")
                                    << LOG_KV("number", block.header.number)
                                    << LOG_KV("hash", block.hash.hex().substr(0, 18))
                                    << LOG_KV("stateRoot",
                                        result.stateRoot.hex().substr(0, 18));
                            });
                        if (expectedTipHash)
                        {
                            // Backfill: the downloaded window must END at the exact hash
                            // the CL named — a full window whose tip differs means the
                            // peer's chain at that height is not the CL's chain: the same
                            // fork/reorg classification as a first-header mismatch.
                            if (lastDownloadedHash != *expectedTipHash)
                            {
                                BOOST_THROW_EXCEPTION(bcos::devp2p::sync::ParentHashMismatch(
                                    "EL sync: backfill reached the target height " +
                                    std::to_string(downloadEnd) + " but the peer's block there (" +
                                    lastDownloadedHash.hex() + ") is not the CL-announced hash " +
                                    expectedTipHash->hex()));
                            }
                            m_clSync->clearBackfillTarget(*expectedTipHash);
                            backfillTarget = std::nullopt;
                            INITIALIZER_LOG(INFO)
                                << LOG_DESC("EL sync: CL-directed backfill complete")
                                << LOG_KV("targetNumber", downloadEnd)
                                << LOG_KV("targetHash", expectedTipHash->hex().substr(0, 18));
                        }
                        // Successful download: end the round here rather than
                        // chaining another bounded download from the next
                        // bootnode. madeProgress is already true, so the round
                        // end keeps the 3s retry cadence, and the next round
                        // re-anchors on the new local head (the anchor advance
                        // resets the failure streak).
                        break;
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
                    catch (bcos::devp2p::sync::ParentHashMismatch const& e)
                    {
                        // Reorg signal: the peer's chain does not build on our
                        // committed local head (or does not end at the CL-announced
                        // backfill target). ONLY this failure type feeds the
                        // suspension — it demands three corroborating mismatches at
                        // one anchor, never a mismatch padded out by transient errors.
                        anyPeerFailed = true;
                        ++mismatchStreak;
                        if (mismatchStreak >= c_maxAnchorFailureStreak && !autonomousSuspended)
                        {
                            // Phase 3 shallow reorg (EthereumChainRollback.h): before
                            // suspending, try to rewind ONE committed block. The iterated
                            // form walks the anchor back to the fork point one block per
                            // tripped streak; a successful rollback changes the resume
                            // anchor, which resets this streak automatically (the
                            // anchor-change reset above). Refusal (fork point deeper than
                            // the reorg window, or a missing journal) falls through to the
                            // original suspension.
                            bool rolledBack = false;
                            auto const localTip =
                                task::syncWait(ledger::getCurrentBlockNumber(*m_ledger));
                            if (localTip >= 1)
                            {
                                try
                                {
                                    task::syncWait(verifier.rollbackChain(
                                        m_globalStateStorageInitializer->storage(),
                                        localTip - 1));
                                    rolledBack = true;
                                }
                                catch (scheduler_v1::RollbackRefused const& refusal)
                                {
                                    INITIALIZER_LOG(ERROR)
                                        << LOG_DESC("EL sync: reorg rollback refused")
                                        << LOG_KV("headNumber", localTip)
                                        << LOG_KV("reason", refusal.what());
                                }
                            }
                            if (rolledBack)
                            {
                                INITIALIZER_LOG(INFO)
                                    << LOG_DESC("EL sync: repeated parent hash mismatch — "
                                                "rewound the committed chain by one block; "
                                                "retrying the download from the new anchor")
                                    << LOG_KV("oldHead", localTip)
                                    << LOG_KV("newHead", localTip - 1)
                                    << LOG_KV("streak", mismatchStreak);
                            }
                            else
                            {
                                // The reorg is deeper than the rollback window serves.
                                // Suspend autonomous advance for the rest of this process
                                // lifetime instead of stopping the loop: the node stays up
                                // for RPC and, on the [engine_rpc] wiring, keeps serving
                                // CL-directed backfill targets. The current target is
                                // unreachable the same way (the CL's chain does not build
                                // on the committed head), so drop it rather than retrying
                                // it forever; the CL's next SYNCING answer re-arms it.
                                autonomousSuspended = true;
                                if (backfillTarget && m_clSync)
                                {
                                    m_clSync->clearBackfillTarget(*backfillTarget);
                                    backfillTarget = std::nullopt;
                                }
                                INITIALIZER_LOG(ERROR)
                                    << LOG_DESC("EL sync: repeated parent hash mismatch at the "
                                                "same anchor and the reorg is too deep to roll "
                                                "back; autonomous advance suspended, waiting "
                                                "for CL direction or operator intervention")
                                    << LOG_KV("anchorNumber", streakAnchor)
                                    << LOG_KV("streak", mismatchStreak)
                                    << LOG_KV("action",
                                        "the reorg exceeds [ethereum] reorg_window; the only "
                                        "supported recovery is a full resync from scratch — "
                                        "verify the bootnode list / finalized_checkpoint "
                                        "setting, then restart");
                            }
                        }
                        logPeerFailure(e);
                    }
                    catch (bcos::devp2p::sync::HeaderRuleViolation const& e)
                    {
                        if (onDeterministicFailure(e))
                        {
                            break;
                        }
                    }
                    catch (BlockVerificationFailed const& e)
                    {
                        if (onDeterministicFailure(e))
                        {
                            break;
                        }
                    }
                    catch (scheduler_v1::StaleOrOutOfOrderBlock const& e)
                    {
                        // The verifier's height guard: the block is not the ledger
                        // head + 1. Deterministic — no peer retry repairs a
                        // wrong-height request.
                        if (onDeterministicFailure(e))
                        {
                            break;
                        }
                    }
                    catch (std::exception const& e)
                    {
                        // Transient (network-/peer-shaped): connect failures,
                        // disconnects, timeouts, empty or malformed replies.
                        // WARNING and try the next bootnode; no streak, so two
                        // transient errors can never corroborate a reorg suspension
                        // or pin the loop into the deterministic stall.
                        anyPeerFailed = true;
                        logPeerFailure(e);
                    }
                }
                // End of the round — either one pass over the bootnode list, or an
                // early exit after the first successful download / a tripped
                // failure streak. Pause briefly before checking for new blocks
                // again (a successful download already advanced the local head;
                // the next round resumes from there).
                if (deterministicStall)
                {
                    // Slow probing cadence while the deterministic failure
                    // persists; the ERROR was logged once when the stall tripped.
                    std::this_thread::sleep_for(c_caughtUpBackoff);
                }
                else if (madeProgress || anyPeerFailed)
                {
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                }
                else
                {
                    if (headLookupMisses >= bootnodes.size())
                    {
                        // NOT caught up: every bootnode this round declined the
                        // by-hash lookup (its own announced head, or the CL-announced
                        // backfill target), so nothing here proves the local head is
                        // current — log a WARNING instead of a false healthy "caught
                        // up". Same backoff.
                        INITIALIZER_LOG(WARNING)
                            << LOG_DESC("EL sync: no bootnode served the head lookup this round")
                            << LOG_KV("bootnodes", bootnodes.size())
                            << LOG_KV("headNumber", lastHeadNumber)
                            << LOG_KV("retrySeconds", c_caughtUpBackoff.count());
                    }
                    else
                    {
                        // Caught up: at least one peer served its head/window answer
                        // (a peer merely behind us counts as served), no peer offered
                        // a safe download window and none failed, so the local head
                        // sits inside the serving bootnodes' finality window. Back off
                        // to ~one block interval and log the state once per round
                        // instead of per-peer-per-round.
                        INITIALIZER_LOG(INFO)
                            << LOG_DESC("EL sync: caught up with the bootnode tips")
                            << LOG_KV("headNumber", lastHeadNumber)
                            << LOG_KV("retrySeconds", c_caughtUpBackoff.count());
                    }
                    std::this_thread::sleep_for(c_caughtUpBackoff);
                }
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
    std::shared_ptr<ledger::mpt::CommitObserver> m_commitObserver;
    std::shared_ptr<Verifier> m_sharedVerifier;
    std::shared_ptr<engine::engine_common::ClSyncCoordination> m_clSync;
    bcos::txpool::MemPoolImpl* m_gossipMemPool = nullptr;
    std::shared_ptr<bcos::txvalidator::TxValidator> m_gossipValidator;
    std::shared_ptr<TxGossipService> m_txGossip;

    /// Start the gossip pumps when the service was constructed (see the ctor). The peer
    /// config factory mirrors the sync loop's per-peer Status fields — genesis pinned,
    /// head/fork-id recomputed from the local resume point at every (re)connect.
    void startTxGossip()
    {
        if (!m_txGossip)
        {
            return;
        }
        TxGossipService::PeerConfigFactory configFactory =
            [this](bcos::devp2p::rlpx::PeerConfig const& bootnode) {
                auto resume = resumePoint();
                auto config = bootnode;
                config.clientId = "FISCO-BCOS-EL/v0.1.0";
                config.networkId = m_nodeConfig->ethereumChainId();
                config.genesisHash = bcos::protocol::ethHeaderHash(resume.genesisHeader);
                config.headHash = bcos::protocol::ethHeaderHash(resume.anchor);
                // Minimal big-endian u256(0), same as the sync loop's Status: 0x80.
                config.totalDifficulty = {};
                config.forkId = computeForkId(static_cast<uint64_t>(resume.anchor.number),
                    static_cast<uint64_t>(resume.anchor.timestamp));
                return config;
            };
        m_txGossip->start(*m_localKey,
            bcos::devp2p::sync::loadBootnodes(m_nodeConfig->ethereumBootnodesFile()),
            std::move(configFactory));
    }

    std::atomic_bool m_running{false};
    std::thread m_thread;
    // The RLPx identity key, loaded/persisted by start() before the thread spawns.
    std::optional<bcos::devp2p::rlpx::EccKeyPair> m_localKey;
};

}  // namespace bcos::initializer
