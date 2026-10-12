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
 * @file EthereumBlockImport.h
 * @brief Offline import of RLP-encoded Ethereum blocks (`fisco-bcos --import-blocks <path>`).
 *        First step of the ethereum hive-simulator contract: hive injects /chain.rlp
 *        (concatenated top-level RLP blocks) and/or /blocks/<n>.rlp (one block per file)
 *        into the client container and expects them imported BEFORE the node starts
 *        serving. The import runs after the full node init and before any start():
 *        each block goes through the SAME EthereumBlockVerifier lane as devp2p sync
 *        (PoS header rules -> execute -> roots -> MPT state root -> atomic commit),
 *        then the process exits 0 and the entrypoint starts the node normally —
 *        or exits 1 when nothing could be imported on a fresh ledger
 *        (imported == 0, skipped > 0, head never advanced past genesis).
 * @date 2026/9/30
 */
#pragma once

#include "bcos-codec/rlp/RLPDecode.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-framework/ledger/LedgerConfigState.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/mpt/CommitObserver.h"
#include "bcos-rlp-protocol/EthBlockBody.h"
#include "bcos-rlp-protocol/EthBlockHeader.h"
#include "bcos-rlp-protocol/EthGenesisHeader.h"
#include "bcos-rlp-protocol/EthWithdrawal.h"
#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"  // complete type for shared_ptr upcast in decodeRaw()
#include "bcos-task/Wait.h"
#include "bcos-tool/Exceptions.h"
#include "bcos-tool/NodeConfig.h"
#include "bcos-transaction-scheduler/EthereumBlockVerifier.h"
#include "bcos-transaction-scheduler/SchedulerSerialImpl.h"
#include "ethereum-executor/EthereumExecutor.h"
#include "libinitializer/BlockImportSummary.h"
#include "libinitializer/Common.h"
#include "libinitializer/EthereumSyncInitializer.h"
#include "libinitializer/GlobalStateStorageInitializer.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace bcos::initializer
{

/// Offline RLP block importer for Ethereum L1 EL mode. Stateless apart from the
/// wiring handed over from Initializer; import() drives the whole run
/// synchronously (task::syncWait per block) on the caller's thread.
///
/// Per-block failure semantics: a block that fails to decode, fails verification
/// (valid=false), or throws (e.g. the verifier's StaleOrOutOfOrderBlock height
/// guard for a stale replay) is logged and SKIPPED — the run continues with the
/// next block. Note that inside one chain.rlp file later blocks build on the
/// failed one, so they fail the same way; that is accepted at the log level
/// rather than aborting, because the directory mode (hive /blocks) treats every
/// file as an independent input.
class EthereumBlockImporter
{
public:
    // Same verifier type/alias as the devp2p sync lane (EthereumSyncInitializer).
    using Verifier = EthereumSyncInitializer::Verifier;

    // _sharedVerifier: when set (the [engine_rpc] EL wiring built one), import
    // commits through that shared instance so this lane serializes on the same
    // m_commitMutex as every other commit lane; otherwise a private instance is
    // constructed over the same scheduler/executor/blockFactory.
    EthereumBlockImporter(bcos::tool::NodeConfig::Ptr _nodeConfig,
        bcos::ledger::LedgerInterface::Ptr _ledger, bcos::protocol::BlockFactory::Ptr _blockFactory,
        std::shared_ptr<scheduler_v1::SchedulerSerialImpl> _scheduler,
        std::shared_ptr<executor_v1::eth::EthereumExecutor> _executor,
        GlobalStateStorageInitializer::Ptr _globalStateStorageInitializer,
        std::shared_ptr<bcos::ledger::mpt::CommitObserver> _commitObserver = nullptr,
        bcos::ledger::LedgerConfigState::Ptr _ledgerConfigState = nullptr,
        std::shared_ptr<Verifier> _sharedVerifier = nullptr)
      : m_nodeConfig(std::move(_nodeConfig)),
        m_ledger(std::move(_ledger)),
        m_blockFactory(std::move(_blockFactory)),
        m_scheduler(std::move(_scheduler)),
        m_executor(std::move(_executor)),
        m_globalStateStorageInitializer(std::move(_globalStateStorageInitializer)),
        m_commitObserver(std::move(_commitObserver)),
        m_ledgerConfigState(std::move(_ledgerConfigState)),
        m_sharedVerifier(std::move(_sharedVerifier))
    {
        // Offline import reuses the EL pipeline end to end, so its prerequisites are
        // the EL-mode ones: executor v2 (scheduler + executor non-null) and the
        // [eth_genesis_header] sync anchor (the parent header of the first block on
        // a fresh ledger).
        if (!m_scheduler || !m_executor)
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "--import-blocks requires executor_version >= 2 "
                                      "(the v2 EthereumExecutor); set [executor] version=2 "
                                      "in config.genesis"));
        }
        if (!m_nodeConfig->genesisConfig().m_ethGenesisHeader.has_value())
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "--import-blocks requires an [eth_genesis_header] "
                                      "section in config.genesis (import anchor)"));
        }
    }

    /// Run the import over _path (a single chain.rlp-style file or a directory of
    /// one-block *.rlp files). Throws only on hard input errors (path missing,
    /// file unreadable, malformed RLP framing inside a file) — per-block failures
    /// are absorbed into the summary.
    BlockImportSummary import(std::string const& _path)
    {
        auto files = collectBlockFiles(_path);
        INITIALIZER_LOG(INFO) << LOG_DESC("import-blocks: starting offline import")
                              << LOG_KV("path", _path) << LOG_KV("files", files.size());

        auto verifierHolder = m_sharedVerifier ? m_sharedVerifier :
                                                 std::make_shared<Verifier>(*m_scheduler,
                                                     *m_executor, *m_blockFactory,
                                                     m_commitObserver,
                                                     m_nodeConfig->ethereumReorgWindow());
        Verifier& verifier = *verifierHolder;
        auto const forks = EthereumSyncInitializer::evmcForkSchedule(*m_nodeConfig);
        auto const chainId = m_nodeConfig->ethereumChainId();
        auto const mergeBlock = m_nodeConfig->ethereumMergeBlock();
        // v2 always computes the MPT state root itself; the injected calculator must
        // never run (same contract as the devp2p sync lane).
        typename Verifier::template StateRootCalculator<ViewType> stateRootCalc =
            [](ViewType&, uint32_t) -> task::Task<bcos::crypto::HashType> {
            BOOST_THROW_EXCEPTION(std::runtime_error{
                "legacy state-root fold must not run for executor v2 (offline block import)"});
        };

        BlockImportSummary summary;
        // The running parent header: seeded from the ledger resume anchor (genesis on
        // a fresh node, the local head on a resume) and advanced to each committed
        // block. A skipped block does NOT advance it — the parent stays at the last
        // committed block, while the next block's own parentHash/number still refer
        // to the skipped one, so it fails the continuity checks the same way.
        auto parentHeader = resumeParentHeader();
        for (auto const& file : files)
        {
            auto const fileBytes = readFile(file);
            for (auto const& item :
                splitRlpItems(bcos::bytesConstRef(fileBytes.data(), fileBytes.size()),
                    file.string()))
            {
                importOneBlock(verifier, item, file.string(), forks, chainId, mergeBlock,
                    stateRootCalc, parentHeader, summary);
            }
        }

        summary.headNumber = task::syncWait(ledger::getCurrentBlockNumber(*m_ledger));
        summary.headHash = bcos::protocol::ethHeaderHash(parentHeader);
        return summary;
    }

private:
    /// Expand _path to the ordered list of block inputs. A regular file is a
    /// chain.rlp-style blob of concatenated top-level RLP blocks; a directory is
    /// walked for *.rlp files in filename order (the hive /blocks contract), each
    /// holding exactly one block. Throws on a missing/unusable path or an empty set.
    static std::vector<std::filesystem::path> collectBlockFiles(std::string const& _path)
    {
        namespace fs = std::filesystem;
        std::vector<fs::path> files;
        std::error_code ec;
        if (fs::is_regular_file(_path, ec))
        {
            files.emplace_back(_path);
        }
        else if (!ec && fs::is_directory(_path, ec))
        {
            // ec-scoped iteration: a mid-walk failure lands in ec (the tool's own
            // message below) instead of escaping as std::filesystem_error.
            for (fs::directory_iterator it(_path, ec), end; !ec && it != end;
                 it.increment(ec))
            {
                std::error_code entryEc;
                if (it->is_regular_file(entryEc) && !entryEc &&
                    it->path().extension() == ".rlp")
                {
                    files.push_back(it->path());
                }
                else if (entryEc)
                {
                    // A dropped entry leaves a gap in the block sequence and every
                    // later block then fails the parent-hash link — name the file
                    // so the investigation starts here, not at the blocks.
                    INITIALIZER_LOG(WARNING)
                        << LOG_DESC("import-blocks: skipping unreadable directory entry")
                        << LOG_KV("path", it->path().string())
                        << LOG_KV("error", entryEc.message());
                }
            }
            // All files share the same directory, so ordering the full paths is
            // ordering the filenames.
            std::sort(files.begin(), files.end());
        }
        if (ec || files.empty())
        {
            BOOST_THROW_EXCEPTION(
                std::runtime_error("--import-blocks: '" + _path +
                                   "' is neither a readable RLP file nor a directory "
                                   "containing *.rlp files"));
        }
        return files;
    }

    static bcos::bytes readFile(std::filesystem::path const& _file)
    {
        std::ifstream in(_file, std::ios::binary | std::ios::ate);
        if (!in)
        {
            BOOST_THROW_EXCEPTION(
                std::runtime_error("--import-blocks: cannot open " + _file.string()));
        }
        auto const size = in.tellg();
        in.seekg(0);
        bcos::bytes data(static_cast<size_t>(size));
        if (size > 0)
        {
            in.read(reinterpret_cast<char*>(data.data()), size);
        }
        if (!in)
        {
            BOOST_THROW_EXCEPTION(
                std::runtime_error("--import-blocks: failed reading " + _file.string()));
        }
        return data;
    }

    /// Split a chain.rlp-style blob into its concatenated top-level RLP items: each
    /// item is one complete block encoding (rlp([header, txs, ommers, withdrawals?])).
    /// Only the RLP framing (header + payload length) is parsed here; the block
    /// itself is decoded per item later. Malformed framing is a hard error — the
    /// file is corrupt, so continuing would skip an unbounded tail silently.
    static std::vector<bcos::bytes> splitRlpItems(
        bcos::bytesConstRef _buffer, std::string const& _source)
    {
        std::vector<bcos::bytes> items;
        bcos::bytesRef view(const_cast<bcos::byte*>(_buffer.data()), _buffer.size());
        while (!view.empty())
        {
            auto cursor = view;
            auto header = bcos::codec::rlp::tryDecodeHeader(cursor);
            if (!header)
            {
                BOOST_THROW_EXCEPTION(std::runtime_error(
                    "--import-blocks: malformed RLP framing in " + _source + " at offset " +
                    std::to_string(_buffer.size() - view.size()) + ": " +
                    header.error().message));
            }
            auto const itemLength = (view.size() - cursor.size()) + header->payloadLength;
            items.emplace_back(view.data(), view.data() + itemLength);
            view = view.getCroppedData(itemLength);
        }
        return items;
    }

    /// Parent header for the first imported block (same mapping as the sync loop's
    /// resumePoint): genesis anchor on a fresh ledger, else the local head header
    /// converted back to the Ethereum header domain. The EthBlockHeader constructor
    /// already converts the stored FISCO-millisecond timestamp to wire seconds, so
    /// the returned header re-encodes byte-exactly — do NOT divide again.
    bcos::protocol::EthBlockHeaderData resumeParentHeader() const
    {
        auto const current = task::syncWait(ledger::getCurrentBlockNumber(*m_ledger));
        if (current <= 0)
        {
            return bcos::protocol::toEthBlockHeaderData(
                m_nodeConfig->genesisConfig().m_ethGenesisHeader.value());
        }
        auto headBlock =
            task::syncWait(ledger::getBlockData(*m_ledger, current, bcos::ledger::HEADER));
        if (!headBlock || !headBlock->blockHeader())
        {
            BOOST_THROW_EXCEPTION(std::runtime_error(
                "--import-blocks: cannot read local head block " + std::to_string(current)));
        }
        bcos::protocol::EthBlockHeader localHead(*headBlock->blockHeader());
        return localHead.data();
    }

    /// The shared raw->Transaction decoder (eth_sendRawTransaction / devp2p / verifier
    /// all use the same decodeWeb3RawTransaction path).
    bcos::protocol::Transaction::Ptr decodeRaw(bcos::bytes const& raw) const
    {
        auto cryptoSuite = m_blockFactory->cryptoSuite();
        return bcos::rpc::decodeWeb3RawTransaction(
            bcos::bytesConstRef(raw.data(), raw.size()), *cryptoSuite->hashImpl());
    }

    using ViewType = GlobalStateStorage::ViewType;

    /// Decode, verify and commit one block item; on success advance _parentHeader and
    /// count it imported, on ANY failure log a WARNING, count it skipped and leave
    /// _parentHeader untouched.
    void importOneBlock(Verifier& _verifier, bcos::bytes const& _item,
        std::string const& _source, scheduler_v1::EvmcForkTimestamps const& _forks,
        uint64_t _chainId, uint64_t _mergeBlock,
        typename Verifier::template StateRootCalculator<ViewType> const& _stateRootCalc,
        bcos::protocol::EthBlockHeaderData& _parentHeader, BlockImportSummary& _summary);

    bcos::tool::NodeConfig::Ptr m_nodeConfig;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    std::shared_ptr<scheduler_v1::SchedulerSerialImpl> m_scheduler;
    std::shared_ptr<executor_v1::eth::EthereumExecutor> m_executor;
    GlobalStateStorageInitializer::Ptr m_globalStateStorageInitializer;
    std::shared_ptr<bcos::ledger::mpt::CommitObserver> m_commitObserver;
    bcos::ledger::LedgerConfigState::Ptr m_ledgerConfigState;
    std::shared_ptr<Verifier> m_sharedVerifier;
};

