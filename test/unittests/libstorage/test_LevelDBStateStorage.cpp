// "Copyright [2018] <fisco-dev>"
#include "libstorage/LevelDBStorage.h"
#include <leveldb/db.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::storage;
using namespace leveldb;

namespace leveldb
{
class Value;
};

namespace test_LevelDBStateStorage
{
inline uint32_t DecodeFixed32(const char* ptr, bool LittleEndian = true)
{
    if (LittleEndian)
    {
        // Load the raw bytes
        uint32_t result;
        memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
        return result;
    }
    return ((static_cast<uint32_t>(static_cast<unsigned char>(ptr[0]))) |
            (static_cast<uint32_t>(static_cast<unsigned char>(ptr[1])) << 8) |
            (static_cast<uint32_t>(static_cast<unsigned char>(ptr[2])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(ptr[3])) << 24));
}

const char* GetVarint32PtrFallback(const char* p, const char* limit, uint32_t* value)
{
    uint32_t result = 0;
    for (uint32_t shift = 0; shift <= 28 && p < limit; shift += 7)
    {
        uint32_t byte = *(reinterpret_cast<const unsigned char*>(p));
        p++;
        if (byte & 128)
        {
            // More bytes are present
            result |= ((byte & 127) << shift);
        }
        else
        {
            result |= (byte << shift);
            *value = result;
            return reinterpret_cast<const char*>(p);
        }
    }
    return nullptr;
}

inline const char* GetVarint32Ptr(const char* p, const char* limit, uint32_t* value)
{
    if (p < limit)
    {
        uint32_t result = *(reinterpret_cast<const unsigned char*>(p));
        if ((result & 128) == 0)
        {
            *value = result;
            return p + 1;
        }
    }
    return GetVarint32PtrFallback(p, limit, value);
}

bool GetVarint32(Slice* input, uint32_t* value)
{
    const char* p = input->data();
    const char* limit = p + input->size();
    const char* q = GetVarint32Ptr(p, limit, value);
    if (q == nullptr)
    {
        return false;
    }
    else
    {
        *input = Slice(q, limit - q);
        return true;
    }
}

bool GetLengthPrefixedSlice(Slice* input, Slice* result)
{
    uint32_t len;
    if (GetVarint32(input, &len) && input->size() >= len)
    {
        *result = Slice(input->data(), len);
        input->remove_prefix(len);
        return true;
    }
    else
    {
        return false;
    }
}

struct MockWriteBatch
{
    std::string rep_;
    int Count() { return DecodeFixed32(rep_.data() + 8); }
};

class MockLevelDB : public leveldb::DB
{
public:
    MockLevelDB() {}
    virtual ~MockLevelDB() {}

    virtual Status Put(const WriteOptions& options, const Slice& key, const Slice& value)
    {
        return Status::OK();
    }

    virtual Status Delete(const WriteOptions& options, const Slice& key)
    {
        db.erase(key.ToString());
        return Status::OK();
    }

    virtual Status Write(const WriteOptions& options, WriteBatch* updates)
    {
        if (updates == nullptr)
            return Status::InvalidArgument(Slice("InvalidArgument"));
        auto batch = reinterpret_cast<MockWriteBatch*>(updates);
        size_t count = batch->Count();
        Slice input(batch->rep_);
        input.remove_prefix(12);
        for (size_t i = 0; i < count; ++i)
        {
            Slice key, value;
            input.remove_prefix(1);
            GetLengthPrefixedSlice(&input, &key);
            GetLengthPrefixedSlice(&input, &value);
            if (key.ToString() == "e_Exception")
                return Status::InvalidArgument(Slice("InvalidArgument"));
            db.insert(std::make_pair(key.ToString(), value.ToString()));
        }
        return Status::OK();
    }

    virtual Status Get(const ReadOptions& options, const Slice& key, std::string* value)
    {
        if (value == nullptr || key.empty() || key == "e_Exception")
            return Status::InvalidArgument(Slice("InvalidArgument"));
        auto it = db.find(key.ToString());
        if (it == db.end())
            return Status::NotFound(Slice("NotFound"));
        *value = it->second;
        return Status::OK();
    }

