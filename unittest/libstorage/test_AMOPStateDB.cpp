#include "Common.h"
#include <json/json.h>
#include <libchannelserver/ChannelMessage.h>
#include <libdevcore/FixedHash.h>
#include <libstorage/AMOPStateStorage.h>
#include <libstorage/StateDB.h>
#include <libstorage/StorageException.h>
#include <libweb3jsonrpc/ChannelRPCServer.h>
#include <unittest/Common.h>
#include <boost/test/unit_test.hpp>
using namespace dev;
using namespace dev::storage;

namespace test_AMOPStateStorage
{
//
class MockInfoChannelRPCServer : public dev::ChannelRPCServer
{
public:
    typedef std::shared_ptr<MockInfoChannelRPCServer> Ptr;

    virtual ~MockInfoChannelRPCServer(){};

    virtual dev::channel::TopicChannelMessage::Ptr pushChannelMessage(
        dev::channel::TopicChannelMessage::Ptr message)
    {
        std::string topic = message->topic();

        BOOST_TEST(topic == "DB");

        BOOST_TEST(message->type() == 0x30);
        BOOST_TEST(message->result() == 0);

        std::stringstream ssIn;
        std::string jsonStr((const char*)message->data(), message->dataSize());
        ssIn << jsonStr;

        Json::Value requestJson;
        ssIn >> requestJson;

        BOOST_TEST(requestJson["op"].asString() == "info");
        BOOST_TEST(requestJson["params"]["blockHash"].asString() == h256(0x12345).hex());
        BOOST_TEST(requestJson["params"]["num"].asInt() == 1000);
        BOOST_TEST(requestJson["params"]["table"].asString() == "t_test");

        Json::Value responseJson;
        responseJson["code"] = 0;
        responseJson["key"] = "姓名";
        responseJson["result"]["indices"].append("姓名");
        responseJson["result"]["indices"].append("资产号");
        responseJson["result"]["indices"].append("资产名");

        dev::channel::TopicChannelMessage::Ptr response =
            std::make_shared<dev::channel::TopicChannelMessage>();
        response->setType(0x31);
        response->setSeq(message->seq());
        response->setResult(0);

        std::stringstream ssOut;
        ssOut << responseJson;

        auto str = ssOut.str();

        response->setTopic(topic);
        response->setData((const byte*)str.data(), str.size());

        return response;
    }
};

class MockSelectChannelRPCServer : public dev::ChannelRPCServer
{
public:
    typedef std::shared_ptr<MockSelectChannelRPCServer> Ptr;

    virtual ~MockSelectChannelRPCServer(){};

    virtual dev::channel::TopicChannelMessage::Ptr pushChannelMessage(
        dev::channel::TopicChannelMessage::Ptr message)
    {
        std::string topic = message->topic();

        //测试请求参数
        BOOST_TEST(topic == "DB");

        BOOST_TEST(message->type() == 0x30);
        BOOST_TEST(message->result() == 0);

        std::stringstream ssIn;
        std::string jsonStr((const char*)message->data(), message->dataSize());
        ssIn << jsonStr;

        Json::Value requestJson;
        ssIn >> requestJson;

        //测试请求参数
        BOOST_TEST(requestJson["op"].asString() == "select");
        BOOST_TEST(requestJson["params"]["blockHash"].asString() == h256(0x12345).hex());
        BOOST_TEST(requestJson["params"]["num"].asInt() == 1000);
        BOOST_TEST(requestJson["params"]["table"].asString() == "t_test");
        BOOST_TEST(requestJson["params"]["key"].asString() == "张三");

        //设置返回数据，供测试使用，这一步模拟数据源
        //设置表头
        Json::Value responseJson;
        responseJson["code"] = 0;
        responseJson["result"]["columns"].append("姓名");
        responseJson["result"]["columns"].append("资产号");
        responseJson["result"]["columns"].append("资产名");
        responseJson["result"]["columns"].append(STATUS);

        //增加记录
        Json::Value line1;
        line1.append("张三");
        line1.append("0");
        line1.append("保时捷911");
        line1.append(0);
        responseJson["result"]["data"].append(line1);

        Json::Value line2;
        line2.append("张三");
        line2.append("1");
        line2.append("amg gtr");
        line2.append(0);
        responseJson["result"]["data"].append(line2);

        //封装返回数据responseJson到response
        dev::channel::TopicChannelMessage::Ptr response =
            std::make_shared<dev::channel::TopicChannelMessage>();
        //设置response的头部数据
        response->setType(0x31);
        response->setSeq(message->seq());
        response->setResult(0);

        //设置response的返回的主体数据，借助std::stringstream进行数据格式转化
        std::stringstream ssOut;
        ssOut << responseJson;

        auto str = ssOut.str();

        response->setTopic(topic);
        response->setData((const byte*)str.data(), str.size());

        return response;
    }
};

class MockSelectChannelRPCServerCodeAndResult : public dev::ChannelRPCServer
{
public:
    typedef std::shared_ptr<MockSelectChannelRPCServerCodeAndResult> Ptr;

