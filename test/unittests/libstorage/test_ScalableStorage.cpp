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
 * (c) 2016-2018 fisco-dev contributors.
 */

#include "MemoryStorage2.h"
#include "libstorage/ScalableStorage.h"
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::storage;

namespace test_ScalableStorage
{
struct StorageFixture
{
    StorageFixture()
    {
        scalableStorage = std::make_shared<ScalableStorage>(2);
        memStorage = std::make_shared<MemoryStorage2>();
        storageFactory = std::make_shared<MemoryStorageFactory>();
        auto storage = storageFactory->getStorage("0");
        scalableStorage->setStateStorage(memStorage);
        scalableStorage->setArchiveStorage(storage, 0);
        scalableStorage->setRemoteStorage(memStorage);
        scalableStorage->setStorageFactory(storageFactory);
    }
    Entries::Ptr getEntries()
    {
        Entries::Ptr entries = std::make_shared<Entries>();
        Entry::Ptr entry = std::make_shared<Entry>();
        entry->setField("Name", "LiSi");
        entry->setField("id", "2");
        entries->addEntry(entry);
        return entries;
    }
    std::shared_ptr<ScalableStorage> scalableStorage;
    std::shared_ptr<MemoryStorage2> memStorage;
    std::shared_ptr<MemoryStorageFactory> storageFactory;
};

BOOST_FIXTURE_TEST_SUITE(TestScalableStorage, StorageFixture)

BOOST_AUTO_TEST_CASE(empty_select)
{
    h256 h(0x01);
    int num = 1;
    std::string table("t_test");
    std::string key("id");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    Entries::Ptr entries =
        scalableStorage->select(num, tableInfo, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 0u);
}

BOOST_AUTO_TEST_CASE(commit)
{
    h256 h(0x01);
    int num = 1;
    h256 blockHash(0x11231);
    std::vector<dev::storage::TableData::Ptr> datas;
    dev::storage::TableData::Ptr tableData = std::make_shared<dev::storage::TableData>();
    tableData->info->name = "t_test";
    tableData->info->key = "Name";
    tableData->info->fields.push_back("id");
    Entries::Ptr entries = getEntries();
    tableData->newEntries = entries;
    datas.push_back(tableData);
    size_t c = scalableStorage->commit(num, datas);
    BOOST_CHECK_EQUAL(c, 2u);
    std::string table("t_test");
    std::string key("LiSi");
    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    entries = scalableStorage->select(num, tableInfo, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 1u);
    scalableStorage->stop();
}

BOOST_AUTO_TEST_CASE(archiveData)
{
    auto storage = storageFactory->getStorage("0");
    scalableStorage->setStateStorage(memStorage);
    scalableStorage->setArchiveStorage(storage, 0);
    scalableStorage->setRemoteStorage(memStorage);
    scalableStorage->setStorageFactory(storageFactory);
    h256 h(0x01);
    int num = 1;
    std::vector<dev::storage::TableData::Ptr> datas;
    dev::storage::TableData::Ptr tableData = std::make_shared<dev::storage::TableData>();
    tableData->info->name = SYS_HASH_2_BLOCK;
    tableData->info->key = "Name";
    tableData->info->fields.push_back("id");
    Entries::Ptr entries = getEntries();
    tableData->newEntries = entries;
    datas.push_back(tableData);
    size_t c = scalableStorage->commit(num, datas);
    BOOST_CHECK_EQUAL(c, 2u);
    std::string key("LiSi");
    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = SYS_HASH_2_BLOCK;
    tableInfo->key = "Name";
    entries = scalableStorage->select(num, tableInfo, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 1u);
    // scalableStorage->setRemoteBlockNumber(10);
    // entries = scalableStorage->select(num, tableInfo, key, std::make_shared<Condition>());
    // BOOST_CHECK_EQUAL(entries->size(), 0);
    scalableStorage->stop();
}

BOOST_AUTO_TEST_CASE(remoteNumber)
{
    scalableStorage->setRemoteBlockNumber(10);
    BOOST_CHECK_EQUAL(scalableStorage->getRemoteBlockNumber(), 10);
    scalableStorage->setRemoteBlockNumber(5);
    BOOST_CHECK_EQUAL(scalableStorage->getRemoteBlockNumber(), 10);
    scalableStorage->stop();
}

BOOST_AUTO_TEST_CASE(exception)
{
    scalableStorage->setRemoteStorage(nullptr);
    int num = 1;
    std::vector<dev::storage::TableData::Ptr> datas;
    dev::storage::TableData::Ptr tableData = std::make_shared<dev::storage::TableData>();
    tableData->info->name = "e";
    tableData->info->key = "Name";
    tableData->info->fields.push_back("id");
    Entries::Ptr entries = getEntries();
    entries->get(0)->setField("Name", "Exception");
    tableData->newEntries = entries;
    datas.push_back(tableData);
    scalableStorage->commit(num, datas);
}

BOOST_AUTO_TEST_SUITE_END()


}  // namespace test_ScalableStorage
