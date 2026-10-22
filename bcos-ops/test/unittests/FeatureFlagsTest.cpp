/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file FeatureFlagsTest.cpp
 */
#include "bcos-ops/tx/FeatureFlags.h"
#include "bcos-ops/OpsError.h"
#include <boost/test/unit_test.hpp>
#include <map>

namespace bcos::ops::test
{
namespace
{
/// getSystemConfigByKey answering from a key → value map; unknown keys fail like the node does
RpcCall featureKeyedRpc(std::map<std::string, std::string> const& _values)
{
    return [&_values](std::string_view _method, Json::Value const& _params) -> Json::Value {
        if (_method != "getSystemConfigByKey")
        {
            throw OpsError(c_exitUsage, std::string(_method) + " failed: no connection");
        }
        auto it = _values.find(_params[2].asString());
        if (it == _values.end())
        {
            // Ledger::asyncGetSystemConfigByKey: no row is GetStorageError
            throw OpsRpcError(
                "getSystemConfigByKey", 3008, "getSystemConfig failed for empty entry");
        }
        Json::Value result;
        result["value"] = it->second;
        result["blockNumber"] = 1;
        return result;
    };
}

class FeatureRecordingSender : public Sender
{
public:
    std::vector<std::string> sentTo;
    std::vector<bytes> sentData;
    int32_t statusToReturn = 0;
    Receipt send(std::string const& _to, bytes _data, std::string const&) override
    {
        sentTo.push_back(_to);
        sentData.push_back(std::move(_data));
        Receipt receipt;
        receipt.status = statusToReturn;
        receipt.txHash = "0x" + std::to_string(sentTo.size());
        receipt.output = "0x0000000000000000000000000000000000000000000000000000000000000000";
        return receipt;
    }
    Receipt call(std::string const&, bytes) override { return Receipt{}; }
};

size_t featureIndexOf(std::vector<std::string> const& _list, std::string const& _name)
{
    return static_cast<size_t>(std::find(_list.begin(), _list.end(), _name) - _list.begin());
}
}  // namespace

BOOST_AUTO_TEST_SUITE(FeatureFlagsTest)

BOOST_AUTO_TEST_CASE(namesComeFromTheLedgerEnum)
{
    auto names = featureNames();
    BOOST_CHECK(names.size() > 50);
    BOOST_CHECK(featureIndexOf(names, "feature_balance") < names.size());
    BOOST_CHECK(featureIndexOf(names, "bugfix_revert") < names.size());
    // reserved placeholders are in Features::featureKeys() but must not be offered
    BOOST_CHECK_EQUAL(featureIndexOf(names, "reserved_removed_op_jovian"), names.size());
    BOOST_CHECK_EQUAL(featureIndexOf(names, "reserved_removed_l2_ethereum_compat"), names.size());
    for (auto const& name : names)
    {
        BOOST_CHECK(name.starts_with("feature_") || name.starts_with("bugfix_"));
    }
}

BOOST_AUTO_TEST_CASE(enableOrderPutsDependenciesFirst)
{
    auto order = featureEnableOrder({"feature_balance_policy1"});
    BOOST_REQUIRE_EQUAL(order.size(), 3U);
    BOOST_CHECK_EQUAL(order[0], "feature_balance");
    BOOST_CHECK_EQUAL(order[1], "feature_balance_precompiled");
    BOOST_CHECK_EQUAL(order[2], "feature_balance_policy1");
    // requested order does not matter, duplicates collapse
    auto mixed = featureEnableOrder(
        {"feature_balance_precompiled", "feature_balance", "feature_balance_precompiled"});
    BOOST_REQUIRE_EQUAL(mixed.size(), 2U);
    BOOST_CHECK_EQUAL(mixed[0], "feature_balance");
    BOOST_CHECK_THROW(featureEnableOrder({"feature_nope"}), OpsError);
    BOOST_CHECK(featureDependencies("bugfix_revert").empty());
}

BOOST_AUTO_TEST_CASE(readStatesMarksOnOffAndDeprecated)
{
    std::map<std::string, std::string> values = {{"feature_balance", "1"}};
    auto states = readFeatureStates(featureKeyedRpc(values), "group0");
    BOOST_CHECK_EQUAL(states.size(), featureNames().size());
    for (auto const& state : states)
    {
        if (state.name == "feature_balance")
        {
            BOOST_CHECK(state.enabled);
        }
        else
        {
            BOOST_CHECK(!state.enabled);
        }
        BOOST_CHECK_EQUAL(state.deprecated, state.name == "feature_raw_address");
    }
}

// only "no row" (3008) and "enabled from a later block" (3003) read as off; a timeout or a closed
// socket must fail the read, or every flag shows off and enable re-sends what is already on
BOOST_AUTO_TEST_CASE(readStatesFailsOnATransportError)
{
    RpcCall timedOut = [](std::string_view, Json::Value const&) -> Json::Value {
        throw OpsError(c_exitUsage, "getSystemConfigByKey timed out after 15000 ms");
    };
    BOOST_CHECK_THROW(readFeatureStates(timedOut, "group0"), OpsError);
    RpcCall otherCode = [](std::string_view, Json::Value const&) -> Json::Value {
        throw OpsRpcError("getSystemConfigByKey", -32603, "internal error");
    };
    BOOST_CHECK_THROW(readFeatureStates(otherCode, "group0"), OpsRpcError);
    RpcCall notYet = [](std::string_view, Json::Value const&) -> Json::Value {
        throw OpsRpcError("getSystemConfigByKey", 3003, "Config not available");
    };
    for (auto const& state : readFeatureStates(notYet, "group0"))
    {
        BOOST_CHECK(!state.enabled);
    }
}

BOOST_AUTO_TEST_CASE(enableSkipsAlreadyOnAndSendsTheRest)
{
    std::map<std::string, std::string> values = {{"feature_balance", "1"}};
    FeatureRecordingSender sender;
    PresetRunner runner(sender, false);
    auto steps =
        enableFeatures(featureKeyedRpc(values), "group0", runner, {"feature_balance_policy1"});
    BOOST_REQUIRE_EQUAL(steps.size(), 3U);
    BOOST_CHECK_EQUAL(steps[0].name, "feature_balance");
    BOOST_CHECK_EQUAL(steps[0].action, "skipped");
    BOOST_CHECK_EQUAL(steps[1].action, "enabled");
    BOOST_CHECK_EQUAL(steps[2].action, "enabled");
    BOOST_REQUIRE_EQUAL(sender.sentTo.size(), 2U);
    BOOST_CHECK_EQUAL(sender.sentTo[0], "0x0000000000000000000000000000000000001000");
    BOOST_CHECK_EQUAL(steps[1].result->receipt.txHash, "0x1");
    BOOST_CHECK_EQUAL(steps[1].toJson()["result"]["mode"].asString(), "direct");
}

BOOST_AUTO_TEST_CASE(enableStopsAtTheFirstFailure)
{
    std::map<std::string, std::string> values;
    FeatureRecordingSender sender;
    sender.statusToReturn = 16;
    PresetRunner runner(sender, false);
    auto steps =
        enableFeatures(featureKeyedRpc(values), "group0", runner, {"feature_balance_policy1"});
    BOOST_REQUIRE_EQUAL(steps.size(), 1U);
    BOOST_CHECK_EQUAL(steps[0].name, "feature_balance");
    BOOST_CHECK_EQUAL(steps[0].action, "failed");
    BOOST_CHECK_EQUAL(sender.sentTo.size(), 1U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
