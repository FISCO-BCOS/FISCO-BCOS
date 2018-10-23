#include "Common.h"
#include <libdevcore/easylog.h>
#include <libstorage/LevelDBStorage.h>
#include <libstorage/MemoryTable.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::storage;

namespace test_MemoryDB
{
class MockAMOPDB : public dev::storage::LevelDBStorage
{
public:
    virtual ~MockAMOPDB() {}

    TableInfo::Ptr info(const std::string& table) override
    {
        BOOST_TEST_TRUE(table == "t_test");

        TableInfo::Ptr info = std::make_shared<TableInfo>();
        // info->fields.push_back("姓名");
        // info->fields.push_back("资产号");
        info->key = "姓名";
        info->name = "t_test";

        return info;
    }

    Entries::Ptr select(
        h256 hash, int num, const std::string& table, const std::string& key) override
    {
        BOOST_TEST_TRUE(table == "t_test");
        if (num == -99999 && key == "Exception")
            throw std::invalid_argument(std::string("Invalid Parameters"));
        Entries::Ptr entries = std::make_shared<Entries>();
        return entries;
    }
};

class MockAMOPDBSelect : public MockAMOPDB
{
public:
    virtual ~MockAMOPDBSelect() {}

    Entries::Ptr select(
        h256 hash, int num, const std::string& table, const std::string& key) override
    {
        BOOST_TEST_TRUE(table == "t_test");

        Entries::Ptr entries = std::make_shared<Entries>();

        Entry::Ptr entry = std::make_shared<Entry>();
        entry->setField("姓名", "张三");
        entry->setField("资产号", "5");
        entry->setField("资产名", "slk200");
        entry->setStatus(Entry::Status::NORMAL);
        entries->addEntry(entry);

        entry = std::make_shared<Entry>();
        entry->setField("姓名", "张三");
        entry->setField("资产号", "6");
        entry->setField("资产名", "sls amg");
        entry->setStatus(Entry::Status::NORMAL);
        entries->addEntry(entry);

        return entries;
    }

    dev::storage::MemoryTable::Ptr memDB;
};

class MockAMOPDBCommit : public MockAMOPDBSelect
{
public:
    virtual ~MockAMOPDBCommit() {}

    size_t commit(
        h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas, h256 blockHash) override
    {
        BOOST_TEST_TRUE(blockHash == h256(0x12345));
        BOOST_TEST_TRUE(datas.size() == 2u);

        return 2;
    }
};

struct MemoryDBFixture
{
    MemoryDBFixture()
    {
        memDB = std::make_shared<dev::storage::MemoryTable>();

        std::shared_ptr<MockAMOPDB> mockAMOPDB = std::make_shared<MockAMOPDB>();

        memDB->setStateStorage(mockAMOPDB);
        memDB->init("t_test");
        memDB->setBlockHash(dev::h256(0x12345));
        memDB->setBlockNum(1000);

        TableInfo::Ptr info = std::make_shared<TableInfo>();
        // info->fields.emplace_back("姓名");
        // info->fields.emplace_back("资产号");
        // info->fields.emplace_back("资产名");
        // info->fields.emplace_back("Exception");
        // info->fields.emplace_back("_status_");
        info->key = "姓名";
        info->name = "t_test";
        // memDB->setTableInfo(info);
    }
    bool is_critical(std::exception const& e) { return true; }
    ~MemoryDBFixture() {}

    void initData()
    {
        Entry::Ptr entry = memDB->newEntry();
        entry->setField("姓名", "张三");
        entry->setField("资产号", "0");
        entry->setField("资产名", "CLA220");
        entry->setStatus(Entry::Status::NORMAL);
        memDB->insert("张三", entry);

        entry = memDB->newEntry();
        entry->setField("姓名", "张三");
        entry->setField("资产号", "1");
        entry->setField("资产名", "A45");
        entry->setStatus(Entry::Status::NORMAL);
        memDB->insert("张三", entry);

        entry = memDB->newEntry();
        entry->setField("姓名", "张三");
        entry->setField("资产号", "2");
        entry->setField("资产名", "SLS AMG");
        entry->setStatus(Entry::Status::NORMAL);
        memDB->insert("张三", entry);

        entry = memDB->newEntry();
        entry->setField("姓名", "张三");
        entry->setField("资产号", "3");
        entry->setField("资产名", "M3");
        entry->setStatus(Entry::Status::NORMAL);
        memDB->insert("张三", entry);

        entry = memDB->newEntry();
        entry->setField("姓名", "张三");
        entry->setField("资产号", "4");
        entry->setField("资产名", "M5");
        entry->setStatus(Entry::Status::NORMAL);
        memDB->insert("张三", entry);
    }

