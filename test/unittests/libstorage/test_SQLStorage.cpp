/**
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
 *
 * @brief
 *
 * @file test_AMOPStorage.cpp
 * @author: monan
 * @date 2019-03-27
 */

#include <libchannelserver/ChannelRPCServer.h>
#include <libdevcore/FixedHash.h>
#include <libstorage/SQLStorage.h>
#include <libstorage/StorageException.h>
#include <libstorage/Table.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::storage;

namespace test_SQLStorage
{
class MockChannelRPCServer : public dev::ChannelRPCServer
{
public:
    dev::channel::TopicChannelMessage::Ptr pushChannelMessage(
        dev::channel::TopicChannelMessage::Ptr message, size_t timeout) override
    {
        BOOST_TEST(timeout > 0);

        std::string jsonStr(message->data(), message->data() + message->dataSize());

        std::stringstream ssIn;
        ssIn << jsonStr;

        Json::Value requestJson;
        ssIn >> requestJson;

        Json::Value responseJson;


        if (requestJson["op"].asString() == "select")
        {
            if (requestJson["params"]["table"].asString() == "e")
            {
                BOOST_THROW_EXCEPTION(StorageException(-1, "mock exception"));
            }

            if (requestJson["params"]["key"].asString() != "LiSi")
            {
                responseJson["code"] = 0;
            }
            else if (requestJson["params"]["condition"].isArray() &&
                     requestJson["params"]["condition"][0][2].asString() == "2")
            {
                responseJson["code"] = 0;
            }
            else
            {
                Json::Value resultJson;
                resultJson["columns"].append("Name");
                resultJson["columns"].append("id");

                Json::Value entry;
                entry.append("LiSi");
                entry.append("1");

                resultJson["data"].append(entry);

                responseJson["code"] = 0;
                responseJson["result"] = resultJson;
            }
        }
        else if (requestJson["op"].asString() == "commit")
        {
            size_t count = 0;
            for (auto it : requestJson["params"]["data"])
            {
                ++count;
                if (it["table"] == "e")
                {
                    BOOST_THROW_EXCEPTION(StorageException(-1, "mock exception"));
                }
            }

            responseJson["result"]["count"] = (Json::UInt64)count;
            responseJson["code"] = 0;
        }

        std::string responseStr = responseJson.toStyledString();
        LOG(TRACE) << "AMOP Storage response:" << responseStr;

        auto response = std::make_shared<dev::channel::TopicChannelMessage>();
        response->setResult(0);
        response->setSeq(message->seq());
        response->setType(dev::channel::AMOP_RESPONSE);
        response->setTopicData(
            message->topic(), (const unsigned char*)responseStr.data(), responseStr.size());

        return response;
    }
};

struct SQLStorageFixture
{
    SQLStorageFixture()
    {
        sqlStorage = std::make_shared<dev::storage::SQLStorage>();
        std::shared_ptr<MockChannelRPCServer> mockChannel =
            std::make_shared<MockChannelRPCServer>();
        sqlStorage->setChannelRPCServer(mockChannel);
        sqlStorage->setMaxRetry(20);
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
    dev::storage::SQLStorage::Ptr sqlStorage;
};

BOOST_FIXTURE_TEST_SUITE(SQLStorageTest, SQLStorageFixture)


BOOST_AUTO_TEST_CASE(onlyDirty)
{
    BOOST_CHECK_EQUAL(sqlStorage->onlyCommitDirty(), true);
}

BOOST_AUTO_TEST_CASE(empty_select)
{
    h256 h(0x01);
    int num = 1;
    std::string table("t_test");
    std::string key("id");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    Entries::Ptr entries = sqlStorage->select(num, tableInfo, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 0u);
}

BOOST_AUTO_TEST_CASE(select_condition)
{
    h256 h(0x01);
    int num = 1;
    std::string table("t_test");
    auto condition = std::make_shared<Condition>();
    condition->EQ("id", "2");

    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    Entries::Ptr entries = sqlStorage->select(num, tableInfo, "LiSi", condition);
    BOOST_CHECK_EQUAL(entries->size(), 0u);

    condition = std::make_shared<Condition>();
    condition->EQ("id", "1");
    tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    entries = sqlStorage->select(num, tableInfo, "LiSi", condition);
    BOOST_CHECK_EQUAL(entries->size(), 1u);
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
    size_t c = sqlStorage->commit(num, datas);
    BOOST_CHECK_EQUAL(c, 1u);
    std::string table("t_test");
    std::string key("LiSi");
    auto tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;
    entries = sqlStorage->select(num, tableInfo, key, std::make_shared<Condition>());
    BOOST_CHECK_EQUAL(entries->size(), 1u);
}

BOOST_AUTO_TEST_CASE(exception)
{
#if 0
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
    tableData->entries = entries;
    datas.push_back(tableData);
    BOOST_CHECK_THROW(sqlStorage->commit(num, datas, blockHash), boost::exception);
    std::string table("e");
    std::string key("Exception");

    BOOST_CHECK_THROW(sqlStorage->select(num, table, key, std::make_shared<Condition>()), boost::exception);
#endif
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_SQLStorage
