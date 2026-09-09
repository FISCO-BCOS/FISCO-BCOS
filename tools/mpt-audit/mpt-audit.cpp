/*
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
 * @brief offline auditor and rollback tool for the path-addressed MPT (pathdb spec §11, §13, B.10)
 * @file mpt-audit.cpp
 */

#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/audit/HistoryAudit.h>
#include <bcos-ledger/mpt/audit/HistoryRollback.h>
#include <bcos-ledger/mpt/audit/PathTreeAudit.h>
#include <bcos-ledger/mpt/history/HistoryTables.h>
#include <bcos-storage/RocksDBStorage2.h>
#include <bcos-storage/StateKVResolver.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/program_options.hpp>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::ledger::mpt;

namespace po = boost::program_options;

namespace
{

/// Exit codes, as the tool's contract (they are what a deployment script branches on).
constexpr int kExitConsistent = 0;
constexpr int kExitFindings = 1;
constexpr int kExitUsage = 2;

/// The state plane, exactly as a node writes it: physical keys "<table>:<rowKey>", split back into
/// the pair by StateKeyResolver.
using StateStorage = bcos::storage2::rocksdb::RocksDBStorage2<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue, bcos::storage2::rocksdb::StateKeyResolver,
    bcos::storage2::rocksdb::StateValueResolver>;

/// Raised for anything the operator can fix by re-typing the command line.
struct UsageError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

void printUsage(po::options_description const& options)
{
    std::cout
        << "mpt-audit — offline auditor for the path-addressed MPT node store and its two\n"
           "reverse-history indexes. The node must be STOPPED: the tool opens its RocksDB\n"
           "directly.\n\n"
           "  mpt-audit tree     <db-path> [--tip N] [--expect-root HEX]\n"
           "        Scan /mptp/a and /mptp/s. Checks every node's parent exists, every\n"
           "        parent/child hash agrees, every account leaf's storageRoot matches that\n"
           "        owner's storage trie, and the state root the rows produce equals the one\n"
           "        the chain committed — the tip header's stateRoot, or --expect-root. With\n"
           "        neither available the run says so: it proved the rows agree with each\n"
           "        other, not that they are the right rows. Orphan rows (unreachable,\n"
           "        harmless) are warnings and do NOT change the exit code.\n\n"
           "  mpt-audit history  <db-path> --state-blocks N --proof-blocks N [--tip N]\n"
           "                               [--from N]\n"
           "        Audit both reverse histories against spec B.10's five checks: each\n"
           "        block's meta row against its shards, a meta row for every block in the\n"
           "        window, a REBUILD of the in-memory index that succeeds and accounts for\n"
           "        every record, the retention-boundary row, and each meta row's block hash\n"
           "        against the ledger's s_number_2_hash at that height. The rebuild is the\n"
           "        one a restarting node performs, so its blocks/records/ms are printed.\n\n"
           "  mpt-audit rollback <db-path> --to BLOCK [--state-blocks N --proof-blocks N]\n"
           "                               [--tip N] [--yes]\n"
           "        Reverse-apply both histories from the tip down to BLOCK. Without --yes\n"
           "        this is a dry run that only prints how many rows would change. Every\n"
           "        block in the range must have a meta row in BOTH histories or the whole\n"
           "        rollback is refused before anything is written. After --yes the tree is\n"
           "        re-audited against the header of BLOCK.\n"
           "        SCOPE: this rolls back the state plane and the trie node rows, and\n"
           "        nothing else. Block data — header, number<->hash, transactions,\n"
           "        receipts — and s_current_state:current_number are outside the history's\n"
           "        capture set and are left as they are; the run ends by telling you which\n"
           "        value to set the tip row to.\n\n"
           "Exit codes: 0 = consistent, 1 = findings, 2 = a bad command line or a database\n"
           "that cannot be opened. Everything raised while READING a store — an undecodable\n"
           "row, a hole in the tree, a refused rollback — exits 1, because at that point the\n"
           "tool has an answer about the store and the answer is bad. A rollback that wrote\n"
           "rows but could not verify the result against the target block's header exits 1\n"
           "as well: the store was changed and the check could not be made.\n\n"
        << options << std::endl;
}

/// Open the node's RocksDB. @p writable false uses RocksDB's read-only mode, so an audit cannot
/// modify the store even through a bug.
std::unique_ptr<::rocksdb::DB> openDatabase(std::string const& path, bool writable)
{
    ::rocksdb::Options options;
    options.create_if_missing = false;
    ::rocksdb::DB* raw = nullptr;
    auto const status = writable ? ::rocksdb::DB::Open(options, path, &raw) :
                                   ::rocksdb::DB::OpenForReadOnly(options, path, &raw);
    if (!status.ok())
    {
        throw UsageError("cannot open RocksDB at " + path + ": " + status.ToString());
    }
    return std::unique_ptr<::rocksdb::DB>(raw);
}

/// The chain tip as the node recorded it: s_current_state:current_number, a decimal string.
std::optional<protocol::BlockNumber> readTip(StateStorage& storage)
{
    auto entry = bcos::task::syncWait(bcos::storage2::readOne(storage,
        bcos::executor_v1::StateKey{ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER}));
    if (!entry)
    {
        return std::nullopt;
    }
    auto const text = entry->get();
    protocol::BlockNumber block = 0;
    auto const [ptr, error] = std::from_chars(text.data(), text.data() + text.size(), block);
    if (error != std::errc{})
    {
        return std::nullopt;
    }
    return block;
}

/// The stateRoot the chain committed for @p block, from s_number_2_header. nullopt when the store
/// has no header for that block — an audit store built by hand, or a block below what this node
/// keeps.
/// @throws whatever the header decoder throws: a header row that will not decode is a finding, not
///         a reason to skip the check.
std::optional<bcos::h256> readHeaderStateRoot(StateStorage& storage, protocol::BlockNumber block)
{
    auto entry = bcos::task::syncWait(bcos::storage2::readOne(storage,
        bcos::executor_v1::StateKey{ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(block)}));
    if (!entry)
    {
        return std::nullopt;
    }
    auto const raw = entry->get();
    bcostars::protocol::BlockHeaderImpl header;
    header.decode(bcos::bytesConstRef(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
    return header.stateRoot();
}

/// The hash the LEDGER records for @p block: the `s_number_2_hash` row, 32 raw bytes.
///
/// This is the value B.10 ⑤ compares each meta row against, and it is read from this row rather
/// than recomputed from the header because the row is what both commit paths write and what the
/// ledger itself reads back (LedgerMethods.cpp:384 — "hash from the SYS_NUMBER_2_HASH row (works
/// for OP headers whose in-memory BlockHeader::hash() would throw)"). The same value goes into the
/// meta row at commit time, so a disagreement means the retained pre-images belong to a different
/// block at that height, not that two hash functions were used.
///
/// `ledger::getBlockHash(storage, block, FromStorage{})` (LedgerMethods.h:555-575) reads exactly
/// this row and is the shared way to do it — but it builds `HashType(hashStr, FromBinary)`, which
/// zero-PADS a short string rather than rejecting it. That is right for a caller that already
/// trusts the row; it is wrong here, because this is an auditor and a row of the wrong length is
/// itself a finding. Zero-padding it would turn a malformed row into a plausible hash, report it
/// as a B.10 ⑤ mismatch, and send the operator to compare block hashes when the fault is the row's
/// length. So the length is checked and a wrong one reads as "no hash" — B.10 ⑤ then reports the
/// height as UNVERIFIABLE, which is what it is.
std::optional<bcos::h256> readLedgerBlockHash(StateStorage& storage, protocol::BlockNumber block)
{
    auto entry = bcos::task::syncWait(bcos::storage2::readOne(
        storage, bcos::executor_v1::StateKey{ledger::SYS_NUMBER_2_HASH, std::to_string(block)}));
    if (!entry)
    {
        return std::nullopt;
    }
    auto const raw = entry->get();
    if (raw.size() != bcos::h256::SIZE)
    {
        return std::nullopt;
    }
    return bcos::h256(bcos::bytesConstRef(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
}

/// The root `tree` should hold the store to: --expect-root if given, else the header of @p block.
/// The second element names where it came from, for the line the tool prints.
///
/// @p block nullopt means the tip could not be determined and no --expect-root was given, so there
/// is NOTHING to compare against. It must not fall back to a block number: block 0's header is a
/// real row on most stores, and comparing the tip's tree against genesis reports a healthy store
/// as corrupted.
std::pair<std::optional<bcos::h256>, std::string> committedRoot(StateStorage& storage,
    po::variables_map const& params, std::optional<protocol::BlockNumber> block)
{
    if (params.count("expect-root") != 0U)
    {
        auto text = params["expect-root"].as<std::string>();
        if (text.starts_with("0x") || text.starts_with("0X"))
        {
            text.erase(0, 2);
        }
        // Length checked explicitly: h256's hex constructor right-aligns a short string, which
        // would silently turn a typo into a valid-looking root.
        if (text.size() != bcos::h256::SIZE * 2 || !bcos::isHexString(text))
        {
            throw UsageError("--expect-root must be 32 hex-encoded bytes");
        }
        return {bcos::h256(text), "--expect-root"};
    }
    if (!block)
    {
        return {std::nullopt, "nothing"};
    }
    return {readHeaderStateRoot(storage, *block), "the header of block " + std::to_string(*block)};
}

protocol::BlockNumber requireTip(StateStorage& storage, po::variables_map const& params)
{
    if (params.count("tip") != 0U)
    {
        return params["tip"].as<protocol::BlockNumber>();
    }
    auto const tip = readTip(storage);
    if (!tip)
    {
        throw UsageError(
            "no readable s_current_state:current_number row in this store; pass --tip");
    }
    return *tip;
}

/// The counts and the warnings of one path-tree audit.
///
/// Shared by `tree` and by the re-audit `rollback --yes` performs, because the rollback's re-audit
/// is the SAME audit and its warnings mean the same thing. Reporting only the root there would
/// hide the orphan rows a rollback can create — reverse-applying a block puts node rows back at
/// positions the newer tree had dropped, and any that nothing references afterwards are exactly
/// the "one delete too few" waste (G4) an operator wants to know about before restarting the node.
void printTreeReport(audit::PathTreeAuditReport const& report)
{
    std::cout << "account trie root : " << report.accountRoot.hex() << "\n"
              << "account node rows : " << report.accountNodes << "\n"
              << "accounts          : " << report.accounts << "\n"
              << "storage tries     : " << report.storageTries << "\n"
              << "storage node rows : " << report.storageNodes << "\n"
              << "edges verified    : " << report.verifiedEdges << "\n"
              << "orphan rows       : " << report.orphans << " (" << report.suspectOrphans
              << " from an incomplete storage-trie drop)" << std::endl;
    for (auto const& warning : report.warnings)
    {
        std::cout << "WARNING: " << warning << std::endl;
    }
    if (report.warnings.size() == audit::kMaxAuditWarnings)
    {
        std::cout << "WARNING: warning list truncated at " << audit::kMaxAuditWarnings
                  << " lines; the counts above are complete" << std::endl;
    }
}

int runTree(StateStorage& storage, po::variables_map const& params)
{
    // The tip is only needed to find the header to compare against, so a store without one is
    // still auditable — it just cannot have its root checked.
    auto const tip =
        params.count("tip") != 0U ?
            std::optional<protocol::BlockNumber>{params["tip"].as<protocol::BlockNumber>()} :
            readTip(storage);
    auto const [expected, source] = committedRoot(storage, params, tip);

    auto const report = bcos::task::syncWait(audit::auditPathTree(storage, expected));
    printTreeReport(report);
    if (report.rootChecked)
    {
        std::cout << "state root matches " << source << std::endl;
    }
    else
    {
        // Being UNABLE to compare is not the same as the roots agreeing, and the report must not
        // read as if it were: without a committed root this run proved the rows agree with each
        // other, nothing about them being the right rows.
        std::cout << "WARNING: no committed state root to compare against (no "
                  << ledger::SYS_NUMBER_2_BLOCK_HEADER
                  << " row for the tip and no --expect-root); the tree was checked for internal "
                     "consistency only"
                  << std::endl;
    }
    std::cout << "path tree is consistent"
              << (report.orphans != 0 ? ", with unreachable rows to reclaim" : "") << std::endl;
    return kExitConsistent;
}

/// Print one history report and say whether it was clean.
bool reportHistory(std::string_view label, audit::HistoryAuditReport const& report)
{
    std::cout << label << ": tip " << report.tip << ", depth " << report.depth << ", window ["
              << report.windowStart << ", " << report.tip << "], retained ["
              << report.oldestRetained << ", " << report.newestRetained << "], "
              << report.blocksWithMeta << " blocks, " << report.shardRows << " shard rows, "
              << report.records << " records, boundary ";
    if (report.retentionBoundary)
    {
        std::cout << *report.retentionBoundary;
    }
    else
    {
        std::cout << "<absent>";
    }
    std::cout << std::endl;

    // The rebuild is B.10 ③ AND the operational number an operator sizes a restart with: this is
    // exactly the walk the node performs before it will answer a single historical read, so the
    // time it took here is the time the next start will pay.
    std::cout << "  rebuild: ";
    if (report.rebuilt)
    {
        std::cout << report.rebuiltBlocks << " blocks, " << report.rebuiltRecords << " records, "
                  << report.indexVersions << " index versions, " << report.bytesScanned
                  << " bytes, " << report.rebuildMilliseconds << " ms" << std::endl;
    }
    else
    {
        std::cout << "REFUSED — a node restarting on this store would come up with its history "
                     "unavailable"
                  << std::endl;
    }
    if (!report.blockHashChecked)
    {
        // Being UNABLE to compare is not the same as the hashes agreeing.
        std::cout << "  WARNING: no ledger block hashes were available, so the retained blocks "
                     "were not checked against this chain"
                  << std::endl;
    }

    for (auto const& finding : report.findings)
    {
        std::cout << "  FINDING " << audit::describe(finding.kind) << " at block " << finding.block;
        // The kind names the B.10 item; `detail` is where a finding says which DIRECTION it went,
        // and for the retention boundary that is the whole difference between "this store answers
        // historical reads with today's value" and "it is carrying rows nothing can reach". An
        // operator who never sees it cannot tell those apart.
        if (!finding.detail.empty())
        {
            std::cout << "\n           " << finding.detail;
        }
        std::cout << std::endl;
    }
    return report.consistent();
}

int runHistory(StateStorage& storage, po::variables_map const& params)
{
    auto const tip = requireTip(storage, params);
    auto const stateDepth = params["state-blocks"].as<protocol::BlockNumber>();
    auto const proofDepth = params["proof-blocks"].as<protocol::BlockNumber>();
    auto const from = params["from"].as<protocol::BlockNumber>();

    // B.10 ⑤'s other side. `s_number_2_hash` and not the header row: it holds exactly the hash the
    // commit path handed stageBlockHistory, on BOTH schedulers — BaselineScheduler writes
    // `header->hash()` into it and OpScheduler the OP header hash, which is also what each of them
    // records in the meta row. Decoding the header instead would re-derive a hash, and on the OP
    // side the in-memory `BlockHeader::hash()` is not even the one the chain uses
    // (LedgerMethods.cpp:384 takes the same view).
    audit::BlockHashSource const blockHashAt =
        [&storage](protocol::BlockNumber block) -> std::optional<bcos::h256> {
        return readLedgerBlockHash(storage, block);
    };

    auto const stateReport =
        bcos::task::syncWait(audit::auditStateHistory(storage, tip, stateDepth, from, blockHashAt));
    auto const trieReport =
        bcos::task::syncWait(audit::auditTrieHistory(storage, tip, proofDepth, from, blockHashAt));

    bool const stateClean = reportHistory("StateHistory", stateReport);
    bool const trieClean = reportHistory("TrieHistory ", trieReport);
    if (stateClean && trieClean)
    {
        std::cout << "both history indexes are consistent" << std::endl;
        return kExitConsistent;
    }
    return kExitFindings;
}

int runRollback(StateStorage& storage, po::variables_map const& params)
{
    if (params.count("to") == 0U)
    {
        throw UsageError("rollback needs --to BLOCK");
    }
    auto const tip = requireTip(storage, params);
    auto const target = params["to"].as<protocol::BlockNumber>();
    auto const stateDepth = params["state-blocks"].as<protocol::BlockNumber>();
    auto const proofDepth = params["proof-blocks"].as<protocol::BlockNumber>();
    bool const apply = params.count("yes") != 0U;

    // Always count first, so the operator sees the size of what --yes would do.
    auto const plan = bcos::task::syncWait(
        audit::rollbackTo(storage, tip, target, stateDepth, proofDepth, /*apply=*/false));
    std::cout << "rollback " << tip << " -> " << target << ": " << plan.blocks << " blocks, "
              << plan.stateRows << " state pre-images, " << plan.trieRows << " trie-node pre-images"
              << std::endl;
    if (!apply)
    {
        std::cout << "dry run; pass --yes to apply (the node must be stopped)" << std::endl;
        return kExitConsistent;
    }

    auto const report = bcos::task::syncWait(
        audit::rollbackTo(storage, tip, target, stateDepth, proofDepth, /*apply=*/true));
    std::cout << "applied: " << report.rowsWritten << " rows written, " << report.rowsDeleted
              << " rows deleted" << std::endl;

    // The rows are back; whether they are the RIGHT rows is a separate question, and the header
    // of the target block is the only thing that can answer it. auditPathTree throws on a
    // mismatch, so a successful re-audit is what earns the 0.
    //
    // Without a header there is no re-audit, and the run must not read as verified: this has
    // already WRITTEN to the state plane, so "we changed your store and cannot tell you whether it
    // landed correctly" is a result an operator has to act on, not a footnote.
    int exitCode = kExitConsistent;
    auto const targetRoot = readHeaderStateRoot(storage, target);
    if (targetRoot)
    {
        auto const tree = bcos::task::syncWait(audit::auditPathTree(storage, targetRoot));
        std::cout << "tree re-audited against the header of block " << target << ':' << std::endl;
        printTreeReport(tree);
    }
    else
    {
        std::cout << "FINDING applied but unverified: no " << ledger::SYS_NUMBER_2_BLOCK_HEADER
                  << " row for block " << target
                  << ", so the rolled-back tree could not be checked against the root the chain "
                     "committed. The rows were written. Re-run `mpt-audit tree <db> --expect-root "
                     "<the block's stateRoot>` before restarting the node."
                  << std::endl;
        exitCode = kExitFindings;
    }

    // The tip row is a NEXT STEP, not a finding. It is never in the state history — the capture
    // set is exactly the /apps/ rows the state commitment folds in (HistoryCommit.h's
    // isHistoricalStateRow), and s_current_state is not one of them — so reporting its unchanged
    // value as a fault would fire on every successful rollback and teach the operator to ignore
    // the tool.
    std::cout << "\nNEXT STEP: set " << ledger::SYS_CURRENT_STATE << ':'
              << ledger::SYS_KEY_CURRENT_NUMBER << " to " << target << " (it reads "
              << report.currentNumberRow.value_or("<absent>")
              << "). Block data is not rolled back — this tool restores the state plane and the "
                 "trie node rows only; the header, number<->hash, transaction and receipt rows of "
                 "blocks above "
              << target << " are still on disk." << std::endl;
    return exitCode;
}

}  // namespace

int main(int argc, const char* argv[])
{
    po::options_description options("options");
    options.add_options()("help,h", "this help")("command", po::value<std::string>(),
        "tree | history | rollback")("path", po::value<std::string>(), "RocksDB directory")(
        "state-blocks", po::value<bcos::protocol::BlockNumber>()->default_value(128),
        "H_state, the state-history retention depth")("proof-blocks",
        po::value<bcos::protocol::BlockNumber>()->default_value(128),
        "H_proof, the trie-history retention depth")("tip",
        po::value<bcos::protocol::BlockNumber>(),
        "chain tip; read from s_current_state:current_number when omitted")("from",
        po::value<bcos::protocol::BlockNumber>()->default_value(0),
        "earliest block this node ever wrote history for")(
        "to", po::value<bcos::protocol::BlockNumber>(), "rollback target block")("yes",
        "apply the rollback instead of only counting it")("expect-root", po::value<std::string>(),
        "state root the tree must hash to (32 hex bytes); overrides the block header");

    po::positional_options_description positional;
    positional.add("command", 1).add("path", 1);

    try
    {
        po::variables_map params;
        po::store(po::command_line_parser(argc, argv).options(options).positional(positional).run(),
            params);
        po::notify(params);

        if (params.count("help") != 0U || params.count("command") == 0U)
        {
            printUsage(options);
            return params.count("help") != 0U ? kExitConsistent : kExitUsage;
        }
        auto const command = params["command"].as<std::string>();
        if (params.count("path") == 0U)
        {
            throw UsageError("missing the RocksDB directory");
        }
        auto const path = params["path"].as<std::string>();
        bool const writable = (command == "rollback") && (params.count("yes") != 0U);

        auto database = openDatabase(path, writable);
        StateStorage storage(*database, bcos::storage2::rocksdb::StateKeyResolver{},
            bcos::storage2::rocksdb::StateValueResolver{});

        if (command == "tree")
        {
            return runTree(storage, params);
        }
        if (command == "history")
        {
            return runHistory(storage, params);
        }
        if (command == "rollback")
        {
            return runRollback(storage, params);
        }
        throw UsageError("unknown command '" + command + "'");
    }
    catch (UsageError const& error)
    {
        std::cerr << "mpt-audit: " << error.what() << std::endl;
        return kExitUsage;
    }
    catch (po::error const& error)
    {
        std::cerr << "mpt-audit: " << error.what() << std::endl;
        return kExitUsage;
    }
    catch (std::exception const& error)
    {
        // Every audit failure the library raises lands here: a hole, a hash disagreement, a
        // storageRoot that does not match. The message carries the position (G6, fail loud).
        std::cerr << "mpt-audit: " << boost::diagnostic_information(error) << std::endl;
        return kExitFindings;
    }
}
