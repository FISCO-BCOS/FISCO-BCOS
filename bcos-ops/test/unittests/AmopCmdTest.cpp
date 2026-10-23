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
 * @brief amop sub/pub exit conditions and output against a scripted transport
 * @file AmopCmdTest.cpp
 */
#include "bcos-ops/cmd/AmopCmd.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/RpcCall.h"
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <functional>
#include <sstream>
#include <thread>
#include <vector>

namespace bcos::ops::test
{
namespace
{
bytes bytesOf(std::string const& _text)
{
    return bytes(_text.begin(), _text.end());
}

/// delivers the scripted messages synchronously inside subscribe(); publish answers from a
/// script too
class FakeAmop : public AmopTransport
{
public:
    std::vector<std::string> deliver;  // data of the messages handed to the subscriber
    std::vector<std::string> queued;   // replies made but not drained, like the SDK's queue
    std::vector<std::string> replies;  // what the subscriber echoed back, once drained
    std::vector<std::string> events;   // "reply:<data>" / "drain", in order
    std::chrono::milliseconds drainLimit{0};
    std::string subscribedTopic;
    std::atomic<bool>* stopAfterDelivery = nullptr;
    std::function<void(AmopMessage)> kept;  // the SDK keeps the callback for ever; so do we
    AmopPublishResult publishResult;
    std::string publishedTopic;
    std::string publishedData;
    int publishedTimeoutMs = 0;

