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
 * @file EventLogFilterTest.cpp
 * @author: octopuswang
 * @date 2019-08-13
 */
#include <iostream>

#include "json/value.h"
#include <libdevcore/Common.h>
#include <libethcore/CommonJS.h>
#include <libethcore/LogEntry.h>
#include <libeventfilter/EventLogFilter.h>
#include <libeventfilter/EventLogFilterManager.h>
#include <libeventfilter/EventLogFilterParams.h>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::event;
namespace ut = boost::unit_test;

namespace dev
{
namespace test
{
struct EventLogFilterTestFixture
{
    EventLogFilterTestFixture() {}

    ~EventLogFilterTestFixture() {}
};
BOOST_FIXTURE_TEST_SUITE(EventLogFilterTest, EventLogFilterTestFixture)

BOOST_AUTO_TEST_CASE(EventLogFilterParams_test0)
{
    // groupId => groupID
    std::string json0 =
        "{\"groupId\":\"1\",\"filterID\":\"43289048jhkjjk\",\"fromBlock\":\"0x1\",\"toBlock\":"
        "\"0x2\","
        "\"addresses\":"
        "\"0x8888f1f195afa192cfee860698584c030f4c9db1\",\"topics\":["
        "\"0x000000000000000000000000a94f5374fce5edbc8e2a8697c15331677e6ebf0b\",null,["
        "\"0x000000000000000000000000a94f5374fce5edbc8e2a8697c15331677e6ebf0b\","
        "\"0x0000000000000000000000000aff3454fce5edbc8cca8697c15331677e6ebccc\"]]}";
    auto params0 = EventLogFilterParams::buildEventLogFilterParamsObject(json0);

    BOOST_CHECK_EQUAL(params0, nullptr);

    // topics loss
    std::string json1 =
        "{\"groupId\":\"1\",\"fromBlock\":\"0x1\",\"toBlock\":\"0x2\",\"addresses\":"
        "\"0x8888f1f195afa192cfee860698584c030f4c9db1\"}";
    auto params1 = EventLogFilterParams::buildEventLogFilterParamsObject(json1);

    BOOST_CHECK_EQUAL(params1, nullptr);

    // address loss
    std::string json2 =
        "{\"groupID\":\"1\",\"fromBlock\":\"0x1\",\"toBlock\":\"0x2\",\"topics\":["
        "\"0x000000000000000000000000a94f5374fce5edbc8e2a8697c15331677e6ebf0b\",null,["
        "\"0x000000000000000000000000a94f5374fce5edbc8e2a8697c15331677e6ebf0b\","
        "\"0x0000000000000000000000000aff3454fce5edbc8cca8697c15331677e6ebccc\"]]}";
    auto params2 = EventLogFilterParams::buildEventLogFilterParamsObject(json2);

    BOOST_CHECK_EQUAL(params2, nullptr);

    // address loss
    std::string json3 =
        "{\"groupId\":\"1\",\"fromBlock\":\"0x1\",\"toBlock\":\"0x2\",\"addresses\":"
        "\"0x8888f1f195afa192cfee860698584c030f4c9db1\",\"topics\":["
        "\"0x000000000000000000000000a94f5374fce5edbc8e2a8697c15331677e6ebf0b\",null,["
        "\"0x000000000000000000000000a94f5374fce5edbc8e2a8697c15331677e6ebf0b\","
        "\"0x0000000000000000000000000aff3454fce5edbc8cca8697c15331677e6ebccc\"],null,null]}";
    auto params3 = EventLogFilterParams::buildEventLogFilterParamsObject(json3);

    BOOST_CHECK_EQUAL(params3, nullptr);

    // groupId => groupID
    std::string json4 =
        "{\"groupId\":\"0\",\"filterID\":\"43289048jhkjjk\",\"fromBlock\":\"0x1\",\"toBlock\":"
        "\"0x2\","
        "\"addresses\":"
        "\"0x8888f1f195afa192cfee860698584c030f4c9db1\",\"topics\":["
        "\"0x000000000000000000000000a94f5374fce5edbc8e2a8697c15331677e6ebf0b\",null,["
        "\"0x000000000000000000000000a94f5374fce5edbc8e2a8697c15331677e6ebf0b\","
        "\"0x0000000000000000000000000aff3454fce5edbc8cca8697c15331677e6ebccc\"]]}";
    auto params4 = EventLogFilterParams::buildEventLogFilterParamsObject(json4);

    BOOST_CHECK_EQUAL(params4, nullptr);
}

BOOST_AUTO_TEST_CASE(EventLogFilterParams_test1)
{
    std::string json =
        "{\"groupID\":\"1\",\"filterID\":\"adfsflkjklj9poi9\",\"fromBlock\":\"latest\",\"toBlock\":"
        "\"latest\",\"addresses\":"
        "\"0x8888f1f195afa192cfee860698584c030f4c9db1\",\"topics\":["
        "\"0x000000000000000000000000a94f5374fce5edbc8e2a8697c15331677e6ebf0b\",null,["
        "\"0x000000000000000000000000a94f5374fce5edbc8e2a8697c15331677e6ebf0b\","
        "\"0x0000000000000000000000000aff3454fce5edbc8cca8697c15331677e6ebccc\"]]}";
    auto params = EventLogFilterParams::buildEventLogFilterParamsObject(json);

    BOOST_CHECK_EQUAL(params->getFromBlock(), int64_t(eth::MAX_BLOCK_NUMBER));
    BOOST_CHECK_EQUAL(params->getToBlock(), int64_t(eth::MAX_BLOCK_NUMBER));
    BOOST_CHECK_EQUAL(params->getAddresses().size(), 1);
    BOOST_CHECK_EQUAL(toHexPrefixed(*(params->getAddresses().begin())),
        "0x8888f1f195afa192cfee860698584c030f4c9db1");

    std::string json1 =
        "{\"groupID\":\"1\",\"filterID\":\"adfsflkjklj9poi9\",\"addresses\":"
        "\"0x8888f1f195afa192cfee860698584c030f4c9db1\",\"topics\":["
        "\"0x000000000000000000000000a94f5374fce5edbc8e2a8697c15331677e6ebf0b\",null,["
        "\"0x000000000000000000000000a94f5374fce5edbc8e2a8697c15331677e6ebf0b\","
        "\"0x0000000000000000000000000aff3454fce5edbc8cca8697c15331677e6ebccc\"]]}";
    auto params1 = EventLogFilterParams::buildEventLogFilterParamsObject(json1);

    BOOST_CHECK_EQUAL(params1->getFromBlock(), int64_t(0x7fffffffffffffff));
    BOOST_CHECK_EQUAL(params1->getToBlock(), int64_t(0x7fffffffffffffff));
    BOOST_CHECK_EQUAL(params1->getAddresses().size(), 1);
    BOOST_CHECK_EQUAL(toHexPrefixed(*(params->getAddresses().begin())),
        "0x8888f1f195afa192cfee860698584c030f4c9db1");
}


BOOST_AUTO_TEST_CASE(EventLogFilter_test0)
{
    std::string json =
        "{\"groupID\":\"1\",\"filterID\":\"adfsflkjklj9poi9\",\"fromBlock\":\"latest\",\"toBlock\":"
        "\"latest\",\"addresses\":"
        "\"0x8888f1f195afa192cfee860698584c030f4c9db1\",\"topics\":["
        "\"0x000000000000000000000000a94f5374fce5edbc8e2a8697c15331677e6ebf0b\",null,["
        "\"0x000000000000000000000000a94f5374fce5edbc8e2a8697c15331677e6ebf0b\","
        "\"0x0000000000000000000000000aff3454fce5edbc8cca8697c15331677e6ebccc\"]]}";
    auto params = EventLogFilterParams::buildEventLogFilterParamsObject(json);

    EventLogFilter::Ptr filter = std::make_shared<EventLogFilter>(params, 1000, 1);

    BOOST_CHECK_EQUAL(filter->getNextBlockToProcess(), 1000);

    filter->updateNextBlockToProcess(-1);
    BOOST_CHECK_EQUAL(filter->getNextBlockToProcess(), -1);

    filter->setResponseCallBack([](const std::string& _filterID, int32_t _result,
                                    const Json::Value& _logs, GROUP_ID const& _groupId) -> bool {
        EVENT_LOG(INFO) << LOG_KV("result", _result) << LOG_KV("_filterID", _filterID)
                        << LOG_KV("groupId", _groupId);
        (void)_logs;
        return true;
    });

    auto r0 = filter->getResponseCallback()("aaa", -1, Json::Value(), 1);
    BOOST_CHECK_EQUAL(r0, true);
}

BOOST_AUTO_TEST_CASE(EventLogFilter_test1)
{
    std::string json =
        "{\"groupID\":\"1\",\"filterID\":\"adfsflkjklj9poi9\",\"fromBlock\":\"latest\","
        "\"toBlock\":\"latest\",\"addresses\":"
        "\"0x692a70d2e424a56d2c6c27aa97d1a86395877b3a\",\"topics\":["
        "\"0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4\"]}";

    auto params = EventLogFilterParams::buildEventLogFilterParamsObject(json);

    BOOST_CHECK_EQUAL(params->getFromBlock(), int64_t(0x7fffffffffffffff));
    BOOST_CHECK_EQUAL(params->getToBlock(), int64_t(0x7fffffffffffffff));
    BOOST_CHECK_EQUAL(params->getAddresses().size(), 1);
    BOOST_CHECK_EQUAL(params->getTopics()[0].size(), 1);

    EventLogFilter::Ptr filter = std::make_shared<EventLogFilter>(params, 1111, 2);

    BOOST_CHECK_EQUAL(filter->getNextBlockToProcess(), 1111);

    BOOST_CHECK_EQUAL(toHexPrefixed(*(params->getAddresses().begin())),
        "0x692a70d2e424a56d2c6c27aa97d1a86395877b3a");

    BOOST_CHECK_EQUAL(toHexPrefixed(*(params->getTopics()[0].begin())),
        "0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4");

    Address addr = jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3a");
    h256s h{jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4")};

    LogEntry _log{addr, h, bytes{}};

    BOOST_CHECK_EQUAL(*(params->getAddresses().begin()), addr);
    BOOST_CHECK_EQUAL(*(params->getTopics()[0].begin()), h[0]);

    BOOST_CHECK_EQUAL(filter->matches(_log), true);
}

BOOST_AUTO_TEST_CASE(EventLogFilter_test2)
{
    std::string json =
        "{\"groupID\":\"1\",\"filterID\":\"adfsflkjklj9poi9\",\"fromBlock\":\"latest\",\"toBlock\":"
        "\"latest\",\"addresses\":"
        "\"0x692a70d2e424a56d2c6c27aa97d1a86395877b3a\",\"topics\":["
        "\"0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5\"]}";

    auto params = EventLogFilterParams::buildEventLogFilterParamsObject(json);

    BOOST_CHECK_EQUAL(params->getFromBlock(), int64_t(0x7fffffffffffffff));
    BOOST_CHECK_EQUAL(params->getToBlock(), int64_t(0x7fffffffffffffff));
    BOOST_CHECK_EQUAL(params->getAddresses().size(), 1);
    BOOST_CHECK_EQUAL(params->getTopics()[0].size(), 1);

    EventLogFilter::Ptr filter = std::make_shared<EventLogFilter>(params, 1111, 3);

    BOOST_CHECK_EQUAL(filter->getNextBlockToProcess(), 1111);

    h256s topics{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4")};

    LogEntry _log{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3a"), topics, bytes{}};

    BOOST_CHECK_EQUAL(filter->matches(_log), false);

    h256s topics0{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa6")};
    LogEntry _log0{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3a"), topics0, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log0), true);

    h256s topics1{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
    };
    LogEntry _log1{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3a"), topics1, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log1), true);
}


BOOST_AUTO_TEST_CASE(EventLogFilter_test3)
{
    std::string json =
        "{\"groupID\":\"1\",\"filterID\":\"adfsflkjklj9poi9\",\"fromBlock\":\"1111\",\"toBlock\":"
        "\"2222\",\"addresses\":["
        "\"0x692a70d2e424a56d2c6c27aa97d1a86395877b3a\","
        "\"0x692a70d2e424a56d2c6c27aa97d1a86395877b3b\","
        "\"0x692a70d2e424a56d2c6c27aa97d1a86395877b3c\"],\"topics\":["
        "\"0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4\",null,"
        "\"0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5\",["
        "\"0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa6\",null,"
        "\"0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa7\"]]}";

    auto params = EventLogFilterParams::buildEventLogFilterParamsObject(json);

    BOOST_CHECK_EQUAL(params->getFilterID(), "adfsflkjklj9poi9");
    BOOST_CHECK_EQUAL(params->getFromBlock(), int64_t(1111));
    BOOST_CHECK_EQUAL(params->getToBlock(), int64_t(2222));
    BOOST_CHECK_EQUAL(params->getAddresses().size(), 3);

    BOOST_CHECK_EQUAL(params->getTopics()[0].size(), 1);
    BOOST_CHECK_EQUAL(params->getTopics()[1].size(), 0);
    BOOST_CHECK_EQUAL(params->getTopics()[2].size(), 1);
    BOOST_CHECK_EQUAL(params->getTopics()[3].size(), 2);

    EventLogFilter::Ptr filter = std::make_shared<EventLogFilter>(params, 0, 0);

    BOOST_CHECK_EQUAL(filter->getNextBlockToProcess(), 0);

    BOOST_CHECK_EQUAL(toHexPrefixed(*(params->getTopics()[0].begin())),
        "0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4");
    BOOST_CHECK_EQUAL(toHexPrefixed(*(params->getTopics()[2].begin())),
        "0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5");

    h256s topics0{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa6")};
    LogEntry _log0{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3a"), topics0, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log0), true);

    h256s topics1{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa6")};
    LogEntry _log1{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3b"), topics1, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log1), true);

    h256s topics2{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa6")};
    LogEntry _log2{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3c"), topics2, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log2), true);

    h256s topics3{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa7")};
    LogEntry _log3{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3a"), topics3, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log3), true);

    h256s topics4{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa7")};
    LogEntry _log4{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3b"), topics4, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log4), true);

    h256s topics5{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa7")};
    LogEntry _log5{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3c"), topics5, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log5), true);

    h256s topics6{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa7")};
    LogEntry _log6{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3d"), topics6, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log6), false);
}


BOOST_AUTO_TEST_CASE(EventLogFilter_test4)
{
    std::string json =
        "{\"groupID\":\"10\",\"filterID\":\"adfsflkjklj9poi9aa\",\"fromBlock\":\"1111\","
        "\"toBlock\":"
        "\"2222\",\"addresses\":[],\"topics\":[]}";

    auto params = EventLogFilterParams::buildEventLogFilterParamsObject(json);

    BOOST_CHECK_EQUAL(params->getFilterID(), "adfsflkjklj9poi9aa");
    BOOST_CHECK_EQUAL(params->getFromBlock(), int64_t(1111));
    BOOST_CHECK_EQUAL(params->getToBlock(), int64_t(2222));
    BOOST_CHECK_EQUAL(params->getAddresses().size(), 0);

    BOOST_CHECK_EQUAL(params->getTopics()[0].size(), 0);
    BOOST_CHECK_EQUAL(params->getTopics()[1].size(), 0);
    BOOST_CHECK_EQUAL(params->getTopics()[2].size(), 0);
    BOOST_CHECK_EQUAL(params->getTopics()[3].size(), 0);

    EventLogFilter::Ptr filter = std::make_shared<EventLogFilter>(params, 0, 0);

    BOOST_CHECK_EQUAL(filter->getNextBlockToProcess(), 0);

    h256s topics0{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa6")};
    LogEntry _log0{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3a"), topics0, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log0), true);

    h256s topics1{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa6")};
    LogEntry _log1{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3b"), topics1, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log1), true);

    h256s topics2{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa6")};
    LogEntry _log2{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3c"), topics2, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log2), true);

    h256s topics3{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa7")};
    LogEntry _log3{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3a"), topics3, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log3), true);

    h256s topics4{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa7")};
    LogEntry _log4{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3b"), topics4, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log4), true);

    h256s topics5{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4")};
    LogEntry _log5{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3b"), topics4, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log5), true);

    h256s topics6{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa7")};
    LogEntry _log6{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3c"), topics5, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log6), true);

    h256s topics7{
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5"),
        jsToFixed<32>("0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa7")};
    LogEntry _log7{jsToAddress("0x692a70d2e424a56d2c6c27aa97d1a86395877b3d"), topics6, bytes{}};
    BOOST_CHECK_EQUAL(filter->matches(_log7), true);
}

BOOST_AUTO_TEST_CASE(EventLogFilterManager_test0)
{
    std::string json =
        "{\"groupID\":\"1\",\"filterID\":\"aaaa\",\"fromBlock\":\"1111\",\"toBlock\":\"2222\","
        "\"addresses\":["
        "\"0x692a70d2e424a56d2c6c27aa97d1a86395877b3a\","
        "\"0x692a70d2e424a56d2c6c27aa97d1a86395877b3b\","
        "\"0x692a70d2e424a56d2c6c27aa97d1a86395877b3c\"],\"topics\":["
        "\"0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa4\",null,"
        "\"0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa5\",["
        "\"0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa6\",null,"
        "\"0x1be7c4acf0f0eba0992603759c32d028600239a9034d28a643e234992e646aa7\"]]}";

    auto manager = std::make_shared<EventLogFilterManager>(nullptr, 1000, 1111);
    auto params = EventLogFilterParams::buildEventLogFilterParamsObject(json);
    auto filter = std::make_shared<EventLogFilter>(params, 11111, 0);

    BOOST_CHECK_EQUAL(manager->getMaxBlockRange(), 1000);
    BOOST_CHECK_EQUAL(manager->getMaxBlockPerFilter(), 1111);
    BOOST_CHECK_EQUAL(filter->getNextBlockToProcess(), 11111);

    manager->addEventLogFilter(filter);
    BOOST_CHECK_EQUAL(manager->getWaitAddCount(), 1);

    manager->addEventLogFilter(filter);
    BOOST_CHECK_EQUAL(manager->getWaitAddCount(), 2);

    manager->addEventLogFilter(filter);
    BOOST_CHECK_EQUAL(manager->getWaitAddCount(), 3);

    manager->addFilter();
    BOOST_CHECK_EQUAL(manager->filters().size(), 3);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev