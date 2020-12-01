/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2019 fisco-dev contributors.
 */
/** @file test_BasicRocksDB.cpp
 *  @author yujiechen
 *  @date 2010-06-26
 */
#include "libdevcrypto/AES.h"
#include <libconfig/GlobalConfigure.h>
#include <libledger/DBInitializer.h>
#include <libledger/LedgerParam.h>
#include <libstorage/BasicRocksDB.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::crypto;
using namespace bcos::storage;
using namespace rocksdb;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(TestRocksDB, TestOutputHelperFixture)

void openTable(std::shared_ptr<BasicRocksDB> basicRocksDB, std::string const& dbName)
{
    rocksdb::Options options;
    options.IncreaseParallelism(std::max(1, (int)std::thread::hardware_concurrency()));
    options.OptimizeLevelStyleCompaction();
    options.create_if_missing = true;
    options.max_open_files = 1000;
    options.compression = rocksdb::kSnappyCompression;
    // open rocksDB with default option
    basicRocksDB->Open(options, dbName);
}

// test get value
void testReGetValue(std::shared_ptr<BasicRocksDB> basicRocksDB, std::string const& keyPrefix,
    std::string const& valuePrefix, std::string const& dbName, size_t const& succNum,
    bool needOpen = true)
{
    LOG(DEBUG) << LOG_DESC("testReGetValue");
    // open DB
    if (needOpen)
    {
        openTable(basicRocksDB, dbName);
    }
    std::string value;
    for (size_t i = 0; i < succNum; i++)
    {
        std::string key = keyPrefix + std::to_string(i);
        auto dbStatus = basicRocksDB->Get(rocksdb::ReadOptions(), key, value);
        BOOST_CHECK(dbStatus.ok());
        BOOST_CHECK(value == valuePrefix + std::to_string(i));
    }
}

std::shared_ptr<BasicRocksDB> testBasicOperation(std::shared_ptr<BasicRocksDB> basicRocksDB,
    std::string const& dbName, std::string const& keyPrefix, std::string const& valuePrefix,
    size_t& succNum)
{
    // open table
    openTable(basicRocksDB, dbName);
    std::string key = keyPrefix;
    std::string value = valuePrefix;
    ROCKSDB_LOG(DEBUG) << LOG_DESC("* Check get non-exist key from rocksDB");
    // get a non-exist key
    auto dbStatus = basicRocksDB->Get(rocksdb::ReadOptions(), key, value);
    BOOST_CHECK(dbStatus.IsNotFound() == true);
    BOOST_CHECK(value == "");

    ROCKSDB_LOG(DEBUG) << LOG_DESC("* Check Write and Get value from DB");
    // put the key value into batch
    rocksdb::WriteBatch batch;
    succNum = 0;
    for (size_t i = 0; i < 10; i++)
    {
        value = valuePrefix + std::to_string(succNum);
        key = keyPrefix + std::to_string(succNum);
        dbStatus = basicRocksDB->Put(batch, key, value);
        if (dbStatus.ok())
        {
            succNum++;
            BOOST_CHECK((size_t)batch.Count() == succNum);
        }
    }
    // write batch into DB
    basicRocksDB->Write(rocksdb::WriteOptions(), batch);
    // check Get
    testReGetValue(basicRocksDB, keyPrefix, valuePrefix, dbName, succNum, false);
    return basicRocksDB;
}

void testAllDBOperation(
    std::string const& dbName, std::string const& keyPrefix, std::string const& valuePrefix)
{
    size_t succNum;
    // open table succ
    std::shared_ptr<BasicRocksDB> basicRocksDB = std::make_shared<BasicRocksDB>();
    auto handler = testBasicOperation(basicRocksDB, dbName, keyPrefix, valuePrefix, succNum);
    if (handler)
    {
        BOOST_CHECK(succNum > 0);
        // test reopen table
        basicRocksDB->closeDB();
        testReGetValue(basicRocksDB, keyPrefix, valuePrefix, dbName, succNum, true);
    }
}

// test write and read value from rocksDB
BOOST_AUTO_TEST_CASE(testWriteAndReadValue)
{
    std::string dbName = "test_rocksDB";

    std::string keyPrefix = "test_key";
    std::string valuePrefix = "test_value";
    testAllDBOperation(dbName, keyPrefix, valuePrefix);
    boost::filesystem::remove_all(dbName);
}

// test dbOperation with hook handler(encryption and decryption)
BOOST_AUTO_TEST_CASE(testWithEncryptDecryptHandler)
{
    // fake param
    std::string dbName = "tmp/testRocksDB";
    // set datakey
    g_BCOSConfig.diskEncryption.dataKey = asString(
        *fromHexString("3031323334353637303132333435363730313233343536373031323334353637"));
    rocksdb::Options options;
    options.create_if_missing = true;
    std::shared_ptr<BasicRocksDB> basicRocksDB = std::make_shared<BasicRocksDB>();
    // basicRocksDB->Open(options, dbName);

    basicRocksDB->setEncryptHandler([=](std::string const& data, std::string& encData) {
        encData = aesCBCEncrypt(data, g_BCOSConfig.diskEncryption.dataKey);
    });

    basicRocksDB->setDecryptHandler([=](std::string& data) {
        data = aesCBCDecrypt(data, g_BCOSConfig.diskEncryption.dataKey);
    });

    BOOST_CHECK(basicRocksDB != nullptr);

    // test db operation
    std::string keyPrefix = "test_encKey";
    std::string valuePrefix = "value_encValue";
    size_t succNum = 0;
    auto handler = testBasicOperation(basicRocksDB, dbName, keyPrefix, valuePrefix, succNum);
    if (handler)
    {
        BOOST_CHECK(succNum > 0);
        // test reopen table
        basicRocksDB->closeDB();
        testReGetValue(basicRocksDB, keyPrefix, valuePrefix, dbName, succNum, true);
    }
    boost::filesystem::remove_all(dbName);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
