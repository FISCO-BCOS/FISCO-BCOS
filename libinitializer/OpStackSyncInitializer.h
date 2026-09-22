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
 * @file OpStackSyncInitializer.h
 * @brief OP-Stack EL-mode self-sync driver ([ethereum] mode=opstack-el): connect to
 *        op-geth RLPx bootnodes, download blocks with BlockExchange (OP header
 *        validation via makeOpHeaderValidator), verify + commit each block with
 *        OpBlockVerifier (execute -> incremental MPT -> commitments -> ledger), in
 *        a dedicated background thread. Wired from AirNodeInitializer when
 *        [ethereum] mode=opstack-el; the config file is the source of truth.
 *
 * Mirrors EthereumSyncInitializer (the L1 EL driver) section for section; the
 * OP-specific pieces are the fork-id ladder (eth/OpForkId.h, op-geth's
 * reflection-gathered ChainConfig fork set), the header validator
 * (sync/OpHeaderValidator.h) and the verifier (opstack-executor/OpBlockVerifier.h,
 * whose typed exceptions drive the failure classification below).
 * @date 2026/9/22
 */
#pragma once

#include "libinitializer/Common.h"
#include "libinitializer/GlobalStateStorageInitializer.h"
#include "bcos-devp2p/eth/OpForkId.h"
#include "bcos-devp2p/rlpx/Client.h"
#include "bcos-devp2p/sync/BlockExchange.h"
#include "bcos-devp2p/sync/Bootnodes.h"
#include "bcos-devp2p/sync/OpHeaderValidator.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/mpt/CommitObserver.h"
#include "bcos-tool/NodeConfig.h"
#include "bcos-rlp-protocol/EthBlockHeader.h"
#include "bcos-rlp-protocol/EthGenesisHeader.h"
#include "bcos-task/Wait.h"
#include <opstack-executor/OpBlockVerifier.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/algorithm/string/trim.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
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

/// Self-contained OP-Stack EL-mode sync client. Owns a background thread that
/// repeatedly: (1) loads the bootnode list (op-geth EL serving peers), (2) connects
/// to each in turn, (3) downloads blocks from the local head onward and verifies +
/// commits each through OpBlockVerifier, and (4) loops forever (catching transient
/// network errors).
///
/// Current limits (same as the L1 driver): sync is serial; the trusted-bootnode
/// model does not check the op-node derivation chain or L1 finality; and no rollback
/// tool ships, so a fatal fork/checkpoint stop requires a full resync until the
/// follow-up recovery work lands.
class OpStackSyncInitializer
{
public:
    // _globalStateStorageInitializer: production MultiLayerStorage (GlobalStateStorage),
    // handed to OpBlockVerifier (its execution/commit plane). _commitObserver: the
    // shared MPT pruner (storage.mpt_prune_window > 0), forwarded to the verifier so
    // devp2p-synced commits feed pruning like every other commit path; null keeps the
    // verifier's built-in NoopCommitObserver.
    OpStackSyncInitializer(bcos::tool::NodeConfig::Ptr _nodeConfig,
        bcos::ledger::LedgerInterface::Ptr _ledger,
        bcos::protocol::BlockFactory::Ptr _blockFactory,
        GlobalStateStorageInitializer::Ptr _globalStateStorageInitializer,
        bcos::IOServicePool::Ptr _ioServicePool,
        std::shared_ptr<ledger::mpt::CommitObserver> _commitObserver = nullptr)
      : m_nodeConfig(std::move(_nodeConfig)),
        m_ledger(std::move(_ledger)),
        m_blockFactory(std::move(_blockFactory)),
        m_globalStateStorageInitializer(std::move(_globalStateStorageInitializer)),
        m_ioServicePool(std::move(_ioServicePool)),
        m_commitObserver(std::move(_commitObserver))
    {}

    ~OpStackSyncInitializer() { stop(); }

    OpStackSyncInitializer(OpStackSyncInitializer const&) = delete;
    OpStackSyncInitializer& operator=(OpStackSyncInitializer const&) = delete;

