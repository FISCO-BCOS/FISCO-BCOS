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
/** @file test_RocksDBStorage.cpp
 *  @author xingqiangbai
 *  @date 20180425
 */

#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/write_batch.h"
#include <libdevcore/FixedHash.h>
#include <libdevcore/Log.h>
#include <libstorage/BasicRocksDB.h>
#include <libstorage/RocksDBStorage.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace std;
using namespace dev::storage;
using namespace rocksdb;
namespace rocksdb
{
Status ReadRecordFromWriteBatch(Slice* input, char* tag, uint32_t* column_family, Slice* key,
    Slice* value, Slice* blob, Slice* xid);
}

namespace test_RocksDBStorage
{
struct MockWriteBatch : public rocksdb::WriteBatch
{
    Slice getOriginData()
    {
        Slice input(rep_);
        input.remove_prefix(12);
        return input;
    }
};

class MockRocksDB : public BasicRocksDB
{
public:
    MockRocksDB() {}
    virtual ~MockRocksDB() {}
    Status Write(WriteOptions const&, WriteBatch& updates) override
    {
        auto batch = reinterpret_cast<MockWriteBatch*>(&updates);
        size_t count = batch->Count();
        auto input = batch->getOriginData();
        char tag = 0;
        uint32_t column_family = 0;  // default
        for (size_t i = 0; i < count; ++i)
        {
            Slice key, value, blob, xid;
            auto s =
                ReadRecordFromWriteBatch(&input, &tag, &column_family, &key, &value, &blob, &xid);
            if (!s.ok())
            {
                break;
            }
            if (key == "e_Exception")
                return Status::InvalidArgument(Slice("InvalidArgument"));
            LOG(INFO) << "write key=" << key.ToString();
            db.insert(std::make_pair(key.ToString(), value.ToString()));
        }
        return Status::OK();
    }

    Status Get(ReadOptions const&, std::string const& key, std::string& value) override
    {
        if (key.empty() || key == "e_Exception")
            return Status::InvalidArgument(Slice("InvalidArgument"));
        auto it = db.find(key);
        if (it == db.end())
            return Status::NotFound(Slice("NotFound"));
        value = it->second;
        return Status::OK();
    }
#if 0
    virtual Status Delete(const WriteOptions&, ColumnFamilyHandle*, const Slice& key)
    {
        db.erase(key.ToString());
        return Status::OK();
    }
    
    Status SingleDelete(const WriteOptions&, ColumnFamilyHandle*, const Slice& key)
    {
        db.erase(key.ToString());
        return Status::OK();
    }
    Status Merge(const WriteOptions&, ColumnFamilyHandle*, const Slice&, const Slice&)
    {
        return Status::OK();
    }
        virtual Status Put(const WriteOptions&, ColumnFamilyHandle*, const Slice&, const Slice&)
    {
        return Status::OK();
    }
    
    Status Get(const ReadOptions&, ColumnFamilyHandle*, const Slice&, PinnableSlice*)
    {
        return Status::OK();
    }

