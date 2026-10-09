/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-ops/OpsError.h>
#include <bcos-ops/tx/Smoke.h>
#include <boost/test/unit_test.hpp>
#include <deque>

namespace bcos::ops::test
{
namespace
{
Receipt receipt(int32_t _status, std::string _hash, std::string _contract = "",
    std::string _output = "", std::string _message = "")
{
    Receipt r;
    r.status = _status;
    r.txHash = std::move(_hash);
    r.contractAddress = std::move(_contract);
    r.output = std::move(_output);
    r.message = std::move(_message);
    r.gasUsed = "6d6c";
    return r;
}

std::string const c_helloOutput =
    "0x0000000000000000000000000000000000000000000000000000000000000020"
    "0000000000000000000000000000000000000000000000000000000000000011"
    "48656c6c6f2c20464953434f2042434f53000000000000000000000000000000";

/// scripted sender: hands out receipts in order and records what it was asked
class FakeSender : public Sender
{
public:
    std::deque<Receipt> sendReceipts;
    std::deque<Receipt> callReceipts;
    std::vector<std::string> sentTo;
    std::vector<std::string> calledTo;
    Receipt send(std::string const& _to, bytes, std::string const&) override
    {
        sentTo.push_back(_to);
        auto r = sendReceipts.front();
        sendReceipts.pop_front();
        return r;
    }
    Receipt call(std::string const& _to, bytes) override
    {
        calledTo.push_back(_to);
        auto r = callReceipts.front();
        callReceipts.pop_front();
        return r;
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(SmokeFlowTest)

BOOST_AUTO_TEST_CASE(threeStepsSucceed)
{
    FakeSender sender;
    sender.sendReceipts = {receipt(0, "0x3f", "0xa1"), receipt(0, "0x9c")};
    sender.callReceipts = {receipt(0, "", "", c_helloOutput)};
    auto result = runSmoke(sender, false, false);
    BOOST_CHECK(result.ok);
    BOOST_CHECK_EQUAL(result.exitCode(), c_exitOk);
    BOOST_REQUIRE_EQUAL(result.steps.size(), 3U);
    BOOST_CHECK_EQUAL(result.steps[0].name, "deploy");
    BOOST_CHECK(result.steps[0].detail.find("0xa1") != std::string::npos);
    BOOST_CHECK_EQUAL(result.steps[1].txHash, "0x9c");
    BOOST_CHECK(result.steps[2].detail.find("Hello, FISCO BCOS") != std::string::npos);
    BOOST_REQUIRE_EQUAL(sender.sentTo.size(), 2U);
    BOOST_CHECK_EQUAL(sender.sentTo[0], "");      // deploy
    BOOST_CHECK_EQUAL(sender.sentTo[1], "0xa1");  // set goes to the new contract
    BOOST_CHECK_EQUAL(sender.calledTo[0], "0xa1");
    auto json = result.toJson();
    BOOST_CHECK_EQUAL(json["steps"].size(), 3U);
    BOOST_CHECK_EQUAL(json["steps"][1]["txHash"].asString(), "0x9c");
}

BOOST_AUTO_TEST_CASE(secondStepFailureStopsBeforeThird)
{
    FakeSender sender;
    sender.sendReceipts = {receipt(0, "0x3f", "0xa1"), receipt(16, "0x9c", "", "", "revert")};
    sender.callReceipts = {receipt(0, "", "", c_helloOutput)};
    auto result = runSmoke(sender, false, false);
    BOOST_CHECK(!result.ok);
    BOOST_CHECK_EQUAL(result.reason, "set_failed");
    BOOST_CHECK_EQUAL(result.exitCode(), c_exitChecksFailed);
    BOOST_CHECK_EQUAL(result.steps.size(), 2U);
    BOOST_CHECK(sender.calledTo.empty());
}

BOOST_AUTO_TEST_CASE(permissionDeniedDeployIsAuthDenied)
{
    FakeSender sender;
    sender.sendReceipts = {receipt(18, "0x3f")};  // TransactionStatus::PermissionDenied
    auto result = runSmoke(sender, false, std::nullopt);
    BOOST_CHECK(!result.ok);
    BOOST_CHECK_EQUAL(result.reason, "auth_denied");
    BOOST_CHECK_EQUAL(result.exitCode(), c_exitChecksFailed);
    BOOST_CHECK_EQUAL(result.steps.size(), 1U);
}

BOOST_AUTO_TEST_CASE(authChainUnexplainedDeployFailureIsAuthDenied)
{
    FakeSender sender;
    sender.sendReceipts = {receipt(1, "0x3f")};
    BOOST_CHECK_EQUAL(runSmoke(sender, false, true).reason, "auth_denied");
    FakeSender plain;
    plain.sendReceipts = {receipt(1, "0x3f")};
    BOOST_CHECK_EQUAL(runSmoke(plain, false, false).reason, "deploy_failed");
}

BOOST_AUTO_TEST_CASE(getMismatchFails)
{
    FakeSender sender;
    sender.sendReceipts = {receipt(0, "0x3f", "0xa1"), receipt(0, "0x9c")};
    sender.callReceipts = {receipt(0, "", "",
        "0x0000000000000000000000000000000000000000000000000000000000000020"
        "0000000000000000000000000000000000000000000000000000000000000003"
        "6e6f700000000000000000000000000000000000000000000000000000000000")};
    auto result = runSmoke(sender, false, false);
    BOOST_CHECK(!result.ok);
    BOOST_CHECK_EQUAL(result.reason, "get_failed");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