    /// Validate that the opstack-el prerequisites hold (mode, OP executor version, OP
    /// fork schedule, genesis anchor present and hashing to the configured
    /// [eth_genesis_header].hash, bootnode file readable). Throws InvalidConfig on failure.
    void validateConfig() const { validateNodeConfig(*m_nodeConfig); }

    /// NodeConfig-only validation of the opstack-el prerequisites. Static on purpose:
    /// every check reads only NodeConfig, so AirNodeInitializer runs this BEFORE the
    /// core node init — which performs the MPT pruner's boot-time window walk and, with
    /// storage.mpt_prune_sweep_garbage, a whole-keyspace garbage sweep — keeping a config
    /// error fail-fast instead of slow and side-effectful (the same placement rule as
    /// the L1 EL driver, EthereumSyncInitializer::validateNodeConfig).
    static void validateNodeConfig(bcos::tool::NodeConfig const& nodeConfig)
    {
        if (!nodeConfig.opStackELModeEnabled())
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "OpStackSyncInitializer: [ethereum].mode != opstack-el"));
        }
        if (nodeConfig.executorVersion() < ledger::OPSTACK_EXECUTOR_VERSION)
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "opstack-el mode requires executor.version >= 3 "
                                      "(the OP lane); set [executor] version=3 "
                                      "in config.genesis"));
        }
        if (!nodeConfig.opForkSchedule().has_value())
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "opstack-el mode requires an [op_fork_timestamps] "
                                      "section in config.genesis (the OP fork schedule)"));
        }
        if (nodeConfig.ethereumChainId() == 0)
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "opstack-el mode requires a non-zero [web3] chain_id in "
                                      "config.genesis (the OP chain id, e.g. 11155420 for "
                                      "op-sepolia)"));
        }
        if (!nodeConfig.genesisConfig().m_ethGenesisHeader.has_value())
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "opstack-el mode requires an [eth_genesis_header] "
                                      "section in config.genesis (sync anchor)"));
        }
        // The initializer's own genesis projection must hash to the configured
        // [eth_genesis_header].hash — the RLPx Status handshake (genesisHash) and the
        // EIP-2124 fork-id both derive from THIS projection; a drift would disconnect
        // every bootnode with no config error. (Same check as the L1 driver.)
        auto const& ethGenesisHeader = nodeConfig.genesisConfig().m_ethGenesisHeader;
        auto const projectedHash = bcos::protocol::ethHeaderHash(
            bcos::protocol::toEthBlockHeaderData(ethGenesisHeader.value()));
        if (projectedHash != ethGenesisHeader->m_hash)
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "opstack-el mode: [eth_genesis_header].hash " +
                                      ethGenesisHeader->m_hash.hex() +
                                      " does not match the re-computed genesis hash " +
                                      projectedHash.hex()));
        }
        // Bootnode file must exist and parse (validates the enode list eagerly).
        auto nodes = bcos::devp2p::sync::loadBootnodes(nodeConfig.ethereumBootnodesFile());
        if (nodes.empty())
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "opstack-el mode: no bootnodes in " +
                                      nodeConfig.ethereumBootnodesFile() +
                                      " (expected op-geth EL serving peers of the OP chain)"));
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
        INITIALIZER_LOG(INFO) << LOG_DESC("OP-EL sync: starting self-sync loop")
                              << LOG_KV("bootnodes", m_nodeConfig->ethereumBootnodesFile())
                              << LOG_KV("maxBatch", m_nodeConfig->ethereumMaxBatchSize())
                              << LOG_KV("chainId", m_nodeConfig->ethereumChainId())
                              << LOG_KV("blockTimeSeconds", m_nodeConfig->opBlockTimeSeconds());
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
        bcos::protocol::EthBlockHeaderData genesisHeader; // chain genesis (handshake pin)
    };

    ResumePoint resumePoint() const
    {
        auto genesisHeader = genesisAnchorHeader();
        auto current = task::syncWait(ledger::getCurrentBlockNumber(*m_ledger));
        if (current <= 0)
        {
            // Fresh node: only the genesis block exists; start downloading at 1.
            return {1, genesisHeader, genesisHeader};
        }
        // Resume from the local head: anchor = local head header, start at head + 1.
        auto headBlock = task::syncWait(
            ledger::getBlockData(*m_ledger, current, bcos::ledger::HEADER));
        if (!headBlock || !headBlock->blockHeader())
        {
            BOOST_THROW_EXCEPTION(std::runtime_error(
                "OP-EL sync: cannot read local head block " + std::to_string(current) +
                " for resume"));
        }
        // Convert the stored Tars header back to the Ethereum header domain. The
        // EthBlockHeader constructor copies the fork-gated optionals only when the
        // stored header carries them, so the anchor re-encodes to the same RLP that
        // was committed — byte-exact resume. The Tars timestamp is FISCO milliseconds
        // and the constructor converts to wire seconds — do NOT divide again (the L1
        // driver documents the same pitfall).
        bcos::protocol::EthBlockHeader localHead(*headBlock->blockHeader());
        auto head = localHead.data();
        return {static_cast<uint64_t>(current + 1), head, genesisHeader};
    }

    /// The header-validation chain config: chain id ([web3] chain_id, pinned by
    /// validateL2Invariants for opstack-el), the block cadence knob ([ethereum]
    /// op_block_time_seconds) and the ten OP fork times from [op_fork_timestamps].
    bcos::devp2p::sync::OpChainConfig opChainConfig() const
    {
        auto const& schedule = m_nodeConfig->opForkSchedule().value();
        bcos::devp2p::sync::OpChainConfig config;
        config.chainId = m_nodeConfig->ethereumChainId();
        config.blockTimeSeconds = m_nodeConfig->opBlockTimeSeconds();
        config.regolithTime = schedule.m_regolithTime;
        config.canyonTime = schedule.m_canyonTime;
        config.deltaTime = schedule.m_deltaTime;
        config.ecotoneTime = schedule.m_ecotoneTime;
        config.fjordTime = schedule.m_fjordTime;
        config.graniteTime = schedule.m_graniteTime;
        config.holoceneTime = schedule.m_holoceneTime;
        config.isthmusTime = schedule.m_isthmusTime;
        config.jovianTime = schedule.m_jovianTime;
        config.karstTime = schedule.m_karstTime;
        return config;
    }

    /// EIP-2124 fork-id announced in the eth Status handshake. op-geth gathers the OP
    /// fork set by reflection over its ChainConfig (core/forkid/forkid.go gatherForks)
    /// after LoadOPStackChainConfig (params/superchain.go) maps Canyon->ShanghaiTime,
    /// Ecotone->CancunTime, Isthmus->PragueTime: the ladder is exactly
    /// canyon/ecotone/fjord/granite/holocene/isthmus/jovian/karst (Regolith is always 0
    /// and dropped, Delta has no ChainConfig field, OsakaTime stays nil on OP chains —
    /// Karst is its own field). The fork-id reflects the LOCAL head, not wall-clock
    /// time (see eth/OpForkId.h and the L1 driver's computeForkId for why).
    bcos::devp2p::eth::ForkId computeForkId(uint64_t _localHeadTime) const
    {
        auto const& genesis = m_nodeConfig->genesisConfig().m_ethGenesisHeader.value();
        auto const& schedule = m_nodeConfig->opForkSchedule().value();
        bcos::devp2p::eth::OpForkIdLadder const ladder{{
            schedule.m_canyonTime, schedule.m_ecotoneTime, schedule.m_fjordTime,
            schedule.m_graniteTime, schedule.m_holoceneTime, schedule.m_isthmusTime,
            schedule.m_jovianTime, schedule.m_karstTime,
        }};
        return bcos::devp2p::eth::computeOpForkId(genesis.m_hash,
            static_cast<uint64_t>(genesis.m_timestamp), _localHeadTime, ladder);
    }

    /// Fatal, non-retryable sync failure: thrown past the per-bootnode catch so the
    /// sync loop STOPS (operator intervention required) instead of trying the next
    /// bootnode — used for finalized-checkpoint mismatches.
    struct FatalSyncError : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    /// Shutdown cancellation: thrown by the per-block download callback when stop()
    /// cleared m_running, so downloadRange unwinds immediately instead of streaming
    /// blocks until the peer's tip (which would hang the join in stop()). Caught at
    /// the per-bootnode catch site — a normal shutdown, not a sync failure.
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
            throw std::runtime_error("OP-EL sync: cannot open node key file " + _path);
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
                "OP-EL sync: node key file " + _path + " must hold exactly 64 hex chars " +
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
                << LOG_DESC("OP-EL sync: loaded node key") << LOG_KV("file", configured);
            return makeNodeKeyPair(std::move(key), configured);
        }
        auto const dir = std::filesystem::path(m_nodeConfig->privateKeyPath()).parent_path();
        auto const path = (dir.empty() ? std::filesystem::path(".") : dir) / "node.rlpx.key";
        if (std::filesystem::exists(path))
        {
            auto key = readNodeKeyFile(path.string());
            INITIALIZER_LOG(INFO)
                << LOG_DESC("OP-EL sync: loaded persisted node key") << LOG_KV("file", path);
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
                    "OP-EL sync: cannot persist generated node key to " + path.string());
            }
        }
        std::filesystem::permissions(path, std::filesystem::perms::owner_read |
                                               std::filesystem::perms::owner_write);
        {
            std::ofstream out(path, std::ios::trunc);
            if (!out)
            {
                throw std::runtime_error(
                    "OP-EL sync: cannot persist generated node key to " + path.string());
            }
            out << bcos::toHexStringWithPrefix(generated.privateKey()) << '\n';
        }
        INITIALIZER_LOG(INFO) << LOG_DESC("OP-EL sync: generated and persisted node key")
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
                "OP-EL sync: invalid secp256k1 private key in " + _path + ": " + e.what());
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
        // re-encodes to the byte-exact committed RLP (OpBlockVerifier's header
        // projection keeps the RLP identity), so this hash IS the committed OP
        // block hash.
        bcos::protocol::EthBlockHeader localHeader(*block->blockHeader());
        auto const localHash = bcos::protocol::ethHeaderHash(localHeader.data());
        if (localHash != _checkpoint.hash)
        {
            INITIALIZER_LOG(FATAL)
                << LOG_DESC("OP-EL sync: finalized checkpoint mismatch — the local chain is on "
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
        INITIALIZER_LOG(INFO) << LOG_DESC("OP-EL sync: finalized checkpoint verified")
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
                << LOG_DESC("OP-EL sync: unhandled error escaped the sync loop; stopping")
                << LOG_KV("error", e.what())
                << LOG_KV("diag", boost::current_exception_diagnostic_information());
            m_running.store(false);
        }
    }

    void syncLoopImpl()
    {
        // The verifier executes each block itself (it builds its own serial scheduler
        // and OpstackExecutor per block internally); the dependencies it shares with
        // the rest of the node are the block factory, the global state storage, the
        // ledger, the IO pool and the commit observer (the shared MPT pruner when
        // storage.mpt_prune_window > 0, else null -> the verifier's built-in Noop).
        using Verifier =
            bcos::executor_v1::opstack::OpBlockVerifier<GlobalStateStorage>;
        Verifier verifier(m_blockFactory->receiptFactory(),
            m_blockFactory->cryptoSuite()->hashImpl(), m_nodeConfig->ethereumChainId(),
            m_nodeConfig->opForkSchedule().value(), m_blockFactory,
            m_globalStateStorageInitializer->storage(), m_ledger, m_ioServicePool,
            m_commitObserver);
        auto const opConfig = opChainConfig();

        // The node's own identity for the RLPx handshake: loaded/persisted by start()
        // (see loadNodeKey) so bootnodes can authenticate us by a stable public key.
        auto& localKey = *m_localKey;
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
        // committed local head (a reorg) — three of them at one anchor stop the
        // loop, because committed blocks are NOT rolled back (no reorg handling
        // yet) and every following round fails identically. A HeaderRuleViolation /
        // OpConsensusError (incl. OpBlockVerificationFailed) /
        // OpStaleOrOutOfOrderBlock is deterministic — the block re-fails
        // identically for every peer — so its streak drops to a slow probing
        // cadence instead of dialing every bootnode every 3s forever. Every
        // other failure (disconnects, timeouts, empty or malformed replies, and
        // local storage faults such as OpStorageError) is transient: WARNING and
        // try the next bootnode, no streak.
        constexpr size_t c_maxAnchorFailureStreak = 3;
        int64_t streakAnchor = -1;
        size_t mismatchStreak = 0;
        size_t deterministicStreak = 0;
        bool deterministicStall = false;
        // Last head the "resuming from local head" INFO was emitted for — the
        // line is logged once per head ADVANCE, not once per resumePoint() call.
        int64_t lastLoggedHead = -1;

        // Caught-up backoff: when a whole round over the bootnode list yields no
        // download window (and no peer failure) AND at least one bootnode actually
        // served its head lookup, the local head sits inside that peer's finality
        // window — retry after ~one block interval instead of the 3s behind-cadence.
        // A round where NO bootnode served the by-hash head lookup is not "caught
        // up" — it is a connectivity signal and logs a WARNING instead (same
        // backoff). (Same round accounting as the L1 driver.)
        constexpr std::chrono::seconds c_caughtUpBackoff{12};

        while (m_running.load())
        {
            try
            {
                auto bootnodes =
                    bcos::devp2p::sync::loadBootnodes(m_nodeConfig->ethereumBootnodesFile());
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
                    // Refresh the resume point for EVERY bootnode, not once per
                    // round: a peer whose download commits blocks advances the
                    // local head, and the next peer must anchor on the NEW head
                    // instead of re-downloading committed blocks only to be
                    // rejected by the verifier's head+1 guard. The chain genesis
                    // is pinned separately for the RLPx handshake.
                    auto resume = resumePoint();
                    lastHeadNumber = resume.anchor.number;
                    if (static_cast<int64_t>(resume.anchor.number) != lastLoggedHead)
                    {
                        INITIALIZER_LOG(INFO)
                            << LOG_DESC("OP-EL sync: resuming from local head")
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
                            << LOG_DESC("OP-EL sync: bootnode failed, trying next")
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
                                << LOG_DESC("OP-EL sync: cannot advance past the local head "
                                            "with any bootnode; backing off and retrying")
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
                        clientConfig.clientId = "FISCO-BCOS-OP-EL/v0.1.0";
                        clientConfig.networkId = opConfig.chainId;
                        clientConfig.genesisHash = bcos::protocol::ethHeaderHash(genesisHeader);
                        clientConfig.headHash = bcos::protocol::ethHeaderHash(anchor);
                        // totalDifficulty: minimal big-endian u256(0). An EMPTY byte
                        // string RLP-encodes as 0x80 (the canonical RLP integer 0); a
                        // single {0} would encode as 0x00 (non-canonical, rejected by
                        // strict peers). OP chains are post-merge from genesis (Bedrock),
                        // so TD is 0 for every block.
                        clientConfig.totalDifficulty = {};
                        // EIP-2124 fork-id over the LOCAL head (genesis on a fresh
                        // node), op-geth's OP ladder (see computeForkId).
                        clientConfig.forkId =
                            computeForkId(static_cast<uint64_t>(resume.anchor.timestamp));

                        bcos::devp2p::rlpx::RlpxClient client(localKey, clientConfig);
                        // DEBUG: in steady state (caught up) the loop re-dials every
                        // bootnode each round — per-peer connect/handshake noise at
                        // INFO would drown the log; the download path below logs its
                        // own INFO with the peer's host/port.
                        INITIALIZER_LOG(DEBUG)
                            << LOG_DESC("OP-EL sync: connecting to bootnode")
                            << LOG_KV("host", peer.host) << LOG_KV("port", peer.port)
                            << LOG_KV("startNumber", resume.startNumber);
                        auto established = client.connect();
                        INITIALIZER_LOG(DEBUG)
                            << LOG_DESC("OP-EL sync: handshake OK")
                            << LOG_KV("host", peer.host) << LOG_KV("port", peer.port)
                            << LOG_KV("peerHead",
                                established.peerStatus.headHash.hex().substr(0, 18));

                        // Download from the local head onward: startNumber = anchor + 1,
                        // anchor = local head header (genesis on a fresh node). Blocks are
                        // verified in strictly ascending order, which the verifier's
                        // incremental MPT requires — hence the resume point, never a jump.
                        // The header validator is swapped to the OP ruleset (the default
                        // is the L1 PoS ruleset; the devp2p ChainConfig is unused once a
                        // custom validator is installed).
                        bcos::devp2p::sync::BlockExchange exchange(resume.startNumber, anchor,
                            bcos::devp2p::sync::ChainConfig{},
                            m_nodeConfig->ethereumMaxBatchSize());
                        exchange.setHeaderValidator(
                            bcos::devp2p::sync::makeOpHeaderValidator(opConfig));

                        // Resolve the peer's head NUMBER from its announced head hash
                        // (eth/68 hands us only the hash; one GetBlockHeaders-by-hash
                        // round trip, amount 1 — shares the exchange's request ids).
                        auto peerHead = exchange.requestHeaderByHash(
                            established.session, established.peerStatus.headHash);
                        // Finality lag: download only up to 64 blocks behind the peer
                        // head (~128 s at the OP 2s cadence). A block committed at the
                        // RAW tip is vulnerable to a routine tip reorg — which the
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
                            INITIALIZER_LOG(DEBUG)
                                << LOG_DESC("OP-EL sync: no safe download window")
                                << LOG_KV("startNumber", resume.startNumber)
                                << LOG_KV("peerHeadNumber", peerHead ? peerHead->number() : 0)
                                << LOG_KV("peerHeadHash",
                                    established.peerStatus.headHash.hex().substr(0, 18));
                            continue;
                        }
                        madeProgress = true;
                        uint64_t const downloadCount = downloadEnd - resume.startNumber + 1;
                        INITIALIZER_LOG(INFO)
                            << LOG_DESC("OP-EL sync: starting bounded download")
                            << LOG_KV("host", peer.host) << LOG_KV("port", peer.port)
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
                                        SyncCancelled("OP-EL sync: cancelled by stop()"));
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
                                        "OP-EL sync: finalized checkpoint mismatch at block " +
                                        std::to_string(checkpoint->number) +
                                        " (downloaded hash " + block.hash.hex() +
                                        " != configured " + checkpoint->hash.hex() +
                                        "): the bootnodes serve a wrong fork — refusing to "
                                        "commit; verify the bootnode list and the "
                                        "finalized_checkpoint setting"));
                                }
                                // verifyAndCommit is a single atomic step: height guard,
                                // fork resolution, execution, incremental MPT state root,
                                // six-way commitment comparison and the FIB-104 commit.
                                // Its typed exceptions drive the classification below.
                                auto result = task::syncWait(verifier.verifyAndCommit(block));
                                INITIALIZER_LOG(INFO)
                                    << LOG_DESC("OP-EL sync: committed block")
                                    << LOG_KV("number", block.header.number)
                                    << LOG_KV("hash", block.hash.hex().substr(0, 18))
                                    << LOG_KV("stateRoot",
                                        result.commitments.stateRoot.hex().substr(0, 18));
                            });
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
                            << LOG_DESC("OP-EL sync: fatal error, stopping the sync loop")
                            << LOG_KV("error", e.what());
                        m_running.store(false);
                        return;
                    }
                    catch (bcos::devp2p::sync::ParentHashMismatch const& e)
                    {
                        // Reorg signal: the peer's chain does not build on our
                        // committed local head. ONLY this failure type feeds the
                        // reorg stop — the FATAL (the node's one unrecoverable
                        // action) demands three corroborating mismatches at one
                        // anchor, never a mismatch padded out by transient errors.
                        anyPeerFailed = true;
                        ++mismatchStreak;
                        if (mismatchStreak >= c_maxAnchorFailureStreak)
                        {
                            INITIALIZER_LOG(FATAL)
                                << LOG_DESC("OP-EL sync: repeated parent hash mismatch at the "
                                            "same anchor — the committed local chain is on a "
                                            "fork the bootnodes rejected (reorg); stopping the "
                                            "sync loop")
                                << LOG_KV("anchorNumber", streakAnchor)
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
                        logPeerFailure(e);
                    }
                    catch (bcos::devp2p::sync::HeaderRuleViolation const& e)
                    {
                        if (onDeterministicFailure(e))
                        {
                            break;
                        }
                    }
                    catch (bcos::executor_v1::opstack::OpBlockVerificationFailed const& e)
                    {
                        // Commitment mismatch (an OpConsensusError subclass — must be
                        // caught FIRST): the executed block diverges from the announced
                        // header; every honest peer serves the same block, so this
                        // re-fails identically for all of them.
                        if (onDeterministicFailure(e))
                        {
                            break;
                        }
                    }
                    catch (bcos::executor_v1::opstack::OpStaleOrOutOfOrderBlock const& e)
                    {
                        // The verifier's height guard: the block is not the ledger
                        // head + 1. Deterministic — no peer retry repairs a
                        // wrong-height request.
                        if (onDeterministicFailure(e))
                        {
                            break;
                        }
                    }
                    catch (bcos::evm::OpConsensusError const& e)
                    {
                        // Deterministic OP consensus rejection (inadmissible tx type
                        // byte, undecodable envelope, block-shape violation, ...).
                        if (onDeterministicFailure(e))
                        {
                            break;
                        }
                    }
                    catch (std::exception const& e)
                    {
                        // Transient (network-/peer-shaped): connect failures,
                        // disconnects, timeouts, empty or malformed replies. Local
                        // storage faults (bcos::evm::engine::OpStorageError) also land
                        // here: they are not consensus failures, and the next round's
                        // retry is the only recovery that exists. WARNING and try the
                        // next bootnode; no streak, so two transient errors can never
                        // corroborate a reorg FATAL or pin the loop into the
                        // deterministic stall.
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
                        // by-hash lookup for its own announced head, so nothing here
                        // proves the local head is current — log a WARNING instead of
                        // a false healthy "caught up". Same backoff.
                        INITIALIZER_LOG(WARNING)
                            << LOG_DESC(
                                   "OP-EL sync: no bootnode served the head lookup this round")
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
                            << LOG_DESC("OP-EL sync: caught up with the bootnode tips")
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
                    << LOG_DESC("OP-EL sync: sync round failed, retrying")
                    << LOG_KV("error", e.what());
                // Back off briefly before retrying the next bootnode / round.
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
        }
    }

    bcos::tool::NodeConfig::Ptr m_nodeConfig;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    GlobalStateStorageInitializer::Ptr m_globalStateStorageInitializer;
    bcos::IOServicePool::Ptr m_ioServicePool;
    std::shared_ptr<ledger::mpt::CommitObserver> m_commitObserver;

    std::atomic_bool m_running{false};
    std::thread m_thread;
    // The RLPx identity key, loaded/persisted by start() before the thread spawns.
    std::optional<bcos::devp2p::rlpx::EccKeyPair> m_localKey;
};

}  // namespace bcos::initializer
