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
 * @file AmopCmd.cpp
 */
#include "AmopCmd.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/Connect.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/Output.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <deque>
#include <mutex>
#include <ostream>

namespace bcos::ops
{
namespace
{
constexpr double c_amopDefaultWaitSeconds = 60;

std::atomic<bool> g_amopStop{false};

bool amopPrintable(bytes const& _data)
{
    return std::ranges::all_of(_data, [](uint8_t _byte) {
        return _byte == '\t' || _byte == '\n' || (_byte >= 0x20 && _byte < 0x7f) || _byte >= 0x80;
    });
}

void amopPrintMessage(std::ostream& _out, AmopMessage const& _message, bool _json)
{
    std::string text(_message.data.begin(), _message.data.end());
    if (_json)
    {
        Json::Value value;
        value["topic"] = _message.topic;
        value["from"] = _message.from;
        value["data"] = amopPrintable(_message.data) ? Json::Value(sanitizeForTerminal(text)) :
                                                       Json::Value(Json::nullValue);
        value["hex"] = "0x" + toHex(_message.data);
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        _out << Json::writeString(builder, value) << '\n' << std::flush;
        return;
    }
    _out << (amopPrintable(_message.data) ? sanitizeForTerminal(text) : "0x" + toHex(_message.data))
         << '\n'
         << std::flush;
}

int runAmopSub(AmopTransport& _transport, Args const& _args, std::ostream& _out, std::ostream& _err,
    std::atomic<bool> const& _stop)
{
    if (_args.positionals().size() < 2)
    {
        throw OpsError(c_exitUsage, "usage: amop sub <topic> [--count N] [--wait seconds]");
    }
    auto const& topic = _args.positionals()[1];
    auto count = _args.numberOr<int64_t>("count", 1);
    if (count < 0)
    {
        throw OpsError(c_exitUsage, "--count must be >= 0 (0 = until interrupted)");
    }
    auto waitSeconds = _args.numberOr<double>("wait", c_amopDefaultWaitSeconds);
    bool deadlineApplies = count > 0 || _args.option("wait").has_value();
    bool json = wantJson(_args.flag("json"));

    std::mutex mutex;
    std::condition_variable arrived;
    std::deque<AmopMessage> queue;
    _transport.subscribe(topic, [&](AmopMessage _message) {
        // the publisher waits for this reply; echo so `amop pub` sees a round trip
        if (_message.reply)
        {
            _message.reply(bytesConstRef(_message.data.data(), _message.data.size()));
        }
        std::lock_guard<std::mutex> lock(mutex);
        queue.push_back(std::move(_message));
        arrived.notify_all();
    });
    _err << "subscribed " << topic << '\n' << std::flush;

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(static_cast<int64_t>(waitSeconds * 1000));
    int64_t received = 0;
    while (true)
    {
        std::deque<AmopMessage> batch;
        {
            std::unique_lock<std::mutex> lock(mutex);
            arrived.wait_for(
                lock, std::chrono::milliseconds(200), [&]() { return !queue.empty(); });
            batch.swap(queue);
        }
        for (auto const& message : batch)
        {
            amopPrintMessage(_out, message, json);
            ++received;
            if (count > 0 && received >= count)
            {
                return c_exitOk;
            }
        }
        if (_stop.load())
        {
            return c_exitOk;
        }
        if (deadlineApplies && std::chrono::steady_clock::now() >= deadline)
        {
            _err << "timeout: received " << received << " of " << count << " messages on " << topic
                 << '\n';
            return c_exitChecksFailed;
        }
    }
}

int runAmopPub(AmopTransport& _transport, Args const& _args, std::ostream& _out, std::ostream& _err)
{
    if (_args.positionals().size() < 3)
    {
        throw OpsError(c_exitUsage, "usage: amop pub <topic> <message> [--wait seconds]");
    }
    auto const& topic = _args.positionals()[1];
    auto const& text = _args.positionals()[2];
    auto waitMs = static_cast<int>(_args.numberOr<double>("wait", 10) * 1000);
    bool json = wantJson(_args.flag("json"));
    auto result = _transport.publish(
        topic, bytesConstRef(reinterpret_cast<uint8_t const*>(text.data()), text.size()), waitMs);
    if (!result.ok)
    {
        if (json)
        {
            Json::Value value;
            value["error"]["code"] = result.code;
            value["error"]["message"] = result.message;
            value["error"]["topic"] = topic;
            printJson(_out, value);
        }
        else
        {
            _err << "publish to " << topic << " failed: code=" << result.code << " "
                 << result.message << '\n';
        }
        return c_exitChecksFailed;
    }
    std::string response(result.response.begin(), result.response.end());
    if (json)
    {
        Json::Value value;
        value["ok"] = true;
        value["topic"] = topic;
        value["response"] = amopPrintable(result.response) ?
                                Json::Value(sanitizeForTerminal(response)) :
                                Json::Value(Json::nullValue);
        value["responseHex"] = "0x" + toHex(result.response);
        printJson(_out, value);
    }
    else
    {
        _out << "response "
             << (amopPrintable(result.response) ? sanitizeForTerminal(response) :
                                                  "0x" + toHex(result.response))
             << '\n';
    }
    return c_exitOk;
}

int runAmop(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    if (_args.positionals().empty() ||
        (_args.positionals()[0] != "sub" && _args.positionals()[0] != "pub"))
    {
        throw OpsError(c_exitUsage, "usage: amop sub <topic> [--count N] | amop pub <topic> <msg>");
    }
    auto options = connectOptionsFrom(_args);
    options.allowIpc = false;  // AMOP rides the WebSocket RPC, not the local socket
    auto connection = connect(options);
    auto transport = makeAmopTransport(connection);
    std::signal(SIGINT, [](int) { g_amopStop.store(true); });
    return runAmopOn(*transport, _args, _out, _err, g_amopStop);
}
}  // namespace

int runAmopOn(AmopTransport& _transport, Args const& _args, std::ostream& _out, std::ostream& _err,
    std::atomic<bool> const& _stop)
{
    if (_args.positionals().empty())
    {
        throw OpsError(c_exitUsage, "usage: amop sub <topic> [--count N] | amop pub <topic> <msg>");
    }
    if (_args.positionals()[0] == "sub")
    {
        return runAmopSub(_transport, _args, _out, _err, _stop);
    }
    if (_args.positionals()[0] == "pub")
    {
        return runAmopPub(_transport, _args, _out, _err);
    }
    throw OpsError(c_exitUsage, "unknown amop subcommand: " + _args.positionals()[0]);
}

void registerAmopCommand()
{
    registerCommand("amop",
        Command{"AMOP subscribe/publish over the node's WebSocket RPC (java-sdk AmopTest; no "
                "console equivalent). sub echoes each message back to the publisher",
            "sub <topic> [--count N (0 = until Ctrl-C)] [--wait seconds (default 60)] | "
            "pub <topic> <message> [--wait seconds (default 10)]   [--node-dir <dir> | --rpc "
            "<host:port>] [--json]",
            {}, runAmop});
}
}  // namespace bcos::ops