    std::vector<Status> MultiGet(const ReadOptions&, const std::vector<ColumnFamilyHandle*>&,
        const std::vector<Slice>&, std::vector<std::string>*)
    {
        return std::vector<Status>();
    }
    Iterator* NewIterator(const ReadOptions&, ColumnFamilyHandle*) { return nullptr; }
    Status NewIterators(
        const ReadOptions&, const std::vector<ColumnFamilyHandle*>&, std::vector<Iterator*>*)
    {
        return Status::OK();
    }
    const Snapshot* GetSnapshot() { return nullptr; }
    void ReleaseSnapshot(const Snapshot*) {}
    bool GetProperty(ColumnFamilyHandle*, const Slice&, std::string*) { return true; }
    bool GetMapProperty(ColumnFamilyHandle*, const Slice&, std::map<std::string, std::string>*)
    {
        return true;
    }
    bool GetIntProperty(ColumnFamilyHandle*, const Slice&, uint64_t*) { return true; }
    bool GetAggregatedIntProperty(const Slice&, uint64_t*) { return true; }
    void GetApproximateSizes(
        ColumnFamilyHandle*, const Range*, int, uint64_t*, uint8_t = INCLUDE_FILES)
    {}
    void GetApproximateMemTableStats(
        ColumnFamilyHandle*, const Range&, uint64_t* const, uint64_t* const)
    {}
    Status CompactRange(const CompactRangeOptions&, ColumnFamilyHandle*, const Slice*, const Slice*)
    {
        return Status::OK();
    }
    Status SetDBOptions(const std::unordered_map<std::string, std::string>&)
    {
        return Status::OK();
    }
    Status CompactFiles(const CompactionOptions&, ColumnFamilyHandle*,
        const std::vector<std::string>&, const int, const int = -1,
        std::vector<std::string>* const = nullptr, CompactionJobInfo* = nullptr)
    {
        return Status::OK();
    }
    Status PauseBackgroundWork() { return Status::OK(); }
    Status ContinueBackgroundWork() { return Status::OK(); }
    Status EnableAutoCompaction(const std::vector<ColumnFamilyHandle*>&) { return Status::OK(); }
    int NumberLevels(ColumnFamilyHandle*) { return 0; }
    int MaxMemCompactionLevel(ColumnFamilyHandle*) { return 0; }
    int Level0StopWriteTrigger(ColumnFamilyHandle*) { return 0; }
    const std::string& GetName() const
    {
        static string s("haed");
        return s;
    }
    Env* GetEnv() const { return nullptr; }
    Options GetOptions(ColumnFamilyHandle*) const { return Options(); }
    DBOptions GetDBOptions() const { return DBOptions(); }
    Status Flush(const FlushOptions&, ColumnFamilyHandle*) { return Status::OK(); }
    Status Flush(const FlushOptions&, const std::vector<ColumnFamilyHandle*>&)
    {
        return Status::OK();
    }
    Status SyncWAL() { return Status::OK(); }
    SequenceNumber GetLatestSequenceNumber() const { return 0; }
    bool SetPreserveDeletesSequenceNumber(SequenceNumber) { return true; }
    Status DisableFileDeletions() { return Status::OK(); }
    Status EnableFileDeletions(bool = true) { return Status::OK(); }
    Status GetLiveFiles(std::vector<std::string>&, uint64_t*, bool = true) { return Status::OK(); }
    Status GetSortedWalFiles(VectorLogPtr&) { return Status::OK(); }
    Status GetUpdatesSince(SequenceNumber, std::unique_ptr<TransactionLogIterator>*,
        const TransactionLogIterator::ReadOptions& = TransactionLogIterator::ReadOptions())
    {
        return Status::OK();
    }
    Status DeleteFile(std::string) { return Status::OK(); }
    Status IngestExternalFile(
        ColumnFamilyHandle*, const std::vector<std::string>&, const IngestExternalFileOptions&)
    {
        return Status::OK();
    }
    Status IngestExternalFiles(const std::vector<IngestExternalFileArg>&) { return Status::OK(); }
    Status VerifyChecksum() { return Status::OK(); }
    Status GetDbIdentity(std::string&) const { return Status::OK(); }
    ColumnFamilyHandle* DefaultColumnFamily() const { return nullptr; }
    Status GetPropertiesOfAllTables(ColumnFamilyHandle*, TablePropertiesCollection*)
    {
        return Status::OK();
    }
    Status GetPropertiesOfTablesInRange(
        ColumnFamilyHandle*, const Range*, std::size_t, TablePropertiesCollection*)
    {
        return Status::OK();
    }
#endif
private:
    // No copying allowed
    MockRocksDB(const MockRocksDB&) = delete;
    void operator=(const MockRocksDB&) = delete;
    std::map<std::string, std::string> db;
};

struct RocksDBFixture
{
    RocksDBFixture()
    {
        rocksDB = std::make_shared<dev::storage::RocksDBStorage>();
        std::shared_ptr<MockRocksDB> mockRocksDB = std::make_shared<MockRocksDB>();
        rocksDB->setDB(mockRocksDB);
    }
    Entries::Ptr getEntries()
    {
        Entries::Ptr entries = std::make_shared<Entries>();
        Entry::Ptr entry = std::make_shared<Entry>();
        entry->setField("Name", "LiSi");
        entry->setField("id", "1");
        entries->addEntry(entry);
        return entries;
    }
    dev::storage::RocksDBStorage::Ptr rocksDB;
};

BOOST_FIXTURE_TEST_SUITE(RocksDB, RocksDBFixture)

BOOST_AUTO_TEST_CASE(empty_select)
{
    h256 h(0x01);
    int num = 1;
    std::string table("t_test");
    std::string key("id");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    Entries::Ptr entries = rocksDB->select(num, tableInfo, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 0u);
}

BOOST_AUTO_TEST_CASE(commitNew)
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
    size_t c = rocksDB->commit(num, datas);
    BOOST_CHECK_EQUAL(c, 1u);
    std::string table("t_test");
    std::string key("LiSi");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    entries = rocksDB->select(num, tableInfo, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 1u);
}

BOOST_AUTO_TEST_CASE(commitDirty)
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
    tableData->dirtyEntries = entries;
    datas.push_back(tableData);
    size_t c = rocksDB->commit(num, datas);
    BOOST_CHECK_EQUAL(c, 1u);
    std::string table("t_test");
    std::string key("LiSi");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    entries = rocksDB->select(num, tableInfo, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 1u);
}

BOOST_AUTO_TEST_CASE(exception)
{
    h256 h(0x01);
    int num = 1;
    h256 blockHash(0x11231);
    std::vector<dev::storage::TableData::Ptr> datas;
    dev::storage::TableData::Ptr tableData = std::make_shared<dev::storage::TableData>();
    tableData->info->name = "e";
    tableData->info->key = "Name";
    tableData->info->fields.push_back("id");
    Entries::Ptr entries = getEntries();
    entries->get(0)->setField("Name", "Exception");
    tableData->newEntries = entries;
    datas.push_back(tableData);
    BOOST_CHECK_THROW(rocksDB->commit(num, datas), boost::exception);
    std::string table("e");
    std::string key("Exception");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    BOOST_CHECK_THROW(
        rocksDB->select(num, tableInfo, key, std::make_shared<Condition>()), boost::exception);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_RocksDBStorage