    virtual ~MockSelectChannelRPCServerCodeAndResult() {}

    virtual dev::channel::TopicChannelMessage::Ptr pushChannelMessage(
        dev::channel::TopicChannelMessage::Ptr message)
    {
        count++;

        std::string topic = message->topic();

        BOOST_TEST(topic == "DB");

        BOOST_TEST(message->type() == 0x30);
        BOOST_TEST(message->result() == 0);

        std::stringstream ssIn;
        std::string jsonStr((const char*)message->data(), message->dataSize());
        ssIn << jsonStr;

        Json::Value requestJson;
        ssIn >> requestJson;

        BOOST_TEST(requestJson["op"].asString() == "select");
        BOOST_TEST(requestJson["params"]["blockHash"].asString() == h256(0x12345).hex());
        BOOST_TEST(requestJson["params"]["num"].asInt() == 1000);
        BOOST_TEST(requestJson["params"]["table"].asString() == "t_test");
        BOOST_TEST(requestJson["params"]["key"].asString() == "张三");

        Json::Value responseJson;
        switch (count)
        {  // test code
        case 1:
            responseJson["code"] = "p";
            break;
        case 2:
            responseJson["code"] = 1;
            break;
        default:
            responseJson["code"] = 0;
        }
        responseJson["result"]["columns"].append("姓名");
        responseJson["result"]["columns"].append("资产号");
        responseJson["result"]["columns"].append("资产名");
        responseJson["result"]["columns"].append(STATUS);

        Json::Value line1;
        line1.append("张三");
        line1.append("0");
        line1.append("保时捷911");
        line1.append(0);
        responseJson["result"]["data"].append(line1);

        Json::Value line2;
        line2.append("张三");
        line2.append("1");
        line2.append("amg gtr");
        line2.append(0);
        responseJson["result"]["data"].append(line2);

        dev::channel::TopicChannelMessage::Ptr response =
            std::make_shared<dev::channel::TopicChannelMessage>();
        response->setType(0x31);
        response->setSeq(message->seq());
        if (count == 3)
        {
            response->setResult(1);  // test result
        }
        else
        {
            response->setResult(0);
        }

        std::stringstream ssOut;
        ssOut << responseJson;

        auto str = ssOut.str();

        response->setTopic(topic);
        response->setData((const byte*)str.data(), str.size());

        return response;
    }

private:
    int count = 0;
};

class MockCommitChannelRPCServer : public dev::ChannelRPCServer
{
public:
    typedef std::shared_ptr<MockCommitChannelRPCServer> Ptr;

    virtual ~MockCommitChannelRPCServer() {}

