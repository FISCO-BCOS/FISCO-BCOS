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
 * @brief the gov page's data: committee rates and governors, the newest proposals and the
 *        details of the ones open on screen, read through the same CommitteeReader the CLI
 *        uses. Plain rows the renderers show; fetched by the refresher on auth chains only.
 * @file Governance.h
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bcos::ops
{
struct CommitteeReader;
struct ProposalOutcome;
}  // namespace bcos::ops

namespace bcos::ops::tui
{
struct GovernorRow
{
    std::string address;
    std::string weight;
};

struct ProposalRow
{
    int64_t id = 0;
    int type = 0;  // CommitteeManager.sol:384-445 (11 governor … 52 rmNode)
    std::string typeName;
    int status = 0;  // ProposalManager.sol:19
    std::string statusName;
    std::string proposer;
    std::string resource;
    std::string expiresAtBlock;
    size_t agree = 0;
    size_t against = 0;
    bool executed = false;
    std::optional<int32_t> execCode;
    std::string execCodeName;
};

struct GovernanceState
{
    bool loaded = false;  // one fetch succeeded since the TUI started
    std::string committee;
    std::string proposalManager;
    std::string participatesRate;
    std::string winRate;
    std::vector<GovernorRow> governors;
    int64_t proposalCount = 0;
    std::vector<ProposalRow> proposals;      // newest first, the rows the list shows
    std::map<int64_t, ProposalRow> details;  // the proposals open in a detail page
    std::string error;                       // last fetch failure; empty after a success
};

/// what the gov page needs from the refresher: how many list rows fit, which ids are open
struct GovernanceWants
{
    size_t rows = 20;
    std::vector<int64_t> ids;
    bool shown = true;  // page 6 or a proposal detail is open: only then the refresher reads it
};

constexpr size_t c_governanceMaxRows = 50;

ProposalRow proposalRowFrom(ProposalOutcome const& _outcome);
/// committee info, the newest min(_wants.rows, 50) proposals and the details of _wants.ids;
/// _stop (when given) is checked between RPC calls and the fetch ends early. A failure leaves
/// the previous rows and sets _state.error.
void governanceFetch(GovernanceState& _state, CommitteeReader& _reader,
    GovernanceWants const& _wants, std::atomic<bool> const* _stop);
}  // namespace bcos::ops::tui