    dev::storage::MemoryTable::Ptr memDB;
};

BOOST_FIXTURE_TEST_SUITE(MemoryDB, MemoryDBFixture)

BOOST_AUTO_TEST_CASE(empty_select)
{
    //空查询
    Condition::Ptr condition = memDB->newCondition();
    condition->EQ("姓名", "张三");
    condition->GE("资产号", "2");

    Entries::Ptr entries = memDB->select("张三", condition);
    BOOST_TEST_TRUE(entries->size() == 0u);
}

BOOST_AUTO_TEST_CASE(insert)
{
    initData();
}

BOOST_AUTO_TEST_CASE(empty_select2)
{
    //空查询
    Condition::Ptr condition = memDB->newCondition();
    condition->EQ("姓名", "李四");
    condition->GE("资产号", "2");

    Entries::Ptr entries = memDB->select("李四", condition);
    BOOST_TEST_TRUE(entries->size() == 0u);
    memDB->clear();
}

BOOST_AUTO_TEST_CASE(empty_remove)
{
    Condition::Ptr condition = memDB->newCondition();
    condition->EQ("姓名", "王五");
    size_t c = memDB->remove("王五", condition);
    BOOST_TEST_TRUE(c == 0u);
    memDB->clear();
}

BOOST_AUTO_TEST_CASE(empty_update)
{
    //空更新
    Entry::Ptr entry = memDB->newEntry();
    entry->setField("资产名", "保时捷911");

    Condition::Ptr condition = memDB->newCondition();
    condition->EQ("姓名", "王五");
    condition->GE("资产号", "2");

    size_t c = memDB->update("王五", entry, condition);
    BOOST_CHECK(c == 0u);
    memDB->clear();
}

BOOST_AUTO_TEST_CASE(condition_select)
{
    initData();

    std::shared_ptr<MockAMOPDBSelect> mockAMOPDB = std::make_shared<MockAMOPDBSelect>();

    memDB->setStateStorage(mockAMOPDB);
    memDB->init("t_test");

    mockAMOPDB->memDB = memDB;

    //常规查询
    Condition::Ptr condition = memDB->newCondition();
    condition->EQ("姓名", "张三");
    condition->GE("资产号", "2");
    Entries::Ptr entries = memDB->select("张三", condition);
    BOOST_TEST_TRUE(entries->size() == 3u);

    condition = memDB->newCondition();
    condition->EQ("姓名", "张三");
    condition->GT("资产号", "3");
    entries = memDB->select("张三", condition);
    BOOST_TEST_TRUE(entries->size() == 1u);

    condition = memDB->newCondition();
    condition->EQ("姓名", "张三");
    condition->LE("资产号", "2");
    entries = memDB->select("张三", condition);
    BOOST_TEST_TRUE(entries->size() == 3u);  // mock会无条件多返回2个

    condition = memDB->newCondition();
    condition->EQ("姓名", "张三");
    condition->LT("资产号", "2");
    entries = memDB->select("张三", condition);
    BOOST_TEST_TRUE(entries->size() == 2u);  // mock会无条件多返回2个

    condition = memDB->newCondition();
    condition->EQ("姓名", "张三");
    entries = memDB->select("张三", condition);
    BOOST_TEST_TRUE(entries->size() == 5u);

    condition = memDB->newCondition();
    condition->EQ("姓名", "张三");
    condition->NE("资产名", "M5");
    entries = memDB->select("张三", condition);
    BOOST_TEST_TRUE(entries->size() == 4u);
}

BOOST_AUTO_TEST_CASE(data_select)
{
    initData();

    Condition::Ptr condition = memDB->newCondition();
    condition->EQ("姓名", "张三");
    condition->EQ("资产号", "3");

    Entries::Ptr entries = memDB->select("张三", condition);
    BOOST_TEST_TRUE(entries->size() == 1u);

    Entry::Ptr entry = entries->get(0);
    BOOST_TEST_TRUE(entry->getField("姓名") == "张三");
    BOOST_TEST_TRUE(entry->getField("资产号") == "3");
    BOOST_TEST_TRUE(entry->getField("资产名") == "M3");
}

BOOST_AUTO_TEST_CASE(data_update)
{
    initData();

    Condition::Ptr condition = memDB->newCondition();
    condition->EQ("姓名", "张三");
    condition->EQ("资产号", "3");

    Entry::Ptr entry = memDB->newEntry();
    entry->setField("资产名", "保时捷911");

    size_t c = memDB->update("张三", entry, condition);
    BOOST_TEST_TRUE(c == 1u);

    Entries::Ptr entries = memDB->select("张三", condition);
    BOOST_TEST_TRUE(entries->size() == 1u);

    entry = entries->get(0);
    BOOST_TEST_TRUE(entry->getField("姓名") == "张三");
    BOOST_TEST_TRUE(entry->getField("资产号") == "3");
    BOOST_TEST_TRUE(entry->getField("资产名") == "保时捷911");
}

BOOST_AUTO_TEST_CASE(illegal_update)
{
    initData();

    Condition::Ptr condition = memDB->newCondition();
    condition->EQ("姓名", "张三");
    condition->EQ("资产号", "3");
    // failed update
    Entry::Ptr entry = memDB->newEntry();
    entry->setField("车型", "保时捷911");
    auto c = memDB->update("张三", entry, condition);
    BOOST_TEST_TRUE(c == 0u);
}

BOOST_AUTO_TEST_CASE(data_update_multi)
{
    initData();

    Condition::Ptr condition = memDB->newCondition();
    condition->EQ("姓名", "张三");
    condition->GT("资产号", "1");

    Entry::Ptr entry = memDB->newEntry();
    entry->setField("资产号", "100");

    size_t c = memDB->update("张三", entry, condition);
    BOOST_TEST_TRUE(c == 3u);
}

BOOST_AUTO_TEST_CASE(data_remove)
{
    initData();

    Condition::Ptr condition = memDB->newCondition();
    condition->EQ("姓名", "张三");
    condition->EQ("资产号", "3");

    size_t c = memDB->remove("张三", condition);
    BOOST_TEST_TRUE(c == 1u);
    Entries::Ptr entries = memDB->select("张三", condition);
    BOOST_TEST_TRUE(entries->size() == 0u);
}

BOOST_AUTO_TEST_CASE(cache_clear)
{
    memDB->clear();
    size_t c = memDB->data()->size();
    BOOST_TEST_TRUE(c == 0u);
}

BOOST_AUTO_TEST_CASE(multiKey)
{
    initData();

    Condition::Ptr condition = memDB->newCondition();
    condition->EQ("姓名", "张三");
    condition->GE("资产号", "2");

    Entry::Ptr entry = memDB->newEntry();
    entry->setField("资产名", "e90");

    size_t c = memDB->update("张三", entry, condition);
    BOOST_TEST_TRUE(c == 3u);

    entry = memDB->newEntry();
    entry->setField("姓名", "李四");
    entry->setField("资产号", "0");
    entry->setField("资产名", "福特");

    c = memDB->insert("李四", entry);
    BOOST_TEST_TRUE(c == 1u);

    auto datas = memDB->data();
    BOOST_TEST_TRUE(datas->size() == 2u);
}

BOOST_AUTO_TEST_CASE(hash)
{
    initData();
    memDB->hash();
}

BOOST_AUTO_TEST_CASE(select_exception)
{
    memDB->setBlockNum(-99999);
    Condition::Ptr condition = memDB->newCondition();
    memDB->select("Exception", condition);
}

BOOST_AUTO_TEST_CASE(update_exception)
{
    memDB->setBlockNum(-99999);
    Condition::Ptr condition = memDB->newCondition();
    Entry::Ptr entry = memDB->newEntry();
    memDB->update("Exception", entry, condition);
}

BOOST_AUTO_TEST_CASE(insert_exception)
{
    memDB->setBlockNum(-99999);
    Entry::Ptr entry = memDB->newEntry();
    memDB->insert("Exception", entry);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_MemoryDB