    virtual Iterator* NewIterator(const ReadOptions& options) { return nullptr; }

    virtual const Snapshot* GetSnapshot() { return nullptr; }

    virtual void ReleaseSnapshot(const Snapshot* snapshot) {}

    virtual bool GetProperty(const Slice& property, std::string* value) { return true; }

    virtual void GetApproximateSizes(const Range* range, int n, uint64_t* sizes) {}

    virtual void CompactRange(const Slice* begin, const Slice* end) {}

    virtual Status Get(const leveldb::ReadOptions&, const leveldb::Slice&, leveldb::Value*)
    {
        return Status::OK();
    };
    virtual void SuspendCompactions(){};
    virtual void ResumeCompactions(){};

private:
    // No copying allowed
    MockLevelDB(const MockLevelDB&) = delete;
    void operator=(const MockLevelDB&) = delete;
    std::map<std::string, std::string> db;
};

struct LevelDBFixture
{
    LevelDBFixture()
    {
        levelDB = std::make_shared<dev::storage::LevelDBStorage>();
        std::shared_ptr<MockLevelDB> mockLevelDB = std::make_shared<MockLevelDB>();
        levelDB->setDB(mockLevelDB);
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
    dev::storage::LevelDBStorage::Ptr levelDB;
};

BOOST_FIXTURE_TEST_SUITE(LevelDB, LevelDBFixture);

BOOST_AUTO_TEST_CASE(info)
{
    std::string tableName = "test";
    TableInfo::Ptr tableInfo = levelDB->info(tableName);
    BOOST_CHECK_EQUAL(tableInfo->name, tableName);
}

BOOST_AUTO_TEST_CASE(onlyDirty)
{
    BOOST_CHECK_EQUAL(levelDB->onlyDirty(), false);
}

BOOST_AUTO_TEST_CASE(empty_select)
{
    h256 h(0x01);
    int num = 1;
    std::string table("t_test");
    std::string key("id");
    Entries::Ptr entries = levelDB->select(h, num, table, key);
    BOOST_CHECK_EQUAL(entries->size(), 0u);
}

BOOST_AUTO_TEST_CASE(commit)
{
    h256 h(0x01);
    int num = 1;
    h256 blockHash(0x11231);
    std::vector<dev::storage::TableData::Ptr> datas;
    dev::storage::TableData::Ptr tableData = std::make_shared<dev::storage::TableData>();
    tableData->tableName = "t_test";
    Entries::Ptr entries = getEntries();
    tableData->data.insert(std::make_pair(std::string("LiSi"), entries));
    if (!tableData->data.empty())
    {
        datas.push_back(tableData);
    }
    size_t c = levelDB->commit(h, num, datas, blockHash);
    BOOST_CHECK_EQUAL(c, 1u);
    std::string table("t_test");
    std::string key("LiSi");
    entries = levelDB->select(h, num, table, key);
    BOOST_CHECK_EQUAL(entries->size(), 1u);
}

BOOST_AUTO_TEST_CASE(exception)
{
    h256 h(0x01);
    int num = 1;
    h256 blockHash(0x11231);
    std::vector<dev::storage::TableData::Ptr> datas;
    dev::storage::TableData::Ptr tableData = std::make_shared<dev::storage::TableData>();
    tableData->tableName = "e";
    Entries::Ptr entries = getEntries();
    tableData->data.insert(std::make_pair(std::string("Exception"), entries));
    datas.push_back(tableData);
    BOOST_CHECK_THROW(levelDB->commit(h, num, datas, blockHash), boost::exception);
    std::string table("e");
    std::string key("Exception");

    BOOST_CHECK_THROW(levelDB->select(h, num, table, key), boost::exception);
}

BOOST_AUTO_TEST_SUITE_END();

}  // namespace test_LevelDBStateStorage
