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
 * @file HalfCommitCheckerTest.cpp
 * @brief spec §7.2 half-commit detection: scan over a DB crafted like a
 *        crash between block-data store and state commit (spec §5.12)
 */
#include "bcos-ledger/halfcommit/HalfCommitChecker.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Common.h"
#include "bcos-framework/testutils/faker/FakeBlock.h"
#include <rocksdb/db.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <sstream>

using namespace bcos;
using namespace bcos::ledger;

namespace
{

struct HalfCommitCheckerFixture
{
    HalfCommitCheckerFixture()
    {
        m_blockFactory = bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
        m_path = std::filesystem::temp_directory_path() /
                 (std::string("bcos_halfcommit_test_") +
                     boost::unit_test::framework::current_test_case().p_name.get());
        std::filesystem::remove_all(m_path);

        ::rocksdb::Options options;
        options.create_if_missing = true;
        ::rocksdb::DB* db = nullptr;
        auto status = ::rocksdb::DB::Open(options, m_path.string(), &db);
        BOOST_REQUIRE_MESSAGE(status.ok(), status.ToString());
        m_db.reset(db);
    }
    ~HalfCommitCheckerFixture()
    {
        m_db.reset();
        std::filesystem::remove_all(m_path);
    }

    void put(std::string_view table, std::string_view key, std::string_view value)
    {
        auto status = m_db->Put(::rocksdb::WriteOptions{}, storage::toDBKey(table, key),
            ::rocksdb::Slice(value.data(), value.size()));
        BOOST_REQUIRE(status.ok());
    }

    void writeCurrentNumber(protocol::BlockNumber number)
    {
        // mirrors Ledger::asyncPrewriteBlock "current number":
        // ASCII decimal via boost::lexical_cast<std::string>
        put(SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER, std::to_string(number));
    }

    // mirrors Ledger::asyncPrewriteBlock "number 2 transactions": a
    // BlockFactory-encoded block carrying transaction metadata (hash + to) only
    void writeTxsRow(protocol::BlockNumber number, std::vector<crypto::HashType> const& txHashes)
    {
        auto block = m_blockFactory->createBlock();
        for (auto const& hash : txHashes)
        {
            block->appendTransactionMetaData(m_blockFactory->createTransactionMetaData(
                hash, "0x0000000000000000000000000000000000001001"));
        }
        bytes buffer;
        block->encode(buffer);
        put(SYS_NUMBER_2_TXS, std::to_string(number),
            std::string_view(reinterpret_cast<const char*>(buffer.data()), buffer.size()));
    }

    // mirrors Ledger::asyncPrewriteBlock "number 2 nonce": a BlockFactory-encoded
    // block carrying the nonce list only
    void writeNoncesRow(protocol::BlockNumber number, size_t nonceCount)
    {
        auto block = m_blockFactory->createBlock();
        std::vector<std::string> nonces;
        nonces.reserve(nonceCount);
        for (size_t i = 0; i < nonceCount; ++i)
        {
            nonces.push_back(std::to_string(10000 + i));
        }
        block->setNonceList(::ranges::any_view<std::string>(nonces));
        bytes buffer;
        block->encode(buffer);
        put(SYS_BLOCK_NUMBER_2_NONCES, std::to_string(number),
            std::string_view(reinterpret_cast<const char*>(buffer.data()), buffer.size()));
    }

    // a fully committed block: both index rows present and current_number covers it
    void writeCommittedBlock(
        protocol::BlockNumber number, std::vector<crypto::HashType> const& txHashes)
    {
        writeTxsRow(number, txHashes);
        writeNoncesRow(number, txHashes.size());
    }

    std::vector<crypto::HashType> makeHashes(size_t count, unsigned seed)
    {
        std::vector<crypto::HashType> hashes;
        hashes.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            hashes.emplace_back(crypto::HashType(seed * 1000 + i));
        }
        return hashes;
    }

    protocol::BlockFactory::Ptr m_blockFactory;
    std::filesystem::path m_path;
    std::unique_ptr<::rocksdb::DB> m_db;
};

}  // namespace

BOOST_FIXTURE_TEST_SUITE(HalfCommitCheckerSuite, HalfCommitCheckerFixture)

