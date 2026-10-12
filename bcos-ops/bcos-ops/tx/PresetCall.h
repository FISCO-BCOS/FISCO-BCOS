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
 * @brief running a Preset: CLI strings → ABI-encoded call → send or call → decoded result with
 *        the precompiled's int32 code mapped to its name. Shared by the CLI groups and the TUI
 *        send page; neither encodes anything itself.
 * @file PresetCall.h
 */
#pragma once

#include "Presets.h"
#include "TxSender.h"
#include <json/json.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bcos::ops
{
/// name of a negative code a precompiled writes into its output (precompiled/common/Common.h);
/// "" when unknown
std::string precompiledCodeName(int32_t _code);

/// positional CLI values → JSON array text for Abi::encodeMethodBySignature, typed by the
/// preset's params: bytes / bytesN accept `0x…` and become the SDK's `hex://…`; numbers, bools
/// and arrays keep their JSON type; strings and addresses stay strings. Throws OpsError{1} on
/// a wrong count or a malformed bytes value.
std::string encodePresetArgs(Preset const& _preset, std::vector<std::string> const& _args);
/// `0x12345678` → `hex://12345678` (also accepts the already converted form)
std::string toSdkBytes(std::string const& _hexValue);

/// ProposalManager.sol:19 — status of a proposal; `executed` means the committee ran the
/// governance call inside the vote that passed it, `execCode` is that call's int32 result from
/// the execResult(int256) event (0 ok, else a precompiled code such as -51104)
struct ProposalOutcome
{
    std::string id;
    int status = 0;  // 0 none 1 voting 2 passed 3 denied 4 revoked 5 outdated
    std::string statusName;
    bool executed = false;
    std::optional<int32_t> execCode;
    std::string execCodeName;
    Json::Value info;   // getProposalInfo decoded: resource/proposer/type/expiresAtBlock/status/
                        // voters (the absolute block the proposal expires at)
    std::string error;  // getProposalStatus / getProposalInfo failed: status and info unknown
    Json::Value toJson() const;
};
/// getProposalInfo's decoded tuple → the info object above (resourceId, proposer,
/// proposalType, expiresAtBlock, status, agreeVoters, againstVoters); shared by the runner and
/// `committee proposals`
Json::Value proposalInfoJson(Json::Value const& _fields);
std::string proposalStatusName(int _status);

struct PresetResult
{
    std::string mode = "direct";  // direct | proposal (routed through CommitteeManager)
    std::string to;
    std::string method;
    Json::Value params{Json::arrayValue};
    std::string directMethod;  // the precompiled signature a proposal stands in for
    Receipt receipt;
    Json::Value decoded;  // output decoded through the preset's ABI fragment (null when none)
    std::optional<int32_t> code;  // int32 code from the output when the preset returns one
    std::string codeName;
    std::optional<ProposalOutcome> proposal;
    bool ok = false;  // receipt status 0 and (when the preset returns a code) code == 0
    int exitCode() const;
    Json::Value toJson() const;
};

/// how write presets with a ProposalSpec are routed (spec §5.1.4)
struct Routing
{
    bool proposals = false;  // chainUsesProposals(): committee-only writes become proposals
    bool direct = false;     // --direct: call the precompiled even on an auth chain
    std::optional<int64_t> interval;  // --interval: blocks from now; default c_proposalInterval
};

/// blockNumberInterval of a proposal is a count of blocks from the creating block:
/// ProposalManager.sol:102 stores `block.number + blockNumberInterval` and :63 expires the
/// proposal once `block.number` passes that sum. 1000 blocks keeps a multi-governor vote open
/// for a while without an RPC round trip.
constexpr int64_t c_proposalInterval = 1000;
/// the interval a proposal is created with: _routing.interval when given (must be >= 1,
/// OpsError{1} otherwise), else c_proposalInterval. The one place that rule lives.
int64_t proposalInterval(Routing const& _routing);

/// _preset.proposal->args expanded against _args with the interval of _routing: `$N` →
/// _args[N], `$interval` → proposalInterval(_routing), literals kept. The runner and the TUI's
/// confirmation layer share it.
std::vector<std::string> proposalArgsFor(
    Preset const& _preset, std::vector<std::string> const& _args, Routing const& _routing);

/// the reason of a solidity-style revert: output `Error(string)` (selector 0x08c379a0) decoded;
/// "" when the output is not one (BalancePrecompiled and reverted contracts fail this way,
/// the receipt's message stays empty)
std::string revertReason(std::string const& _outputHex, bool _sm);

/// decodes a receipt (or a call result) of _preset; a failed receipt without a message takes
/// its revert reason as message
PresetResult decodePresetResult(Preset const& _preset, Receipt const& _receipt, bool _sm);

/// the one place that decides direct vs proposal (ticket 04) and runs the call
class PresetRunner
{
public:
    /// _call / _group are only needed for the proposal path (ProposalManager lookups)
    PresetRunner(Sender& _sender, bool _sm, RpcCall _call = {}, std::string _group = "")
      : m_sender(_sender), m_sm(_sm), m_call(std::move(_call)), m_group(std::move(_group))
    {}
    void setRouting(Routing _routing) { m_routing = _routing; }
    Routing const& routing() const { return m_routing; }
    /// true when run() will send a CommitteeManager proposal instead of _preset's call
    bool routesThroughProposal(Preset const& _preset) const;
    /// the same decision for a routing not yet installed in a runner (the TUI's confirmation
    /// layer asks before anything is sent): write preset with a ProposalSpec, proposals on,
    /// not --direct
    static bool routesThroughProposal(Routing const& _routing, Preset const& _preset);
    /// the committee preset that stands in for _preset on an auth chain (nullptr when none)
    static Preset const* proposalPreset(Preset const& _preset);
    /// _preset.proposal->args expanded: `$N` → _args[N], `$interval` → proposalInterval(),
    /// literals kept
    std::vector<std::string> proposalArgs(
        Preset const& _preset, std::vector<std::string> const& _args) const;
    /// _to overrides the preset's `to` (sentinels, HelloWorld address); write presets are sent,
    /// read presets are called. A committee-only write on an auth chain becomes the proposal
    /// and the result carries the proposal id, status and execution code.
    PresetResult run(
        Preset const& _preset, std::vector<std::string> const& _args, std::string const& _to = "");
    /// the encoded call data, for the confirmation layer and tests
    bytes encode(Preset const& _preset, std::vector<std::string> const& _args) const;
    /// reads status / info / execution code of proposal _id (CommitteeManager → ProposalManager)
    ProposalOutcome proposalOutcome(std::string const& _id, Receipt const& _createReceipt);

private:
    PresetResult runCall(
        Preset const& _preset, std::vector<std::string> const& _args, std::string const& _to);
    std::string proposalManagerAddress();

    Sender& m_sender;
    bool m_sm;
    RpcCall m_call;
    std::string m_group;
    Routing m_routing;
    std::string m_proposalMgr;  // cached _proposalMgr()
};
}  // namespace bcos::ops
