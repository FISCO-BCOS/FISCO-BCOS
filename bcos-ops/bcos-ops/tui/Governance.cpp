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
 * @brief see Governance.h
 * @file Governance.cpp
 */
#include "Governance.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/cmd/CommitteeCmd.h"
#include "bcos-ops/tx/PresetCall.h"
#include <algorithm>

namespace bcos::ops::tui
{
namespace
{
std::string govText(Json::Value const& _value)
{
    if (_value.isString())
    {
        return _value.asString();
    }
    if (_value.isIntegral())
    {
        return std::to_string(_value.asInt64());
    }
    if (_value.isNull())
    {
        return "";
    }
    return _value.toStyledString();
}

int govInt(Json::Value const& _value)
{
    if (_value.isIntegral())
    {
        return _value.asInt();
    }
    try
    {
        return std::stoi(govText(_value));
    }
    catch (std::exception const&)
    {
        return 0;
    }
}

void govCheckStop(std::atomic<bool> const* _stop)
{
    if (_stop != nullptr && _stop->load())
    {
        throw OpsError(c_exitUsage, "refresh stopped");
    }
}
}  // namespace

std::string proposalTypeName(int _type)
{
    switch (_type)
    {
    case 11:
        return "governor";
    case 12:
        return "rate";
    case 13:
        return "voteComputer";
    case 21:
        return "deployAuthType";
    case 22:
        return "deployAuth";
    case 31:
        return "resetAdmin";
    case 41:
        return "sysConfig";
    case 51:
        return "consensusWeight";
    case 52:
        return "rmNode";
    default:
        return "type" + std::to_string(_type);
    }
}

ProposalRow proposalRowFrom(ProposalOutcome const& _outcome)
{
    ProposalRow row;
    try
    {
        row.id = std::stoll(_outcome.id);
    }
    catch (std::exception const&)
    {
        row.id = 0;
    }
    row.status = _outcome.status;
    row.statusName = _outcome.statusName;
    row.executed = _outcome.executed;
    row.execCode = _outcome.execCode;
    row.execCodeName = _outcome.execCodeName;
    auto const& info = _outcome.info;
    row.type = govInt(info["proposalType"]);
    row.typeName = proposalTypeName(row.type);
    row.proposer = govText(info["proposer"]);
    row.resource = govText(info["resourceId"]);
    row.expiresAtBlock = govText(info["expiresAtBlock"]);
    row.agree = info["agreeVoters"].isArray() ? info["agreeVoters"].size() : 0;
    row.against = info["againstVoters"].isArray() ? info["againstVoters"].size() : 0;
    return row;
}

void governanceFetch(GovernanceState& _state, CommitteeReader& _reader,
    GovernanceWants const& _wants, std::atomic<bool> const* _stop)
{
    try
    {
        govCheckStop(_stop);
        // the two contract addresses are fixed at genesis: read once, then reused every round
        auto committee = !_state.committee.empty() ?
                             _state.committee :
                             _reader.read("committee-address", {})[0].asString();
        govCheckStop(_stop);
        auto info = _reader.read("info", {}, committee);
        govCheckStop(_stop);
        auto manager = !_state.proposalManager.empty() ?
                           _state.proposalManager :
                           _reader.read("proposal-mgr-address", {})[0].asString();
        govCheckStop(_stop);
        auto count = std::stoll(govText(_reader.read("proposal-count", {}, manager)[0]));

        GovernanceState next;
        next.loaded = true;
        next.committee = committee;
        next.proposalManager = manager;
        next.participatesRate = govText(info.get(0U, ""));
        next.winRate = govText(info.get(1U, ""));
        auto governors = info.get(2U, Json::Value(Json::arrayValue));
        auto weights = info.get(3U, Json::Value(Json::arrayValue));
        for (Json::ArrayIndex i = 0; i < governors.size(); ++i)
        {
            next.governors.push_back({govText(governors[i]), govText(weights.get(i, ""))});
        }
        next.proposalCount = count;
        auto rows =
            static_cast<int64_t>(std::min(std::max<size_t>(1, _wants.rows), c_governanceMaxRows));
        for (auto id = count; id >= 1 && id > count - rows; --id)
        {
            govCheckStop(_stop);
            auto outcome = _reader.runner.proposalOutcome(std::to_string(id), Receipt{});
            if (!outcome.error.empty())
            {
                next.error = outcome.error;
            }
            next.proposals.push_back(proposalRowFrom(outcome));
        }
        for (auto id : _wants.ids)
        {
            if (id < 1 || id > count)
            {
                continue;
            }
            auto listed = std::find_if(next.proposals.begin(), next.proposals.end(),
                [id](ProposalRow const& r) { return r.id == id; });
            if (listed != next.proposals.end())
            {
                next.details[id] = *listed;
                continue;
            }
            govCheckStop(_stop);
            next.details[id] =
                proposalRowFrom(_reader.runner.proposalOutcome(std::to_string(id), Receipt{}));
        }
        _state = std::move(next);
    }
    catch (std::exception const& e)
    {
        _state.error = e.what();
    }
}
}  // namespace bcos::ops::tui