BOOST_AUTO_TEST_CASE(HealthyDBReportsNoOrphan)
{
    writeCurrentNumber(100);
    writeCommittedBlock(99, makeHashes(2, 99));
    writeCommittedBlock(100, makeHashes(3, 100));

    auto report = halfcommit::scan(*m_db, *m_blockFactory);

    BOOST_CHECK_EQUAL(report.currentNumber, 100);
    BOOST_CHECK(report.orphans.empty());
    BOOST_CHECK_EQUAL(report.dbCorrupted, false);

    auto text = halfcommit::formatReport(report);
    BOOST_CHECK(text.find("currentNumber: 100") != std::string::npos);
    BOOST_CHECK(text.find("orphans: 0") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(CrashAfterBlockDataStoreReportsOrphans)
{
    // crash simulation: blocks 101..103 got their tx/nonce index rows written,
    // but the state commit never advanced current_number past 100
    writeCurrentNumber(100);
    writeCommittedBlock(100, makeHashes(2, 100));
    auto orphanHashes101 = makeHashes(2, 101);
    auto orphanHashes102 = makeHashes(0, 102);  // empty block
    auto orphanHashes103 = makeHashes(3, 103);
    writeCommittedBlock(101, orphanHashes101);
    writeCommittedBlock(102, orphanHashes102);
    writeCommittedBlock(103, orphanHashes103);

    auto report = halfcommit::scan(*m_db, *m_blockFactory);

    BOOST_CHECK_EQUAL(report.dbCorrupted, false);
    BOOST_CHECK_EQUAL(report.currentNumber, 100);
    BOOST_REQUIRE_EQUAL(report.orphans.size(), 3);
    BOOST_CHECK_EQUAL(report.orphans[0].blockNumber, 101);
    BOOST_CHECK_EQUAL(report.orphans[1].blockNumber, 102);
    BOOST_CHECK_EQUAL(report.orphans[2].blockNumber, 103);
    BOOST_CHECK(report.orphans[0].txHashes == orphanHashes101);
    BOOST_CHECK(report.orphans[1].txHashes.empty());
    BOOST_CHECK(report.orphans[2].txHashes == orphanHashes103);

    auto text = halfcommit::formatReport(report);
    BOOST_CHECK(text.find("orphans: 3") != std::string::npos);
    BOOST_CHECK(text.find("block 101, tx_count=2") != std::string::npos);
    BOOST_CHECK(text.find("block 103, tx_count=3") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(IndexMismatchReportsCorruption)
{
    // txs row for 101 exists but the nonces row does not — the two indexes are
    // written together, so this is not a plain half-commit but corruption
    writeCurrentNumber(100);
    writeTxsRow(101, makeHashes(2, 101));

    auto report = halfcommit::scan(*m_db, *m_blockFactory);

    BOOST_CHECK_EQUAL(report.dbCorrupted, true);
    BOOST_CHECK(report.corruptionReason.find("s_number_2_txs") != std::string::npos);
    BOOST_CHECK(report.corruptionReason.find("s_block_number_2_nonces") != std::string::npos);
    BOOST_CHECK(report.orphans.empty());

    auto text = halfcommit::formatReport(report);
    BOOST_CHECK(text.find("CORRUPTION") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(NonceRowWithoutTxsRowAlsoReportsCorruption)
{
    writeCurrentNumber(100);
    writeNoncesRow(101, 2);

    auto report = halfcommit::scan(*m_db, *m_blockFactory);
    BOOST_CHECK_EQUAL(report.dbCorrupted, true);
}

BOOST_AUTO_TEST_CASE(MissingCurrentNumberReportsCorruption)
{
    auto report = halfcommit::scan(*m_db, *m_blockFactory);

    BOOST_CHECK_EQUAL(report.dbCorrupted, true);
    BOOST_CHECK(report.corruptionReason.find("current_number") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(UndecodableTxsRowReportsCorruption)
{
    writeCurrentNumber(100);
    put(SYS_NUMBER_2_TXS, "101", "definitely-not-a-tars-encoded-block");
    writeNoncesRow(101, 1);

    auto report = halfcommit::scan(*m_db, *m_blockFactory);

    BOOST_CHECK_EQUAL(report.dbCorrupted, true);
    BOOST_CHECK(report.corruptionReason.find("101") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(DumpOrphansWritesOneLinePerTxHash)
{
    writeCurrentNumber(100);
    auto orphanHashes = makeHashes(2, 101);
    writeCommittedBlock(101, orphanHashes);

    auto report = halfcommit::scan(*m_db, *m_blockFactory);
    BOOST_REQUIRE_EQUAL(report.orphans.size(), 1);

    std::ostringstream out;
    halfcommit::dumpOrphans(report, out);

    std::string expected =
        "101 " + orphanHashes[0].hexPrefixed() + "\n101 " + orphanHashes[1].hexPrefixed() + "\n";
    BOOST_CHECK_EQUAL(out.str(), expected);
}

BOOST_AUTO_TEST_SUITE_END()
