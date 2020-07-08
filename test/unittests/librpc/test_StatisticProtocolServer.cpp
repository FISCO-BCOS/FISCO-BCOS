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
 * (c) 2016-2020 fisco-dev contributors.
 */

/**
 * @brief : test_StatisticProtocolServer.cpp
 * @author: yujiechen
 * @date: 2020-07-08
 */

#include <libflowlimit/RPCQPSLimiter.h>
#include <librpc/ModularServer.h>
#include <librpc/Rpc.h>
#include <librpc/StatisticProtocolServer.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <utility>

using namespace dev;
using namespace dev::rpc;
using namespace dev::flowlimit;

namespace dev
{
namespace test
{
class FakeStatisticProtocolServer : public StatisticProtocolServer
{
public:
    FakeStatisticProtocolServer() : StatisticProtocolServer(*(new ModularServer<rpc::Rpc>(nullptr)))
    {}

    ~FakeStatisticProtocolServer() override {}
    std::set<std::string> const& groupRPCMethodSet() { return m_groupRPCMethodSet; }

    void HandleJsonRequest(const Json::Value& _request, Json::Value& _response) override
    {
        Json::FastWriter writer;
        WrapError(_request, 0, "success", _response);
    }

    dev::GROUP_ID WrapperGetGroupID(Json::Value const& _request)
    {
        return StatisticProtocolServer::getGroupID(_request);
    }
};

template <class T>
std::string fakeRequest(std::string const& _interface, T _groupID)
{
    Json::Value fakedRequest;
    Json::FastWriter writer;
    fakedRequest[KEY_REQUEST_METHODNAME] = _interface;
    fakedRequest[KEY_REQUEST_PARAMETERS][0u] = _groupID;
    return writer.write(fakedRequest);
}

void checkSuccessResponse(std::string const& _responseStr)
{
    Json::Reader reader;
    Json::Value response;
    reader.parse(_responseStr, response, false);
    BOOST_CHECK(response["jsonrpc"] == "2.0");
    BOOST_CHECK(response["error"]["code"] = 0);
    BOOST_CHECK(response["error"]["message"] = "success");
}

void checkPermssionDenied(std::string const& _responseStr)
{
    Json::Reader reader;
    Json::Value response;
    reader.parse(_responseStr, response, false);
    BOOST_CHECK(response["jsonrpc"] == "2.0");
    BOOST_CHECK(response["error"]["code"] = RPCExceptionType::PermissionDenied);
}

void checkPermission(std::shared_ptr<FakeStatisticProtocolServer> fakeRPCHandler)
{
    std::srand(utcTime());
    // case1: check passed
    for (auto const& interface : fakeRPCHandler->groupRPCMethodSet())
    {
        auto groupId = boost::lexical_cast<std::string>(std::rand() % (655360000));
        auto fakedRequestStr = fakeRequest(interface, groupId);
        std::string retValue;
        // without permission checker
        fakeRPCHandler->HandleChannelRequest(fakedRequestStr, retValue, nullptr);
        checkSuccessResponse(retValue);
        // with permission checker, and check passed
        fakeRPCHandler->HandleChannelRequest(
            fakedRequestStr, retValue, [](dev::GROUP_ID) { return true; });
        checkSuccessResponse(retValue);
    }

    // case2: check permission denied
    for (auto const& interface : fakeRPCHandler->groupRPCMethodSet())
    {
        auto groupId = boost::lexical_cast<std::string>(std::rand() % (655360000));
        auto fakedRequestStr = fakeRequest(interface, groupId);
        std::string retValue;
        // with permission checker, and check failed
        fakeRPCHandler->HandleChannelRequest(
            fakedRequestStr, retValue, [](dev::GROUP_ID) { return false; });
        checkPermssionDenied(retValue);
    }
}

BOOST_FIXTURE_TEST_SUITE(StatisticProtocolServerTest, TestOutputHelperFixture)
BOOST_AUTO_TEST_CASE(testHandleChannelRequest)
{
    auto fakeRPCHandler = std::make_shared<FakeStatisticProtocolServer>();
    // check getGroupId
    Json::Value fakedRequest;
    fakedRequest[KEY_REQUEST_METHODNAME] = "getBlockByNumber";
    fakedRequest[KEY_REQUEST_PARAMETERS][0u] = 1;
    BOOST_CHECK(fakeRPCHandler->WrapperGetGroupID(fakedRequest) == 1);

    fakedRequest[KEY_REQUEST_PARAMETERS][0u] = 1000;
    BOOST_CHECK(fakeRPCHandler->WrapperGetGroupID(fakedRequest) == 1000);

    uint64_t invalidGroupId = 65536;
    fakedRequest[KEY_REQUEST_PARAMETERS][0u] = invalidGroupId;
    BOOST_CHECK(fakeRPCHandler->WrapperGetGroupID(fakedRequest) == -1);
    fakedRequest[KEY_REQUEST_PARAMETERS][0u] = 655360000;
    BOOST_CHECK(fakeRPCHandler->WrapperGetGroupID(fakedRequest) == -1);

    // checkPermission without limitRPCQPS
    checkPermission(fakeRPCHandler);

    // checkPermission with limitRPCQPS
    auto qpsLimiter = std::make_shared<RPCQPSLimiter>();
    fakeRPCHandler->setQPSLimiter(qpsLimiter);
    checkPermission(fakeRPCHandler);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
