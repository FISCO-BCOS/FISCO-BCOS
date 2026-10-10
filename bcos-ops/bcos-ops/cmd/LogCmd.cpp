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
 * @brief `fisco-bcos log status|tx|pbft|viewchange|sync|seal-stall|p2p|txsync`
 * @file LogCmd.cpp
 */
#include "bcos-ops/Args.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/Connect.h"
#include "bcos-ops/NodeDir.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/Output.h"
#include "bcos-ops/cmd/StatusCmd.h"
#include "bcos-ops/collect/LogCollector.h"
#include "bcos-ops/log/EventNames.h"
#include "bcos-ops/log/LogFiles.h"
#include "bcos-ops/log/Traces.h"
#include "bcos-ops/log/TxLookup.h"
#include <chrono>
#include <iomanip>
#include <map>
#include <ostream>

namespace bcos::ops
{
namespace
{
int64_t logNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

struct LogInput
{
    NodeDir node;
    std::vector<std::string> files;
    std::vector<Event> events;
};

LogInput loadEvents(
    Args const& _args, std::function<bool(Event const&)> const& _keep, bool _lastOfEachOnly)
{
    LogInput input;
    input.node = NodeDir::load(_args.optionOr("node-dir", "."));
    if (!input.node.logFormat.empty())
    {
        throw OpsError(c_exitUsage, "log.format is customized (" + input.node.logFormat +
                                        "); log commands only support the default format");
    }
    auto since = parseDurationMs(_args.optionOr("since", "1h"));
    input.files = selectLogFiles(input.node.logDir(), since, logNowMs());
    if (input.files.empty())
    {
        throw OpsError(c_exitUsage, "no log files in " + input.node.logDir() + " (--since " +
                                        _args.optionOr("since", "1h") + ")");
    }
    input.events = _lastOfEachOnly ? readLastEventOfEach(input.files, _keep) :
                                     readEventsMatching(input.files, _keep);
    return input;
}

void renderTrace(std::ostream& _out, TraceResult const& _trace, bool _json)
{
    if (_json)
    {
        printJson(_out, _trace.toJson());
        return;
    }
    if (!_trace.missing.empty())
    {
        _out << sanitizeForTerminal(_trace.missing) << '\n';
    }
    if (_trace.rows.empty())
    {
        return;
    }
    std::vector<size_t> widths(_trace.header.size(), 0);
    for (size_t i = 0; i < _trace.header.size(); ++i)
    {
        widths[i] = _trace.header[i].size();
    }
    for (auto const& row : _trace.rows)
    {
        for (size_t i = 0; i < row.size() && i < widths.size(); ++i)
        {
            widths[i] = std::max(widths[i], row[i].size());
        }
    }
    auto line = [&](std::vector<std::string> const& _cells) {
        for (size_t i = 0; i < _cells.size() && i < widths.size(); ++i)
        {
            _out << std::left << std::setw(static_cast<int>(widths[i]) + 2)
                 << sanitizeForTerminal(_cells[i]);
        }
        _out << '\n';
    };
    line(_trace.header);
    for (auto const& row : _trace.rows)
    {
        line(row);
    }
}

int runLogStatus(LogInput const& _input, Args const& _args, std::ostream& _out)
{
    auto thresholds = thresholdsFrom(_args);
    auto status = collectFromLog(_input.events, logNowMs(), thresholds.viewChangeWindowMs);
    status.txpoolLimit = static_cast<int64_t>(_input.node.txpoolLimit);
    status.reasons.erase("txpoolLimit");
    status.groupId = _input.node.groupId;
    status.reasons.erase("groupId");
    status.chainId = _input.node.chainId;
    status.reasons.erase("chainId");
    if (!status.consensusTimeoutMs && _input.node.consensusTimeoutMs)
    {
        status.consensusTimeoutMs = _input.node.consensusTimeoutMs;
        status.reasons.erase("consensusTimeoutMs");
    }
    auto checks = evaluate(status, thresholds, logNowMs());
    renderStatus(_out, status, checks, wantJson(_args.flag("json")));
    return anyFailed(checks) ? c_exitChecksFailed : c_exitOk;
}

int runLogTx(LogInput const& _input, Args const& _args, std::ostream& _out)
{
    if (_args.positionals().size() < 2)
    {
        throw OpsError(c_exitUsage, "usage: log tx <txHash>");
    }
    auto const& hash = _args.positionals()[1];
    auto trace = traceTx(_input.events, hash);
    bool json = wantJson(_args.flag("json"));
    std::optional<TxLookup> lookup;
    if (trace.rows.empty())
    {
        // the RPC is an addition, not a prerequisite: say what it found, never fail on it.
        // A connect failure and "the node has no receipt" are different answers.
        try
        {
            ConnectOptions options = connectOptionsFrom(_args);
            options.connectTimeoutMs = std::min(options.connectTimeoutMs, 2000);
            auto connection = connect(options);
            lookup = lookupTxOnChain(connection.call, connection.source, connection.group, hash);
        }
        catch (std::exception const& e)
        {
            lookup = TxLookup::unavailable(e.what());
        }
    }
    if (json)
    {
        auto root = trace.toJson();
        root["source"] = "log";
        if (lookup)
        {
            root["rpc"] = lookup->toJson();
        }
        printJson(_out, root);
    }
    else
    {
        renderTrace(_out, trace, false);
        if (lookup)
        {
            _out << sanitizeForTerminal(lookup->line()) << '\n';
        }
    }
    return c_exitOk;
}

int runLog(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    (void)_err;
    if (_args.positionals().empty())
    {
        throw OpsError(c_exitUsage,
            "usage: log status|tx <hash>|pbft <number>|viewchange|sync|seal-stall|p2p|txsync "
            "[--since 1h] [--last N] [--node-dir <dir>] [--json]");
    }
    auto const& sub = _args.positionals()[0];
    // each thread streams only its own event names out of the window (a DEBUG-level hour does
    // not have to fit in memory); status keeps the handful of lines it reads
    static std::map<std::string, std::vector<std::string_view>> const c_eventsOf = {
        {"status", {events::Report, "compatibilityVersion updated", events::SyncStarted,
                       events::SyncFinished, events::BlockApplied, events::BlockCommitted,
                       events::BlockStat, events::ViewChangeTriggered}},
        {"tx", {events::TxAdmitted, events::TxRejected, events::TxSealed, events::TxSealSkipped,
                   events::TxExecuted, events::TxRemoved}},
        {"pbft",
            {events::PrePrepareSent, events::PrePrepareReceived, events::PrePrepareRejected,
                events::PrepareQuorum, events::CommitQuorum, events::ProposalExecuted,
                events::ProposalExecuteFailed, events::CheckpointSent, events::CheckpointQuorum,
                events::CheckpointResend, events::BlockCommitted, events::Report}},
        {"viewchange",
            {events::ViewChangeTriggered, events::ViewChangeSent, events::ViewChangeReceived,
                events::ViewChangeQuorum, events::NewViewReached}},
        {"sync", {events::SyncStarted, events::SyncFinished}},
        {"seal-stall", {events::SealSkipped, events::SealResumed}},
        {"p2p", {events::PeerConnected, events::PeerDisconnected, events::HandshakeFailed,
                    events::PeerConnectFailed}},
        {"txsync", {events::ProposalTxsMissing, events::TxsRequested, events::TxsReceived,
                       events::TxsRequestFailed}},
    };
    auto names = c_eventsOf.find(sub);
    if (names == c_eventsOf.end())
    {
        throw OpsError(c_exitUsage, "unknown log subcommand: " + sub);
    }
    // status reads one line per kind (the last Report, the last SyncStarted, ...), so it keeps
    // only those; the traces need every line of their thread
    auto input = loadEvents(_args, namedEvents(names->second), sub == "status");
    bool json = wantJson(_args.flag("json"));
    if (sub == "status")
    {
        return runLogStatus(input, _args, _out);
    }
    if (sub == "tx")
    {
        return runLogTx(input, _args, _out);
    }
    TraceResult trace;
    if (sub == "pbft")
    {
        if (_args.positionals().size() < 2)
        {
            throw OpsError(c_exitUsage, "usage: log pbft <number>");
        }
        trace = tracePbft(input.events, std::stoll(_args.positionals()[1]));
    }
    else if (sub == "viewchange")
    {
        trace = traceViewChange(input.events, _args.numberOr<size_t>("last", 20));
    }
    else if (sub == "sync")
    {
        trace = traceSync(input.events);
    }
    else if (sub == "seal-stall")
    {
        trace = traceSealStall(input.events);
    }
    else if (sub == "p2p")
    {
        trace = traceP2p(input.events);
    }
    else if (sub == "txsync")
    {
        trace = traceTxSync(input.events);
    }
    else
    {
        throw OpsError(c_exitUsage, "unknown log subcommand: " + sub);
    }
    renderTrace(_out, trace, json);
    return c_exitOk;
}
}  // namespace

void registerLogCommand()
{
    registerCommand(
        "log", Command{"read the node's own log files: status snapshot or one thread of events",
                   "status | tx <hash> | pbft <number> | viewchange [--last N] | sync | seal-stall "
                   "| p2p | "
                   "txsync   [--since 1h] [--node-dir <dir>] [--json]",
                   {}, runLog});
}
}  // namespace bcos::ops
