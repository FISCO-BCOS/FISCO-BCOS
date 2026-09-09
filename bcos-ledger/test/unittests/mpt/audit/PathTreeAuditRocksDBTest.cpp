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
 * @file PathTreeAuditRocksDBTest.cpp
 * @brief The path-tree audit against the storage production runs on.
 *
 * MemoryStorage orders rows by the (table, rowKey) pair; RocksDB orders the single physical string
 * "<table>:<rowKey>" bytewise. The audit's two scans both seek into a table and then rely on
 * walking off its end to stop, and node row keys carry arbitrary bytes (every compactPath starts
 * with 0x00 or 0x1n, and a storage row key opens with 32 raw owner bytes), so the stop condition
 * is worth pinning against the real comparator rather than only the in-memory stand-in.
 *
 * Setting MPT_AUDIT_TEST_DB_DIR makes this case write its two databases under that directory and
 * KEEP them, which is how the mpt-audit CLI gets a store to run against (one clean, one with a
 * flipped child hash). Unset — the normal case, including CI — it uses a random path and removes
 * it.
 */

#include "AuditTestHelpers.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-ledger/mpt/PathKey.h>
#include <bcos-storage/RocksDBStorage2.h>
#include <bcos-storage/StateKVResolver.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-task/Wait.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <cstdlib>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::audit::test
{

BOOST_AUTO_TEST_SUITE(PathTreeAuditSuite)

/// One header row carrying @p stateRoot. Written by hand rather than through Ledger — this is a
/// store for the auditor to read, not a chain.
void writeBlockHeader(
    RocksDbStateStorage& storage, protocol::BlockNumber block, bcos::h256 const& stateRoot)
{
    bcostars::protocol::BlockHeaderImpl header;
    header.setNumber(block);
    header.setStateRoot(stateRoot);
    bcos::bytes encoded;
    header.encode(encoded);

    bcos::storage::Entry headerEntry;
    headerEntry.set(std::string(encoded.begin(), encoded.end()));
    bcos::task::syncWait(bcos::storage2::writeOne(storage,
        bcos::executor_v1::StateKey{ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(block)},
        std::move(headerEntry)));
}

/// The two rows the CLI needs besides the node rows: that header, and the tip.
void writeChainMetadata(
    RocksDbStateStorage& storage, protocol::BlockNumber block, bcos::h256 const& stateRoot)
{
    writeBlockHeader(storage, block, stateRoot);
    bcos::storage::Entry numberEntry;
    numberEntry.set(std::to_string(block));
    bcos::task::syncWait(bcos::storage2::writeOne(storage,
        bcos::executor_v1::StateKey{ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER},
        std::move(numberEntry)));
}

/// The block number the kept smoke databases stand at.
constexpr protocol::BlockNumber kSmokeTipBlock = 7;

BOOST_AUTO_TEST_CASE(rocksDbBackedTreeIsAuditedTheSameWay)
{
    auto const [base, keep] = auditRocksDbBase();

    // The tree: four accounts, two of them with a storage trie, built by the ordinary builder.
    mpt::test::NodeMemoryStorage nodes;
    std::vector<std::pair<bcos::Address, Account>> accounts;
    accounts.emplace_back(mpt::test::makeAddress(0x01),
        makeAccountWithStorage(nodes, mpt::test::makeAddress(0x01), {}));
    accounts.emplace_back(mpt::test::makeAddress(0x02),
        makeAccountWithStorage(nodes, mpt::test::makeAddress(0x02),
            {{mpt::test::makeHash(0x11), bcos::bytes(32, bcos::byte{0x07})},
                {mpt::test::makeHash(0x22), bcos::bytes(32, bcos::byte{0x09})}}));
    accounts.emplace_back(mpt::test::makeAddress(0x03),
        makeAccountWithStorage(nodes, mpt::test::makeAddress(0x03), {}));
    accounts.emplace_back(mpt::test::makeAddress(0x04),
        makeAccountWithStorage(nodes, mpt::test::makeAddress(0x04),
            {{mpt::test::makeHash(0x33), bcos::bytes(32, bcos::byte{0x05})}}));
    auto const stateRoot = mpt::test::seedStateTrieFlushed(nodes, accounts);

    // The shortest non-empty account position: a direct child of the root either way.
    bcos::bytes childPosition;
    for (auto const& [position, raw] : mpt::test::scanTrieNodes(nodes, TrieScope::account()))
    {
        if (!position.empty())
        {
            childPosition = position;
            break;
        }
    }
    BOOST_REQUIRE(!childPosition.empty());

    {
        AuditRocksDb clean(base + "-clean", keep);
        RocksDbStateStorage storage(*clean.db, bcos::storage2::rocksdb::StateKeyResolver{},
            bcos::storage2::rocksdb::StateValueResolver{});
        landNodeRowsOnFlat(nodes, storage);
        writeChainMetadata(storage, kSmokeTipBlock, stateRoot);

        auto const report = runPathTreeAudit(storage, stateRoot);
        BOOST_CHECK(report.rootChecked);
        BOOST_CHECK_EQUAL(report.accountRoot.hex(), stateRoot.hex());
        BOOST_CHECK_EQUAL(report.accounts, 4);
        BOOST_CHECK_EQUAL(report.storageTries, 2);
        BOOST_CHECK_EQUAL(report.orphans, 0);
    }
    {
        AuditRocksDb corrupt(base + "-corrupt", keep);
        RocksDbStateStorage storage(*corrupt.db, bcos::storage2::rocksdb::StateKeyResolver{},
            bcos::storage2::rocksdb::StateValueResolver{});
        landNodeRowsOnFlat(nodes, storage);
        writeChainMetadata(storage, kSmokeTipBlock, stateRoot);

        auto const childRaw =
            readNodeRow(storage, PathKey{.scope = TrieScope::account(), .position = childPosition});
        BOOST_REQUIRE(childRaw.has_value());
        auto const rootRaw = readNodeRow(storage, accountRootPathKey());
        BOOST_REQUIRE(rootRaw.has_value());
        auto const offset = findDigest(*rootRaw, digestOf(*childRaw));
        BOOST_REQUIRE_NE(offset, kDigestNotFound);
        writeNodeRow(storage, accountRootPathKey(), flipByte(*rootRaw, offset));

        BOOST_CHECK_THROW(runPathTreeAudit(storage), MPTInvariantViolation);
    }
    {
        // Node rows that are perfectly consistent with each other, under a header that commits to
        // a different root. Only the header can tell.
        AuditRocksDb wrongRoot(base + "-wrongroot", keep);
        RocksDbStateStorage storage(*wrongRoot.db, bcos::storage2::rocksdb::StateKeyResolver{},
            bcos::storage2::rocksdb::StateValueResolver{});
        landNodeRowsOnFlat(nodes, storage);
        writeChainMetadata(storage, kSmokeTipBlock, mpt::test::makeHash(0xC0));

        BOOST_CHECK_NO_THROW(runPathTreeAudit(storage));
        BOOST_CHECK_THROW(
            runPathTreeAudit(storage, mpt::test::makeHash(0xC0)), MPTInvariantViolation);
    }
    {
        // A store whose tip row cannot be read, carrying a header for block 0 that commits to some
        // OTHER root — the shape that made the CLI's `tip.value_or(0)` compare the tip's tree
        // against genesis and call a healthy store corrupted. With no tip and no --expect-root
        // there is nothing to compare against, and the audit must be asked to compare nothing
        // rather than to compare against block 0.
        AuditRocksDb noTip(base + "-notip", keep);
        RocksDbStateStorage storage(*noTip.db, bcos::storage2::rocksdb::StateKeyResolver{},
            bcos::storage2::rocksdb::StateValueResolver{});
        landNodeRowsOnFlat(nodes, storage);
        writeBlockHeader(storage, 0, mpt::test::makeHash(0xB0));  // no current_number row

        auto const unchecked = runPathTreeAudit(storage, std::nullopt);
        BOOST_CHECK(!unchecked.rootChecked);
        BOOST_CHECK_EQUAL(unchecked.accountRoot.hex(), stateRoot.hex());
        // What the old fallback did instead.
        BOOST_CHECK_THROW(
            runPathTreeAudit(storage, mpt::test::makeHash(0xB0)), MPTInvariantViolation);
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::audit::test