    void subscribe(std::string const& _topic, std::function<void(AmopMessage)> _onMessage) override
    {
        subscribedTopic = _topic;
        kept = _onMessage;
        for (auto const& text : deliver)
        {
            AmopMessage message;
            message.topic = _topic;
            message.from = "127.0.0.1:20200";
            message.data = bytesOf(text);
            message.reply = [this](bytesConstRef _reply) {
                queued.emplace_back(_reply.begin(), _reply.end());
                events.push_back("reply:" + queued.back());
            };
            _onMessage(std::move(message));
        }
        if (stopAfterDelivery != nullptr)
        {
            stopAfterDelivery->store(true);
        }
    }
    AmopPublishResult publish(
        std::string const& _topic, bytesConstRef _data, int _timeoutMs) override
    {
        publishedTopic = _topic;
        publishedData.assign(_data.begin(), _data.end());
        publishedTimeoutMs = _timeoutMs;
        return publishResult;
    }
    void drainReplies(std::chrono::milliseconds _limit) override
    {
        drainLimit = _limit;
        events.emplace_back("drain");
        replies.insert(replies.end(), queued.begin(), queued.end());
        queued.clear();
    }
};

Args amopArgs(std::vector<std::string> _argv)
{
    return Args(std::move(_argv), {"json"});
}

/// stdout is not a terminal under ctest, so every message is a JSON line; collect the data fields
std::vector<std::string> dataLines(std::string const& _out)
{
    std::vector<std::string> data;
    std::istringstream lines(_out);
    std::string line;
    while (std::getline(lines, line))
    {
        data.push_back(parseJson(line, "line")["data"].asString());
    }
    return data;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(AmopCmdTest)

BOOST_AUTO_TEST_CASE(subExitsZeroAfterCountMessagesAndEchoesEach)
{
    FakeAmop fake;
    fake.deliver = {"hello", "world", "extra"};
    std::atomic<bool> stop{false};
    std::ostringstream out;
    std::ostringstream err;
    auto code = runAmopOn(fake, amopArgs({"sub", "t1", "--count", "2"}), out, err, stop);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    BOOST_CHECK_EQUAL(fake.subscribedTopic, "t1");
    BOOST_CHECK((dataLines(out.str()) == std::vector<std::string>{"hello", "world"}));
    BOOST_CHECK(err.str().find("subscribed t1") != std::string::npos);
    BOOST_REQUIRE_EQUAL(fake.replies.size(), 3U);  // every delivery is answered, printed or not
    BOOST_CHECK_EQUAL(fake.replies[0], "hello");
}

// #5722: reply() only queues the response and the process exits once sub returns, so the
// replies are drained after the last one is made and before the command returns
BOOST_AUTO_TEST_CASE(subDrainsItsRepliesBeforeReturning)
{
    FakeAmop fake;
    fake.deliver = {"hello"};
    std::atomic<bool> stop{false};
    std::ostringstream out;
    std::ostringstream err;
    auto code = runAmopOn(fake, amopArgs({"sub", "t1", "--count", "1"}), out, err, stop);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    BOOST_CHECK((fake.events == std::vector<std::string>{"reply:hello", "drain"}));
    BOOST_CHECK(fake.queued.empty());
    BOOST_CHECK(fake.drainLimit > std::chrono::milliseconds(0));
    // a sub that times out drains what it answered too
    FakeAmop late;
    late.deliver = {"only"};
    code =
        runAmopOn(late, amopArgs({"sub", "t1", "--count", "2", "--wait", "0.2"}), out, err, stop);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    BOOST_CHECK((late.events == std::vector<std::string>{"reply:only", "drain"}));
}

BOOST_AUTO_TEST_CASE(subJsonPrintsOneObjectPerMessage)
{
    FakeAmop fake;
    fake.deliver = {"hi", std::string("\x01\x02", 2)};
    std::atomic<bool> stop{false};
    std::ostringstream out;
    std::ostringstream err;
    auto code = runAmopOn(fake, amopArgs({"sub", "t1", "--count", "2", "--json"}), out, err, stop);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    std::istringstream lines(out.str());
    std::string line;
    BOOST_REQUIRE(std::getline(lines, line));
    auto first = parseJson(line, "line");
    BOOST_CHECK_EQUAL(first["topic"].asString(), "t1");
    BOOST_CHECK_EQUAL(first["data"].asString(), "hi");
    BOOST_CHECK_EQUAL(first["hex"].asString(), "0x6869");
    BOOST_REQUIRE(std::getline(lines, line));
    auto second = parseJson(line, "line");
    BOOST_CHECK(second["data"].isNull());  // control bytes are not text
    BOOST_CHECK_EQUAL(second["hex"].asString(), "0x0102");
}

BOOST_AUTO_TEST_CASE(subTimesOutWithExitTwoWhenFewerArrive)
{
    FakeAmop fake;
    fake.deliver = {"only-one"};
    std::atomic<bool> stop{false};
    std::ostringstream out;
    std::ostringstream err;
    auto code =
        runAmopOn(fake, amopArgs({"sub", "t1", "--count", "2", "--wait", "0.3"}), out, err, stop);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    BOOST_CHECK((dataLines(out.str()) == std::vector<std::string>{"only-one"}));
    BOOST_CHECK(err.str().find("timeout: received 1 of 2") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(stopBeforeCountIsReachedIsExitTwo)
{
    FakeAmop fake;
    fake.deliver = {"only-one"};
    std::atomic<bool> stop{false};
    fake.stopAfterDelivery = &stop;  // SIGINT after the first of two expected messages
    std::ostringstream out;
    std::ostringstream err;
    auto code = runAmopOn(fake, amopArgs({"sub", "t1", "--count", "2"}), out, err, stop);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    BOOST_CHECK(err.str().find("interrupted: received 1 of 2") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(deliveryAfterTheLoopReturnedIsIgnored)
{
    FakeAmop fake;
    fake.deliver = {"first"};
    std::atomic<bool> stop{false};
    std::ostringstream out;
    std::ostringstream err;
    BOOST_CHECK_EQUAL(
        runAmopOn(fake, amopArgs({"sub", "t1", "--count", "1"}), out, err, stop), c_exitOk);
    BOOST_REQUIRE_EQUAL(fake.replies.size(), 1U);
    // the io thread hands over a second message once runAmopSub's frame is gone
    AmopMessage late;
    late.topic = "t1";
    late.data = bytesOf("late");
    late.reply = [&fake](bytesConstRef _reply) {
        fake.replies.emplace_back(_reply.begin(), _reply.end());
    };
    fake.kept(std::move(late));
    BOOST_CHECK_EQUAL(fake.replies.size(), 1U);  // neither answered nor printed
    BOOST_CHECK((dataLines(out.str()) == std::vector<std::string>{"first"}));
}

BOOST_AUTO_TEST_CASE(waitMustBePositiveAndAtMostAnHour)
{
    FakeAmop fake;
    std::atomic<bool> stop{false};
    std::ostringstream out;
    std::ostringstream err;
    for (auto const& wait : {"0", "-1", "-0.5", "3600.5", "1e12"})
    {
        BOOST_CHECK_THROW(
            runAmopOn(fake, amopArgs({"sub", "t1", "--wait", wait}), out, err, stop), OpsError);
        BOOST_CHECK_THROW(
            runAmopOn(fake, amopArgs({"pub", "t1", "m", "--wait", wait}), out, err, stop),
            OpsError);
    }
    BOOST_CHECK(fake.subscribedTopic.empty());
    BOOST_CHECK(fake.publishedTopic.empty());
}

BOOST_AUTO_TEST_CASE(binaryAndMultilinePayloadsRender)
{
    FakeAmop fake;
    fake.deliver = {"a\nb", std::string("\xff\xfe", 2), "caf\xc3\xa9"};
    std::atomic<bool> stop{false};
    std::ostringstream out;
    std::ostringstream err;
    BOOST_CHECK_EQUAL(
        runAmopOn(fake, amopArgs({"sub", "t1", "--count", "3"}), out, err, stop), c_exitOk);
    std::vector<Json::Value> lines;
    std::istringstream in(out.str());
    std::string line;
    while (std::getline(in, line))
    {
        lines.push_back(parseJson(line, "line"));
    }
    BOOST_REQUIRE_EQUAL(lines.size(), 3U);
    BOOST_CHECK_EQUAL(lines[0]["data"].asString(), "a\nb");  // LF kept in JSON
    BOOST_CHECK(lines[1]["data"].isNull());                  // not UTF-8: hex only
    BOOST_CHECK_EQUAL(lines[1]["hex"].asString(), "0xfffe");
    BOOST_CHECK_EQUAL(lines[2]["data"].asString(), "caf\xc3\xa9");
}

BOOST_AUTO_TEST_CASE(subCountZeroRunsUntilStopped)
{
    FakeAmop fake;
    fake.deliver = {"a", "b"};
    std::atomic<bool> stop{false};
    fake.stopAfterDelivery = &stop;  // stands in for SIGINT
    std::ostringstream out;
    std::ostringstream err;
    auto code = runAmopOn(fake, amopArgs({"sub", "t1", "--count", "0"}), out, err, stop);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    BOOST_CHECK((dataLines(out.str()) == std::vector<std::string>{"a", "b"}));
}

BOOST_AUTO_TEST_CASE(pubRefusedByTheNodeIsExitTwoWithErrorJson)
{
    FakeAmop fake;
    fake.publishResult.ok = false;
    fake.publishResult.code = 3001;  // CommonError::NotFoundPeerByTopicSendMsg
    fake.publishResult.message = "there has no node subscribe this topic, topic: t2";
    std::atomic<bool> stop{false};
    std::ostringstream out;
    std::ostringstream err;
    auto code = runAmopOn(fake, amopArgs({"pub", "t2", "nobody", "--json"}), out, err, stop);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    auto json = parseJson(out.str(), "out");
    BOOST_CHECK_EQUAL(json["error"]["code"].asInt(), 3001);
    BOOST_CHECK(json["error"]["message"].asString().find("no node subscribe") != std::string::npos);
    BOOST_CHECK_EQUAL(fake.publishedTopic, "t2");
    BOOST_CHECK_EQUAL(fake.publishedData, "nobody");
    BOOST_CHECK_EQUAL(fake.publishedTimeoutMs, 10000);
}

BOOST_AUTO_TEST_CASE(pubAnsweredIsExitZeroWithTheReply)
{
    FakeAmop fake;
    fake.publishResult.ok = true;
    fake.publishResult.response = bytesOf("hello");
    std::atomic<bool> stop{false};
    std::ostringstream out;
    std::ostringstream err;
    auto code =
        runAmopOn(fake, amopArgs({"pub", "t1", "hello", "--wait", "2", "--json"}), out, err, stop);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    auto json = parseJson(out.str(), "out");
    BOOST_CHECK(json["ok"].asBool());
    BOOST_CHECK_EQUAL(json["response"].asString(), "hello");
    BOOST_CHECK_EQUAL(fake.publishedTimeoutMs, 2000);
}

BOOST_AUTO_TEST_CASE(usageErrors)
{
    FakeAmop fake;
    std::atomic<bool> stop{false};
    std::ostringstream out;
    std::ostringstream err;
    BOOST_CHECK_THROW(runAmopOn(fake, amopArgs({"sub"}), out, err, stop), OpsError);
    BOOST_CHECK_THROW(runAmopOn(fake, amopArgs({"pub", "t1"}), out, err, stop), OpsError);
    BOOST_CHECK_THROW(
        runAmopOn(fake, amopArgs({"sub", "t1", "--count", "-1"}), out, err, stop), OpsError);
    BOOST_CHECK_THROW(runAmopOn(fake, amopArgs({"nope"}), out, err, stop), OpsError);
    auto code = runOps({"fisco-bcos", "amop", "--help"}, out, err);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    BOOST_CHECK(out.str().find("AmopTest") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
