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
#include <optional>
#include <ostream>

namespace bcos::ops
{
namespace
{
constexpr double c_amopDefaultWaitSeconds = 60;

std::atomic<bool> g_amopStop{false};

/// text = printable: well-formed UTF-8 whose only control bytes are TAB and LF
bool amopIsText(bytes const& _data)
{
    size_t i = 0;
    while (i < _data.size())
    {
        uint8_t byte = _data[i];
        if (byte < 0x80)
        {
            if ((byte < 0x20 && byte != '\t' && byte != '\n') || byte == 0x7f)
            {
                return false;
            }
            ++i;
            continue;
        }
        size_t length = byte >= 0xf0 ? 4 : byte >= 0xe0 ? 3 : byte >= 0xc2 ? 2 : 0;
        if (length == 0 || byte > 0xf4 || i + length > _data.size())
        {
            return false;  // continuation byte in lead position, overlong, or truncated
        }
        for (size_t k = 1; k < length; ++k)
        {
            if ((_data[i + k] & 0xc0) != 0x80)
            {
                return false;
            }
        }
        i += length;
    }
    return true;
}

struct AmopRendered
{
    std::optional<std::string> text;  // nullopt when the payload is not text
    std::string hex;                  // always 0x…
};

/// one rule for every payload the command prints: text when UTF-8, else only hex. The text
/// path keeps TAB and LF (sanitizeForTerminal drops LF); _forTerminal renders LF as the two
/// characters `\n` so a multi-line payload stays one output line
AmopRendered amopRender(bytes const& _data, bool _forTerminal)
{
    AmopRendered rendered;
    rendered.hex = "0x" + toHex(_data);
    if (!amopIsText(_data))
    {
        return rendered;
    }
    std::string text;
    text.reserve(_data.size());
    for (uint8_t byte : _data)
    {
        if (byte == '\n' && _forTerminal)
        {
            text += "\\n";
        }
        else
        {
            text.push_back(static_cast<char>(byte));
        }
    }
    rendered.text = text;
    return rendered;
}

void amopPrintMessage(std::ostream& _out, AmopMessage const& _message, bool _json)
{
    auto rendered = amopRender(_message.data, !_json);
    if (_json)
    {
        Json::Value value;
        value["topic"] = _message.topic;
        value["from"] = _message.from;
        value["data"] = rendered.text ? Json::Value(*rendered.text) : Json::Value(Json::nullValue);
        value["hex"] = rendered.hex;
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        _out << Json::writeString(builder, value) << '\n' << std::flush;
        return;
    }
    _out << rendered.text.value_or(rendered.hex) << '\n' << std::flush;
}

/// everything the io thread and the command thread share; owned by both through a shared_ptr so
/// a delivery that lands after the loop returned (the SDK has no unsubscribe) finds `closed`
/// instead of freed stack memory
struct AmopSubState
{
    std::mutex mutex;
    std::condition_variable arrived;
    std::deque<AmopMessage> queue;
    bool closed = false;
};

double amopWaitSeconds(Args const& _args, double _default)
{
    auto seconds = _args.numberOr<double>("wait", _default);
    if (!(seconds > 0))
    {
        throw OpsError(c_exitUsage, "--wait must be a positive number of seconds");
    }
    return seconds;
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
    auto waitSeconds = amopWaitSeconds(_args, c_amopDefaultWaitSeconds);
    bool deadlineApplies = count > 0 || _args.option("wait").has_value();
    bool json = wantJson(_args.flag("json"));

    auto state = std::make_shared<AmopSubState>();
    _transport.subscribe(topic, [state](AmopMessage _message) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->closed)
        {
            return;  // the command already returned; nobody reads or answers any more
        }
        // the publisher waits for this reply; echo so `amop pub` sees a round trip
        if (_message.reply)
        {
            _message.reply(bytesConstRef(_message.data.data(), _message.data.size()));
        }
        state->queue.push_back(std::move(_message));
        state->arrived.notify_all();
    });
    _err << "subscribed " << topic << '\n' << std::flush;

    auto finish = [&](int _code) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->closed = true;
        return _code;
    };
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(static_cast<int64_t>(waitSeconds * 1000));
    int64_t received = 0;
    while (true)
    {
        std::deque<AmopMessage> batch;
        {
            std::unique_lock<std::mutex> lock(state->mutex);
            state->arrived.wait_for(
                lock, std::chrono::milliseconds(200), [&]() { return !state->queue.empty(); });
            batch.swap(state->queue);
        }
        for (auto const& message : batch)
        {
            amopPrintMessage(_out, message, json);
            ++received;
            if (count > 0 && received >= count)
            {
                return finish(c_exitOk);
            }
        }
        bool stopped = _stop.load();
        if (stopped && count == 0)
        {
            return finish(c_exitOk);
        }
        if (stopped || (deadlineApplies && std::chrono::steady_clock::now() >= deadline))
        {
            // interrupted before N arrived counts as the deadline: a scripted
            // `timeout -s INT … sub --count 1` must not report success with zero messages
            _err << (stopped ? "interrupted" : "timeout") << ": received " << received << " of "
                 << count << " messages on " << topic << '\n';
            return finish(c_exitChecksFailed);
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
    auto waitMs = static_cast<int>(amopWaitSeconds(_args, 10) * 1000);
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
                 << sanitizeForTerminal(result.message) << '\n';
        }
        return c_exitChecksFailed;
    }
    auto rendered = amopRender(result.response, !json);
    if (json)
    {
        Json::Value value;
        value["ok"] = true;
        value["topic"] = topic;
        value["response"] =
            rendered.text ? Json::Value(*rendered.text) : Json::Value(Json::nullValue);
        value["responseHex"] = rendered.hex;
        printJson(_out, value);
    }
    else
    {
        _out << "response " << rendered.text.value_or(rendered.hex) << '\n';
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
            {}, runAmop, {"count", "wait"}, true});
}
}  // namespace bcos::ops