    virtual dev::channel::TopicChannelMessage::Ptr pushChannelMessage(
        dev::channel::TopicChannelMessage::Ptr message)
    {
        std::string topic = message->topic();
        BOOST_TEST(topic == "DB");

        BOOST_TEST(message->type() == 0x30);
        BOOST_TEST(message->result() == 0);

        std::stringstream ssIn;
        std::string jsonStr((const char*)message->data(), message->dataSize());
        ssIn << jsonStr;

        Json::Value requestJson;
        ssIn >> requestJson;

        BOOST_TEST(requestJson["op"].asString() == "commit");
        BOOST_TEST(requestJson["params"]["blockHash"].asString() == h256(0x12345).hex());
        BOOST_TEST(requestJson["params"]["num"].asInt() == 1000);
        // BOOST_TEST(requestJson["params"]["table"].asString() == "t_test");

        BOOST_TEST(requestJson["params"]["data"].size() == 2);

        Json::Value tmp;
        Json::Value entry = requestJson["params"]["data"].get(Json::Value::ArrayIndex(0), tmp);

        BOOST_TEST(entry["table"] == "t_test");
        Json::Value entries = entry["entries"];

#if 0
		Json::Value tmp;
		Json::Value entry = requestJson["params"]["data"].get(Json::Value::ArrayIndex(0), tmp);

		BOOST_TEST(entry["fields"]["姓名"].asString() == "李四");
		BOOST_TEST(entry["fields"]["资产号"].asString() == "0");
		BOOST_TEST(entry["fields"]["资产名"].asString() == "z4");
		BOOST_TEST(entry[STATUS].asString() == "0");
#endif

        Json::Value responseJson;
        responseJson["code"] = 0;
        responseJson["result"]["count"] = 2;

        dev::channel::TopicChannelMessage::Ptr response =
            std::make_shared<dev::channel::TopicChannelMessage>();
        response->setType(0x31);
        response->setSeq(message->seq());
        response->setResult(0);

        std::stringstream ssOut;
        ssOut << responseJson;

        auto str = ssOut.str();

        response->setTopic(topic);
        response->setData((const byte*)str.data(), str.size());

        return response;
    }
};

class MockFailChannelRPCServer : public dev::ChannelRPCServer
{
public:
    typedef std::shared_ptr<MockFailChannelRPCServer> Ptr;

    virtual ~MockFailChannelRPCServer() {}

    virtual dev::channel::TopicChannelMessage::Ptr pushChannelMessage(
        dev::channel::TopicChannelMessage::Ptr message)
    {
        std::string topic = message->topic();

        BOOST_TEST(topic == "DB");

        BOOST_TEST(message->type() == 0x30);
        BOOST_TEST(message->result() == 0);

        if (failType == 0)
        {
            Json::Value responseJson;
            responseJson["code"] = -1;

            dev::channel::TopicChannelMessage::Ptr response =
                std::make_shared<dev::channel::TopicChannelMessage>();
            response->setType(0x31);
            response->setSeq(message->seq());
            response->setResult(0);

            std::stringstream ssOut;
            ssOut << responseJson;

            auto str = ssOut.str();

            response->setTopic(topic);
            response->setData((const byte*)str.data(), str.size());

            return response;
        }

        if (failType == 1)
        {
            return dev::channel::TopicChannelMessage::Ptr();
        }

        if (failType == 2)
        {
            dev::channel::TopicChannelMessage::Ptr response =
                std::make_shared<dev::channel::TopicChannelMessage>();
            response->setType(0x31);
            response->setSeq(message->seq());
            response->setResult(-1);

            return response;
        }
    }

    int failType = 0;
};

struct AMOPDBFixture
{
    AMOPDBFixture()
    {
        amopDB = std::make_shared<dev::storage::AMOPStateStorage>();

        // amopDB->setBlockHash(dev::h256(0x12345));
        // amopDB->setNum(1000);
        // amopDB->setTable("t_test");
        amopDB->setTopic("DB");
    }

    ~AMOPDBFixture()
    {
        //什么也不做
    }

