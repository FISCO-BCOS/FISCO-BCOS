#pragma once

#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-storage/RocksDBStorage2.h>
#include <bcos-storage/StateKVResolver.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::storage2::rocksdb;
using namespace bcos::transaction_executor;

struct TestRocksDBStorage2Fixture
{
    TestRocksDBStorage2Fixture()
    {
        constexpr static std::string_view path = "./rocksdbtestdb";

        ::rocksdb::Options options;
        options.create_if_missing = true;

        rocksdb::DB* db;
        rocksdb::Status s = rocksdb::DB::Open(options, std::string(path), &db);
        BOOST_CHECK_EQUAL(s.ok(), true);

        originRocksDB.reset(db);
    }

    std::unique_ptr<rocksdb::DB> originRocksDB;
};

BOOST_FIXTURE_TEST_SUITE(TestRocksDBStorage2, TestRocksDBStorage2Fixture)

BOOST_AUTO_TEST_CASE(readWrite)
{
    RocksDBStorage2<StateKey, StateValue, StateKeyResolver, Resolver<ValueType> ValueResolver>
        rocksDB(*originRocksDB);
}

BOOST_AUTO_TEST_SUITE_END()