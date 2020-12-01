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

/**
 * @brief : KeyCenter(key-manager) unitest
 * @author: jimmyshi
 * @date: 2019-05-30
 */


#include <jsonrpccpp/common/exception.h>
#include <libsecurity/KeyCenter.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace std;
using namespace bcos;
using namespace jsonrpc;

namespace bcos
{
namespace test
{
class FakeKeyCenterHttpClient : public KeyCenterHttpClientInterface
{
public:
    using Ptr = std::shared_ptr<FakeKeyCenterHttpClient>;

    void connect() override { m_isOpen = true; }
    void close() override { m_isOpen = false; };
    Json::Value callMethod(const std::string& _method, Json::Value _params) override
    {
        BOOST_CHECK(m_isOpen);
        queryCnt++;

        Json::Value queryJson;
        queryJson["id"] = 83;
        queryJson["jsonrpc"] = "2.0";
        queryJson["method"] = _method;
        queryJson["params"] = _params;
        latestQueryJson = queryJson;

        Json::Value rsp;
        rsp["error"] = 0;
        rsp["dataKey"] = dataKeyStr;
        rsp["info"] = "Ok";
        return rsp;
    }

    string latestQueryJsonString()
    {
        Json::FastWriter fastWriter;
        return fastWriter.write(latestQueryJson);
    }

private:
    bool m_isOpen = false;

public:
    Json::Value latestQueryJson;
    string dataKeyStr = "313233343536";
    size_t queryCnt = 0;
};

class KeyCenterFixture : public TestOutputHelperFixture
{
public:
    KeyCenterFixture() : TestOutputHelperFixture()
    {
        kcClient = make_shared<FakeKeyCenterHttpClient>();
        keyCenter = make_shared<KeyCenter>();
        keyCenter->setKcClient(kcClient);
    }

    FakeKeyCenterHttpClient::Ptr kcClient;
    KeyCenter::Ptr keyCenter;
};

BOOST_FIXTURE_TEST_SUITE(KeyCenterTest, KeyCenterFixture)

BOOST_AUTO_TEST_CASE(SetIpPortTest)
{
    keyCenter->setIpPort("127.0.0.1", 2333);
    BOOST_CHECK(keyCenter->url() == "127.0.0.1:2333");
}

BOOST_AUTO_TEST_CASE(getDataKeyTest)
{
    const string fakeKey = "85cf52964f7334c015545b394c1ffec9";

    BOOST_CHECK_EQUAL(kcClient->queryCnt, 0);

    bytes key = keyCenter->getDataKey(fakeKey);
    bytes compareKey = keyCenter->uniformDataKey(*fromHexString(kcClient->dataKeyStr));
    BOOST_CHECK(key == compareKey);
    BOOST_CHECK_EQUAL(kcClient->queryCnt, 1);

    string queryStr = kcClient->latestQueryJsonString();
    string compareQueryStr =
        "{\"id\":83,\"jsonrpc\":\"2.0\",\"method\":\"decDataKey\",\"params\":["
        "\"85cf52964f7334c015545b394c1ffec9\"]}\n";
    BOOST_CHECK_EQUAL(queryStr, compareQueryStr);

    // test query buffer
    key = keyCenter->getDataKey(fakeKey);
    BOOST_CHECK(key == compareKey);
    BOOST_CHECK_EQUAL(kcClient->queryCnt, 1);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