inline void EthereumBlockImporter::importOneBlock(Verifier& _verifier, bcos::bytes const& _item,
    std::string const& _source, scheduler_v1::EvmcForkTimestamps const& _forks,
    uint64_t _chainId, uint64_t _mergeBlock,
    typename Verifier::template StateRootCalculator<ViewType> const& _stateRootCalc,
    bcos::protocol::EthBlockHeaderData& _parentHeader, BlockImportSummary& _summary)
{
    bcos::protocol::EthBlock block;
    try
    {
        block.rlpDecode(bcos::bytesConstRef(_item.data(), _item.size()));
    }
    catch (std::exception const& e)
    {
        ++_summary.skipped;
        INITIALIZER_LOG(WARNING) << LOG_DESC("import-blocks: skipping undecodable block")
                                 << LOG_KV("source", _source) << LOG_KV("error", e.what());
        return;
    }
    auto const& blockData = block.data();
    auto const blockHash = bcos::protocol::ethHeaderHash(blockData.header);

    // Chain continuity: the verifier's validateHeaderPoS contract expects the
    // CALLER to have checked the hash link (it checks only number == parent + 1;
    // the devp2p lane links at HeaderChain, the engine lane looks the parent up
    // by hash). Without this, a block with a forged parentHash would execute
    // against the real previous state and commit an unlinked chain.
    auto const expectedParentHash = bcos::protocol::ethHeaderHash(_parentHeader);
    if (blockData.header.parentInfo.blockHash != expectedParentHash)
    {
        ++_summary.skipped;
        INITIALIZER_LOG(WARNING)
            << LOG_DESC("import-blocks: parent hash does not link to the current import "
                        "head, skipping")
            << LOG_KV("number", blockData.header.number)
            << LOG_KV("hash", blockHash.hex().substr(0, 18))
            << LOG_KV("parentHash", blockData.header.parentInfo.blockHash.hex().substr(0, 18))
            << LOG_KV("expected", expectedParentHash.hex().substr(0, 18))
            << LOG_KV("source", _source);
        return;
    }

    // The verifier wants the raw per-item RLP for uncles/withdrawals; the codec is
    // byte-exact, so re-encoding each decoded element reproduces the original bytes
    // (same pattern as EthEngineService's withdrawals sidecar).
    std::vector<bcos::bytes> rawUncles;
    rawUncles.reserve(blockData.ommers.size());
    for (auto const& ommer : blockData.ommers)
    {
        bcos::bytes encoded;
        bcos::codec::rlp::encode(encoded, ommer);
        rawUncles.push_back(std::move(encoded));
    }
    std::optional<std::vector<bcos::bytes>> rawWithdrawals;
    if (blockData.withdrawals.has_value())
    {
        rawWithdrawals.emplace();
        rawWithdrawals->reserve(blockData.withdrawals->size());
        for (auto const& withdrawal : *blockData.withdrawals)
        {
            bcos::bytes encoded;
            bcos::codec::rlp::encode(encoded, withdrawal);
            rawWithdrawals->push_back(std::move(encoded));
        }
    }
    // blockData.transactions already holds the raw EIP-2718 bytes — straight through.

    try
    {
        auto result = task::syncWait(_verifier.verifyAndCommit(
            m_globalStateStorageInitializer->storage(), *m_ledger, blockData.header,
            _parentHeader, blockData.transactions, rawWithdrawals, _forks, _chainId, rawUncles,
            _mergeBlock, [this](bcos::bytes const& raw) { return decodeRaw(raw); },
            _stateRootCalc));
        if (!result.valid)
        {
            ++_summary.skipped;
            INITIALIZER_LOG(WARNING)
                << LOG_DESC("import-blocks: block verification failed, skipping")
                << LOG_KV("number", blockData.header.number)
                << LOG_KV("hash", blockHash.hex().substr(0, 18))
                << LOG_KV("source", _source) << LOG_KV("error", result.error);
            return;
        }
        // The block is committed — advance the running parent and the summary
        // FIRST, so a failure in the post-commit bookkeeping below cannot
        // mis-report this block as skipped and desync the rest of the run.
        _parentHeader = blockData.header;
        ++_summary.imported;
        INITIALIZER_LOG(DEBUG)
            << LOG_DESC("import-blocks: committed block")
            << LOG_KV("number", blockData.header.number)
            << LOG_KV("hash", blockHash.hex().substr(0, 18));
        if (_summary.imported % 1000 == 0)
        {
            INITIALIZER_LOG(INFO) << LOG_DESC("import-blocks: progress")
                                  << LOG_KV("imported", _summary.imported)
                                  << LOG_KV("number", blockData.header.number);
        }
    }
    catch (std::exception const& e)
    {
        // Includes the verifier's StaleOrOutOfOrderBlock height guard (a stale replay
        // of an already-committed block, or a gap left by an earlier skip): logged
        // and skipped like any other per-block failure.
        ++_summary.skipped;
        INITIALIZER_LOG(WARNING) << LOG_DESC("import-blocks: block commit failed, skipping")
                                 << LOG_KV("number", blockData.header.number)
                                 << LOG_KV("hash", blockHash.hex().substr(0, 18))
                                 << LOG_KV("source", _source) << LOG_KV("error", e.what());
        return;
    }
    // TxValidator's "whoever commits a block publishes" contract: this lane
    // bypasses MultiVersionScheduler's publishing wrapper, so republish the
    // post-commit configuration here (same as the devp2p sync lane). Bookkeeping
    // only — a failure here must not count the just-committed block as skipped.
    try
    {
        if (m_ledgerConfigState)
        {
            m_ledgerConfigState->set(task::syncWait(ledger::getLedgerConfig(*m_ledger)));
        }
    }
    catch (std::exception const& e)
    {
        INITIALIZER_LOG(WARNING) << LOG_DESC("import-blocks: ledger-config republish failed")
                                 << LOG_KV("number", blockData.header.number)
                                 << LOG_KV("error", e.what());
    }
}

}  // namespace bcos::initializer
