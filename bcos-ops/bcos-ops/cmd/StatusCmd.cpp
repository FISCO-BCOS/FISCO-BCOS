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
 * @file StatusCmd.cpp
 */
#include "StatusCmd.h"
#include "bcos-ops/Args.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/Connect.h"
#include "bcos-ops/Mode.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/Output.h"
#include "bcos-ops/collect/RpcCollector.h"
#include <algorithm>
#include <chrono>
#include <ostream>
#include <type_traits>

namespace bcos::ops
{
namespace
{
int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

template <typename T>
std::string show(std::optional<T> const& _value)
{
    if (!_value)
    {
        return "-";
    }
    if constexpr (std::is_same_v<T, bool>)
    {
        return *_value ? "true" : "false";
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
        return *_value;
    }
    else
    {
        return std::to_string(*_value);
    }
}

std::string ago(std::optional<int64_t> const& _timestampMs)
{
    if (!_timestampMs)
    {
        return "-";
    }
    // the timestamp comes from the RPC peer: clamp it so the subtraction cannot overflow
    auto now = nowMs();
    auto delta = now - std::clamp<int64_t>(*_timestampMs, 0, now);
    if (delta < 60000)
    {
        return std::to_string(delta / 1000) + "s ago";
    }
    return std::to_string(delta / 60000) + "m ago";
}
}  // namespace

Thresholds thresholdsFrom(Args const& _args)
{
    Thresholds thresholds;
    thresholds.stallFactor = _args.numberOr<double>("stall-factor", thresholds.stallFactor);
    thresholds.maxLag = _args.numberOr<int64_t>("max-lag", thresholds.maxLag);
    thresholds.viewChangeWindowMs =
        _args.numberOr<int64_t>("viewchange-window-ms", thresholds.viewChangeWindowMs);
    return thresholds;
}

void renderStatus(
    std::ostream& _out, NodeStatus const& _status, std::vector<Check> const& _checks, bool _json)
{
    if (_json)
    {
        auto root = _status.toJson();
        root["checks"] = checksToJson(_checks);
        printJson(_out, root);
        return;
    }
    std::vector<std::pair<std::string, std::string>> rows;
    std::string identity = _status.nodeId ? abridged(*_status.nodeId) : "-";
    if (_status.isConsensusNode)
    {
        identity += *_status.isConsensusNode ? " (sealer)" : " (observer)";
    }
    identity += "   version " + show(_status.version) + "   source " + _status.source;
    rows.emplace_back("node", identity);
    rows.emplace_back("chain", "height " + show(_status.blockNumber) + "  hash " +
                                   (_status.latestHash ? abridged(*_status.latestHash) : "-") +
                                   "  " + ago(_status.latestTimestamp));
    if (isElMode(_status.mode))
    {
        rows.emplace_back("consensus", "none: mode " + _status.mode + " runs no PBFT");
    }
    else if (!readsPbft(_status.mode))
    {
        rows.emplace_back("consensus", "not read: mode unknown, pass --mode none to read PBFT");
    }
    else
    {
        rows.emplace_back("consensus",
            "view " + show(_status.view) + "  leader " + show(_status.leaderIndex) + "  nodes " +
                show(_status.consensusNodesNum) + "  connected " +
                show(_status.connectedGroupNodes) + "  quorum " + show(_status.minRequiredQuorum) +
                (_status.inTimeout && *_status.inTimeout ? "  TIMEOUT" : ""));
    }
    rows.emplace_back(
        "sync", std::string(_status.isSyncing && *_status.isSyncing ? "syncing" : "idle") +
                    "  highest " + show(_status.knownHighestNumber) + "  lag " + show(_status.lag) +
                    "  peers " + show(_status.peerCount));
    rows.emplace_back(
        "txpool", "pending " + show(_status.pendingTxSize) +
                      (_status.txpoolLimit ? "  limit " + show(_status.txpoolLimit) : ""));
    size_t failed = 0;
    size_t skippedCount = 0;
    size_t infoCount = 0;
    std::string failedNames;
    for (auto const& check : _checks)
    {
        if (check.failed())
        {
            ++failed;
            failedNames +=
                (failedNames.empty() ? "" : ", ") + check.name + " (" + check.detail + ")";
        }
        else if (check.state == "skipped")
        {
            ++skippedCount;
        }
        else if (!check.ok())
        {
            ++infoCount;
        }
    }
    std::string summary;
    if (failed > 0)
    {
        summary = std::to_string(failed) + " fail: " + failedNames;
    }
    else if (!anyEvaluated(_checks))
    {
        summary = "none evaluated";
    }
    else
    {
        summary = std::to_string(_checks.size() - skippedCount - infoCount) + " ok";
    }
    if (skippedCount > 0)
    {
        summary += "  (" + std::to_string(skippedCount) + " skipped)";
    }
    rows.emplace_back("checks", summary);
    printRows(_out, rows);
    if (!_status.reasons.empty())
    {
        for (auto const& [field, reason] : _status.reasons)
        {
            _out << "  " << field << ": " << sanitizeForTerminal(reason) << '\n';
        }
    }
}

int runStatusOn(
    Connection const& _connection, Thresholds const& _thresholds, bool _json, std::ostream& _out)
{
    LocalFallbacks fallbacks;
    if (_connection.nodeDir)
    {
        fallbacks.txpoolLimit = static_cast<int64_t>(_connection.nodeDir->txpoolLimit);
        fallbacks.consensusTimeoutMs = _connection.nodeDir->consensusTimeoutMs;
    }
    auto status = collectFromRpc(
        _connection.call, _connection.group, _connection.source, fallbacks, _connection.mode);
    auto checks = evaluate(status, _thresholds, nowMs());
    renderStatus(_out, status, checks, _json);
    return anyFailed(checks) || !anyEvaluated(checks) ? c_exitChecksFailed : c_exitOk;
}

namespace
{
int runStatus(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    (void)_err;
    auto connection = connect(connectOptionsFrom(_args));
    return runStatusOn(connection, thresholdsFrom(_args), wantJson(_args.flag("json")), _out);
}

}  // namespace

void registerStatusCommand()
{
    registerCommand(
        "status", Command{"print the node status snapshot and run the health checks",
                      "[--node-dir <dir> | --rpc <host:port>] [--mode none|el|opstack-el] [--json] "
                      "[--stall-factor 2] [--max-lag 10] [--timeout 15000]\n\n"
                      "PBFT state (getConsensusStatus) is read only in mode none: the call makes "
                      "el / opstack-el nodes exit, so a bare --rpc (mode unknown) skips "
                      "consensus_stable and quorum_connected until --mode none is given. "
                      "`attach getConsensusStatus` is not gated; op_engine_rpc and single-node "
                      "non-PBFT setups are not recognised as EL",
                      {}, runStatus});
}
}  // namespace bcos::ops