    dev::storage::AMOPStateStorage::Ptr amopDB;
    h256 hash = dev::h256(0x12345);
    int num = 1000;
};

BOOST_FIXTURE_TEST_SUITE(AMOPDB,
    AMOPDBFixture)  //用在测试套件上。这样的话，测试套件每个测试用例就不用写了。

BOOST_AUTO_TEST_CASE(info)
{
    std::shared_ptr<MockInfoChannelRPCServer> channelServer =
        std::make_shared<MockInfoChannelRPCServer>();
    amopDB->setChannelRPCServer(channelServer);

#if 0
	TableInfo::Ptr info = amopDB->info("t_test");

	BOOST_TEST(info->name == "t_test");
	BOOST_TEST(info->key == "姓名");
	BOOST_TEST(info->indices.size() == 3);
	BOOST_TEST(info->indices[0] == "姓名");
	BOOST_TEST(info->indices[1] == "资产号");
	BOOST_TEST(info->indices[2] == "资产名");
#endif
}

BOOST_AUTO_TEST_CASE(select)
{
    //空查询
    std::shared_ptr<MockSelectChannelRPCServer> channelServer =
        std::make_shared<MockSelectChannelRPCServer>();
    amopDB->setChannelRPCServer(channelServer);
    dev::storage::Entries::Ptr entries = amopDB->select(hash, 1000, "t_test", "张三");

    BOOST_TEST(entries->size() == 2);

    Entry::Ptr entry = entries->get(0);

    BOOST_TEST(entry->getStatus() == Entry::Status::NORMAL);
    BOOST_TEST(entry->getField("姓名") == "张三");
    BOOST_TEST(entry->getField("资产号") == "0");
    BOOST_TEST(entry->getField("资产名") == "保时捷911");

    entry = entries->get(1);

    BOOST_TEST(entry->getStatus() == Entry::Status::NORMAL);
    BOOST_TEST(entry->getField("姓名") == "张三");
    BOOST_TEST(entry->getField("资产号") == "1");
    BOOST_TEST(entry->getField("资产名") == "amg gtr");
}

BOOST_AUTO_TEST_CASE(commit)
{
    std::vector<dev::storage::TableData::Ptr> datas;

    dev::storage::TableData::Ptr tableData = std::make_shared<dev::storage::TableData>();
    tableData->tableName = "t_test";
    Entries::Ptr entries = std::make_shared<Entries>();

    Entry::Ptr entry = std::make_shared<Entry>();
    entry->setField("姓名", "张三");
    entry->setField("资产号", "0");
    entry->setField("资产名", "z4");
    entries->addEntry(entry);

    tableData->data.insert(std::make_pair("张三", entries));

    datas.push_back(tableData);

    tableData = std::make_shared<dev::storage::TableData>();
    tableData->tableName = "t_test";
    entries = std::make_shared<Entries>();

    entry = std::make_shared<Entry>();
    entry->setField("姓名", "李四");
    entry->setField("资产号", "1");
    entry->setField("资产名", "自行车");
    entries->addEntry(entry);

    tableData->data.insert(std::make_pair("李四", entries));

    datas.push_back(tableData);

    std::shared_ptr<MockCommitChannelRPCServer> channelServer =
        std::make_shared<MockCommitChannelRPCServer>();
    amopDB->setChannelRPCServer(channelServer);

    dev::h256 h(0x12345);
    size_t count = amopDB->commit(hash, num, datas, h);
    BOOST_TEST(count == 2);
}

BOOST_AUTO_TEST_CASE(commit_emptySize)
{
    std::vector<dev::storage::TableData::Ptr> datas;
    dev::h256 h(0x12345);
    size_t count = amopDB->commit(hash, num, datas, h);
    BOOST_TEST(count == 0);
}

BOOST_AUTO_TEST_CASE(fail)
{
    std::shared_ptr<MockFailChannelRPCServer> channelServer =
        std::make_shared<MockFailChannelRPCServer>();
    amopDB->setChannelRPCServer(channelServer);
    amopDB->setMaxRetry(1);

    channelServer->failType = 0;
    TableInfo::Ptr info = amopDB->info("t_test");
}

BOOST_AUTO_TEST_CASE(onlyDirty)
{
    bool result = amopDB->onlyDirty();
    BOOST_TEST(result == true);
}

BOOST_AUTO_TEST_CASE(selectCodeAndResult)
{
    std::shared_ptr<MockSelectChannelRPCServerCodeAndResult> channelServer =
        std::make_shared<MockSelectChannelRPCServerCodeAndResult>();
    amopDB->setChannelRPCServer(channelServer);
    dev::storage::Entries::Ptr entries = amopDB->select(hash, 1000, "t_test", "张三");
}

BOOST_AUTO_TEST_CASE(selectMaxRetry)
{
    std::shared_ptr<MockSelectChannelRPCServerCodeAndResult> channelServer =
        std::make_shared<MockSelectChannelRPCServerCodeAndResult>();
    amopDB->setChannelRPCServer(channelServer);
    amopDB->setMaxRetry(1);
    amopDB->setFatalHandler([](std::exception& e) {});
    BOOST_CHECK_THROW(
        dev::storage::Entries::Ptr entries = amopDB->select(hash, 1000, "t_test", "张三"),
        StorageException);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_AMOPStateStorage
