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
 * @file HalfCommitChecker.cpp
 * @brief Half-commit (orphan block) detection over a node state database (spec §5.12)
 */
#include "HalfCommitChecker.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Common.h"
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <boost/lexical_cast.hpp>
#include <optional>
#include <ostream>
#include <set>

namespace bcos::ledger::halfcommit
{

namespace
{

std::optional<protocol::BlockNumber> readCurrentNumber(::rocksdb::DB& db)
{
    std::string value;
    auto status = db.Get(::rocksdb::ReadOptions{},
        storage::toDBKey(SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER), &value);
    if (!status.ok())
    {
        return std::nullopt;
    }
    try
    {
        return boost::lexical_cast<protocol::BlockNumber>(value);
    }
    catch (boost::bad_lexical_cast const&)
    {
        return std::nullopt;
    }
}

// Block numbers of all rows in `table` above `threshold`. Row keys are the
// decimal block number (Ledger::asyncPrewriteBlock uses
// boost::lexical_cast<std::string>(header->number())), stored under the
// physical key `<table>:<key>` (storage::toDBKey).
std::set<protocol::BlockNumber> rowsAbove(
    ::rocksdb::DB& db, std::string_view table, protocol::BlockNumber threshold)
{
    std::set<protocol::BlockNumber> result;
    auto prefix = storage::toDBKey(table, {});
    std::unique_ptr<::rocksdb::Iterator> iterator{db.NewIterator(::rocksdb::ReadOptions{})};
    for (iterator->Seek(prefix); iterator->Valid() && iterator->key().starts_with(prefix);
        iterator->Next())
    {
        auto rowKey = iterator->key().ToStringView().substr(prefix.size());
        protocol::BlockNumber blockNumber{};
        try
        {
            blockNumber = boost::lexical_cast<protocol::BlockNumber>(rowKey);
        }
        catch (boost::bad_lexical_cast const&)
        {
            continue;  // not a ledger-written row; ledger only writes numeric keys here
        }
        if (blockNumber > threshold)
        {
            result.insert(blockNumber);
        }
    }
    return result;
}

}  // namespace

ScanReport scan(::rocksdb::DB& db, protocol::BlockFactory& blockFactory)
{
    ScanReport report;
    auto currentNumber = readCurrentNumber(db);
    if (!currentNumber)
    {
        report.dbCorrupted = true;
        report.corruptionReason =
            fmt::format("missing or non-numeric {}:{} row — not a node state database?",
                SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER);
        return report;
    }
    report.currentNumber = *currentNumber;

    auto txOrphans = rowsAbove(db, SYS_NUMBER_2_TXS, report.currentNumber);
    auto nonceOrphans = rowsAbove(db, SYS_BLOCK_NUMBER_2_NONCES, report.currentNumber);
    if (txOrphans != nonceOrphans)
    {
        report.dbCorrupted = true;
        report.corruptionReason = fmt::format(
            "inconsistent indexes above current_number={}: {} has blocks [{}] but {} has blocks "
            "[{}]",
            report.currentNumber, SYS_NUMBER_2_TXS, fmt::join(txOrphans, ", "),
            SYS_BLOCK_NUMBER_2_NONCES, fmt::join(nonceOrphans, ", "));
        return report;
    }

    report.orphans.reserve(txOrphans.size());
    for (auto blockNumber : txOrphans)
    {
        std::string value;
        auto status = db.Get(::rocksdb::ReadOptions{},
            storage::toDBKey(SYS_NUMBER_2_TXS, boost::lexical_cast<std::string>(blockNumber)),
            &value);
        if (!status.ok())
        {
            report.dbCorrupted = true;
            report.corruptionReason = fmt::format("failed to re-read {} row of block {}: {}",
                SYS_NUMBER_2_TXS, blockNumber, status.ToString());
            return report;
        }

        OrphanBlock orphan{.blockNumber = blockNumber, .txHashes = {}};
        try
        {
            auto block = blockFactory.createBlock(
                bytesConstRef(reinterpret_cast<const byte*>(value.data()), value.size()),
                /*calculateHash*/ false, /*checkSig*/ false);
            orphan.txHashes.reserve(block->transactionsMetaDataSize());
            for (uint64_t i = 0; i < block->transactionsMetaDataSize(); ++i)
            {
                orphan.txHashes.emplace_back(block->transactionHash(i));
            }
        }
        catch (std::exception const& e)
        {
            report.dbCorrupted = true;
            report.corruptionReason = fmt::format(
                "undecodable {} row of block {}: {}", SYS_NUMBER_2_TXS, blockNumber, e.what());
            return report;
        }
        report.orphans.push_back(std::move(orphan));
    }
    return report;
}

std::string formatReport(ScanReport const& report)
{
    if (report.dbCorrupted)
    {
        return fmt::format(
            "*** DB CORRUPTION DETECTED ***\n  reason: {}\n  currentNumber: {}\n"
            "  Operator should investigate before restarting the node.\n",
            report.corruptionReason, report.currentNumber);
    }

    auto out = fmt::format(
        "currentNumber: {}\norphans: {}\n", report.currentNumber, report.orphans.size());
    for (auto const& orphan : report.orphans)
    {
        out += fmt::format("  block {}, tx_count={}\n", orphan.blockNumber, orphan.txHashes.size());
    }
    return out;
}

void dumpOrphans(ScanReport const& report, std::ostream& out)
{
    for (auto const& orphan : report.orphans)
    {
        for (auto const& hash : orphan.txHashes)
        {
            out << orphan.blockNumber << " " << hash.hexPrefixed() << "\n";
        }
    }
}

}  // namespace bcos::ledger::halfcommit
