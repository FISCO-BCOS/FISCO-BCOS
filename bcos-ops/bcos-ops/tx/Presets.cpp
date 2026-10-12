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
 * @file Presets.cpp
 */
#include "Presets.h"
#include <json/json.h>
#include <array>

namespace bcos::ops
{
namespace
{
// Addresses: bcos-framework/bcos-framework/executor/PrecompiledTypeDef.h. Access levels: the
// committee-only methods call checkSenderFromAuth (precompiled/common/Utilities.h) which wants
// sender == 0x…10001; Governor methods check origin against getGovernorList (setTermWeight in
// ConsensusPrecompiled.cpp, registerCaller/unregisterCaller in BalancePrecompiled.cpp);
// ContractAdmin methods are AuthManagerPrecompiled's method-level ones. Signatures are the
// CSS_METHOD_* / AUTH_METHOD_* / BALANCE_METHOD_* string constants of those files.
constexpr std::string_view c_sysConfig = "0x0000000000000000000000000000000000001000";
constexpr std::string_view c_consensus = "0x0000000000000000000000000000000000001003";
constexpr std::string_view c_authManager = "0x0000000000000000000000000000000000001005";
constexpr std::string_view c_balance = "0x0000000000000000000000000000000000001011";
constexpr std::string_view c_committeeManager = "0x0000000000000000000000000000000000010001";

// proposals the CommitteeManager bytecode exposes (bcos-auth); the arg maps follow spec §5.1.4
constexpr std::string_view c_setWeightProposal =
    "createSetConsensusWeightProposal(string,uint32,bool,uint256)";
constexpr std::string_view c_rmNodeProposal = "createRmNodeProposal(string,uint256)";
constexpr std::string_view c_sysConfigProposal =
    "createSetSysConfigProposal(string,string,uint256)";
constexpr std::string_view c_deployAuthTypeProposal =
    "createSetDeployAuthTypeProposal(uint8,uint256)";
constexpr std::string_view c_modifyDeployAuthProposal =
    "createModifyDeployAuthProposal(address,bool,uint256)";
constexpr std::string_view c_resetAdminProposal =
    "createResetAdminProposal(address,address,uint256)";

constexpr std::string_view c_hintNodeId = "128-hex node id";
constexpr std::string_view c_hintInterval = "blocks until the proposal expires";
constexpr std::string_view c_hintSelector = "0x-prefixed 4-byte method selector";

constexpr std::array p_nodeId{ParamSpec{"nodeId", "string", c_hintNodeId}};
constexpr std::array p_nodeWeight{
    ParamSpec{"nodeId", "string", c_hintNodeId}, ParamSpec{"weight", "uint256", "voting weight"}};
constexpr std::array p_nodeWeightTerm{ParamSpec{"nodeId", "string", c_hintNodeId},
    ParamSpec{"weight", "uint256", "voting weight"},
    ParamSpec{"termWeight", "uint256", "rpbft term weight"}};
constexpr std::array p_keyValue{ParamSpec{"key", "string", "system config key"},
    ParamSpec{"value", "string", "value (feature flags: 1)"}};
constexpr std::array p_key{ParamSpec{"key", "string", "system config key"}};
constexpr std::array p_addr{ParamSpec{"account", "address", ""}};
constexpr std::array p_addrAmount{
    ParamSpec{"account", "address", ""}, ParamSpec{"amount", "uint256", "wei"}};
constexpr std::array p_transfer{ParamSpec{"from", "address", ""}, ParamSpec{"to", "address", ""},
    ParamSpec{"amount", "uint256", "wei"}};
constexpr std::array p_contract{ParamSpec{"contract", "address", ""}};
constexpr std::array p_contractSelAddr{ParamSpec{"contract", "address", ""},
    ParamSpec{"selector", "bytes4", c_hintSelector}, ParamSpec{"account", "address", ""}};
constexpr std::array p_contractSel{
    ParamSpec{"contract", "address", ""}, ParamSpec{"selector", "bytes4", c_hintSelector}};
constexpr std::array p_contractAdmin{
    ParamSpec{"contract", "address", ""}, ParamSpec{"admin", "address", "new admin"}};
constexpr std::array p_authType{ParamSpec{"type", "uint8", "0 none, 1 white list, 2 black list"}};
constexpr std::array p_contractSelType{ParamSpec{"contract", "address", ""},
    ParamSpec{"selector", "bytes4", c_hintSelector},
    ParamSpec{"type", "uint8", "0 none, 1 white list, 2 black list"}};
constexpr std::array p_contractBool{
    ParamSpec{"contract", "address", ""}, ParamSpec{"available", "bool", "false freezes"}};
constexpr std::array p_contractStatus{ParamSpec{"contract", "address", ""},
    ParamSpec{"status", "uint8", "0 normal, 1 frozen, 2 abolished"}};
constexpr std::array p_governor{ParamSpec{"account", "address", ""},
    ParamSpec{"weight", "uint32", "0 removes the governor"},
    ParamSpec{"interval", "uint256", c_hintInterval}};
constexpr std::array p_rate{ParamSpec{"participatesRate", "uint8", "percent"},
    ParamSpec{"winRate", "uint8", "percent"}, ParamSpec{"interval", "uint256", c_hintInterval}};
constexpr std::array p_deployType{ParamSpec{"type", "uint8", "0 none, 1 white list, 2 black list"},
    ParamSpec{"interval", "uint256", c_hintInterval}};
constexpr std::array p_modifyDeploy{ParamSpec{"account", "address", ""},
    ParamSpec{"open", "bool", ""}, ParamSpec{"interval", "uint256", c_hintInterval}};
constexpr std::array p_resetAdmin{ParamSpec{"contract", "address", ""},
    ParamSpec{"admin", "address", ""}, ParamSpec{"interval", "uint256", c_hintInterval}};
constexpr std::array p_rmNode{
    ParamSpec{"nodeId", "string", c_hintNodeId}, ParamSpec{"interval", "uint256", c_hintInterval}};
constexpr std::array p_consWeight{ParamSpec{"nodeId", "string", c_hintNodeId},
    ParamSpec{"weight", "uint32", "0 = observer"}, ParamSpec{"add", "bool", "true adds / updates"},
    ParamSpec{"interval", "uint256", c_hintInterval}};
constexpr std::array p_sysProposal{ParamSpec{"key", "string", ""}, ParamSpec{"value", "string", ""},
    ParamSpec{"interval", "uint256", c_hintInterval}};
constexpr std::array p_voteComputer{
    ParamSpec{"computer", "address", ""}, ParamSpec{"interval", "uint256", c_hintInterval}};
constexpr std::array p_proposalId{ParamSpec{"proposalId", "uint256", ""}};
constexpr std::array p_vote{ParamSpec{"proposalId", "uint256", ""}, ParamSpec{"agree", "bool", ""}};
constexpr std::array p_range{
    ParamSpec{"from", "uint256", "first proposal id"}, ParamSpec{"to", "uint256", "last id"}};
constexpr std::array p_text{ParamSpec{"value", "string", ""}};
constexpr std::span<const ParamSpec> p_none;

#define OPS_PROPOSAL(sig, args) \
    std::optional<ProposalSpec> \
    {                           \
        ProposalSpec            \
        {                       \
            sig, args           \
        }                       \
    }
#define OPS_DIRECT std::nullopt

// clang-format off
constexpr std::array c_presets{
    // consensus: ConsensusPrecompiled.cpp; version ≥ 3.1 wants addSealer on an observer (-51104)
    Preset{"consensus", "add-sealer", "addSealer", c_consensus, "addSealer(string,uint256)", p_nodeWeight, "int32", Access::Committee, OPS_PROPOSAL(c_setWeightProposal, "$0,$1,true,$interval"), true},
    Preset{"consensus", "add-sealer-term", "addSealer", c_consensus, "addSealer(string,uint256,uint256)", p_nodeWeightTerm, "int32", Access::Committee, OPS_DIRECT, true},
    Preset{"consensus", "add-observer", "addObserver", c_consensus, "addObserver(string)", p_nodeId, "int32", Access::Committee, OPS_PROPOSAL(c_setWeightProposal, "$0,0,true,$interval"), true},
    Preset{"consensus", "remove", "removeNode", c_consensus, "remove(string)", p_nodeId, "int32", Access::Committee, OPS_PROPOSAL(c_rmNodeProposal, "$0,$interval"), true},
    Preset{"consensus", "set-weight", "setConsensusWeight", c_consensus, "setWeight(string,uint256)", p_nodeWeight, "int32", Access::Committee, OPS_PROPOSAL(c_setWeightProposal, "$0,$1,true,$interval"), true},
    Preset{"consensus", "set-term-weight", "", c_consensus, "setTermWeight(string,uint256)", p_nodeWeight, "int32", Access::Governor, OPS_DIRECT, true},
    Preset{"consensus", "sealers", "getSealerList", c_toRpc, "getSealerList", p_none, "", Access::Anyone, OPS_DIRECT, false},
    Preset{"consensus", "observers", "getObserverList", c_toRpc, "getObserverList", p_none, "", Access::Anyone, OPS_DIRECT, false},
    Preset{"consensus", "status", "getConsensusStatus", c_toRpc, "getConsensusStatus", p_none, "", Access::Anyone, OPS_DIRECT, false},
    // sysconfig: SystemConfigPrecompiled.cpp; keys = m_sysValueCmp ∪ m_valueConverter ∪ featureKeys
    Preset{"sysconfig", "set", "setSystemConfigByKey", c_sysConfig, "setValueByKey(string,string)", p_keyValue, "int32", Access::Committee, OPS_PROPOSAL(c_sysConfigProposal, "$0,$1,$interval"), true},
    Preset{"sysconfig", "get", "getSystemConfigByKey", c_sysConfig, "getValueByKey(string)", p_key, "string,int64", Access::Anyone, OPS_DIRECT, false},
    // balance: BalancePrecompiled.cpp; exists only with feature_balance_precompiled; add/sub/
    // transfer fail by throwing (receipt status ≠ 0), not by a code in the output
    Preset{"balance", "get", "getBalance", c_balance, "getBalance(address)", p_addr, "uint256", Access::Anyone, OPS_DIRECT, false},
    Preset{"balance", "add", "addBalance", c_balance, "addBalance(address,uint256)", p_addrAmount, "", Access::Caller, OPS_DIRECT, true},
    Preset{"balance", "sub", "subBalance", c_balance, "subBalance(address,uint256)", p_addrAmount, "", Access::Caller, OPS_DIRECT, true},
    Preset{"balance", "transfer", "transfer", c_balance, "transfer(address,address,uint256)", p_transfer, "", Access::Caller, OPS_DIRECT, true},
    Preset{"balance", "register-caller", "registerBalanceGovernor", c_balance, "registerCaller(address)", p_addr, "int32", Access::Governor, OPS_DIRECT, true},
    Preset{"balance", "unregister-caller", "unregisterBalanceGovernor", c_balance, "unregisterCaller(address)", p_addr, "int32", Access::Governor, OPS_DIRECT, true},
    Preset{"balance", "list-caller", "listBalanceGovernor", c_balance, "listCaller()", p_none, "address[]", Access::Anyone, OPS_DIRECT, false},
    // auth: AuthManagerPrecompiled.cpp (method-level ones forward to ContractAuthMgrPrecompiled)
    Preset{"auth", "get-admin", "getContractAdmin", c_authManager, "getAdmin(address)", p_contract, "address", Access::Anyone, OPS_DIRECT, false},
    Preset{"auth", "deploy-type", "getDeployAuth", c_authManager, "deployType()", p_none, "uint256", Access::Anyone, OPS_DIRECT, false},
    Preset{"auth", "has-deploy-auth", "checkDeployAuth", c_authManager, "hasDeployAuth(address)", p_addr, "bool", Access::Anyone, OPS_DIRECT, false},
    Preset{"auth", "check-method-auth", "checkMethodAuth", c_authManager, "checkMethodAuth(address,bytes4,address)", p_contractSelAddr, "bool", Access::Anyone, OPS_DIRECT, false},
    Preset{"auth", "get-method-auth", "getMethodAuth", c_authManager, "getMethodAuth(address,bytes4)", p_contractSel, "uint8,string[],string[]", Access::Anyone, OPS_DIRECT, false},
    Preset{"auth", "contract-available", "contractAvailable", c_authManager, "contractAvailable(address)", p_contract, "bool", Access::Anyone, OPS_DIRECT, false},
    Preset{"auth", "reset-admin", "resetAdmin", c_authManager, "resetAdmin(address,address)", p_contractAdmin, "int32", Access::Committee, OPS_PROPOSAL(c_resetAdminProposal, "$0,$1,$interval"), true},
    Preset{"auth", "set-deploy-auth-type", "setDeployAuthType", c_authManager, "setDeployAuthType(uint8)", p_authType, "int32", Access::Committee, OPS_PROPOSAL(c_deployAuthTypeProposal, "$0,$interval"), true},
    Preset{"auth", "open-deploy-auth", "openDeployAuth", c_authManager, "openDeployAuth(address)", p_addr, "int32", Access::Committee, OPS_PROPOSAL(c_modifyDeployAuthProposal, "$0,true,$interval"), true},
    Preset{"auth", "close-deploy-auth", "closeDeployAuth", c_authManager, "closeDeployAuth(address)", p_addr, "int32", Access::Committee, OPS_PROPOSAL(c_modifyDeployAuthProposal, "$0,false,$interval"), true},
    Preset{"auth", "set-method-auth-type", "setMethodAuth", c_authManager, "setMethodAuthType(address,bytes4,uint8)", p_contractSelType, "int32", Access::ContractAdmin, OPS_DIRECT, true},
    Preset{"auth", "open-method-auth", "openMethodAuth", c_authManager, "openMethodAuth(address,bytes4,address)", p_contractSelAddr, "int32", Access::ContractAdmin, OPS_DIRECT, true},
    Preset{"auth", "close-method-auth", "closeMethodAuth", c_authManager, "closeMethodAuth(address,bytes4,address)", p_contractSelAddr, "int32", Access::ContractAdmin, OPS_DIRECT, true},
    Preset{"auth", "set-contract-available", "freezeContract", c_authManager, "setContractStatus(address,bool)", p_contractBool, "int32", Access::ContractAdmin, OPS_DIRECT, true},
    Preset{"auth", "set-contract-status", "abolishContract", c_authManager, "setContractStatus(address,uint8)", p_contractStatus, "int32", Access::ContractAdmin, OPS_DIRECT, true},
    // committee: CommitteeManager (bcos-auth, deployed at genesis by AuthInitializer.cpp)
    Preset{"committee", "propose-governor", "updateGovernorProposal", c_committeeManager, "createUpdateGovernorProposal(address,uint32,uint256)", p_governor, "uint256", Access::Governor, OPS_DIRECT, true},
    Preset{"committee", "propose-rate", "setRateProposal", c_committeeManager, "createSetRateProposal(uint8,uint8,uint256)", p_rate, "uint256", Access::Governor, OPS_DIRECT, true},
    Preset{"committee", "propose-deploy-auth-type", "setDeployAuthTypeProposal", c_committeeManager, "createSetDeployAuthTypeProposal(uint8,uint256)", p_deployType, "uint256", Access::Governor, OPS_DIRECT, true},
    Preset{"committee", "propose-deploy-auth", "modifyDeployAuthProposal", c_committeeManager, "createModifyDeployAuthProposal(address,bool,uint256)", p_modifyDeploy, "uint256", Access::Governor, OPS_DIRECT, true},
    Preset{"committee", "propose-reset-admin", "resetAdminProposal", c_committeeManager, "createResetAdminProposal(address,address,uint256)", p_resetAdmin, "uint256", Access::Governor, OPS_DIRECT, true},
    Preset{"committee", "propose-rm-node", "rmNodeProposal", c_committeeManager, "createRmNodeProposal(string,uint256)", p_rmNode, "uint256", Access::Governor, OPS_DIRECT, true},
    Preset{"committee", "propose-weight", "setConsensusWeightProposal", c_committeeManager, "createSetConsensusWeightProposal(string,uint32,bool,uint256)", p_consWeight, "uint256", Access::Governor, OPS_DIRECT, true},
    Preset{"committee", "propose-sysconfig", "setSysConfigProposal", c_committeeManager, "createSetSysConfigProposal(string,string,uint256)", p_sysProposal, "uint256", Access::Governor, OPS_DIRECT, true},
    Preset{"committee", "propose-vote-computer", "upgradeVoteComputerProposal", c_committeeManager, "createUpgradeVoteComputerProposal(address,uint256)", p_voteComputer, "uint256", Access::Governor, OPS_DIRECT, true},
    Preset{"committee", "revoke", "revokeProposal", c_committeeManager, "revokeProposal(uint256)", p_proposalId, "", Access::Governor, OPS_DIRECT, true},
    Preset{"committee", "vote", "voteProposal", c_committeeManager, "voteProposal(uint256,bool)", p_vote, "", Access::Governor, OPS_DIRECT, true},
    Preset{"committee", "is-governor", "", c_committeeManager, "isGovernor(address)", p_addr, "bool", Access::Anyone, OPS_DIRECT, false},
    Preset{"committee", "committee-address", "", c_committeeManager, "_committee()", p_none, "address", Access::Anyone, OPS_DIRECT, false},
    Preset{"committee", "proposal-mgr-address", "", c_committeeManager, "_proposalMgr()", p_none, "address", Access::Anyone, OPS_DIRECT, false},
    // ProposalManager / Committee: addresses read from CommitteeManager each time; the tuple
    // outputs of getProposalInfo* come with the pinned bcos-auth ABI (AuthAbi.h, ticket 04)
    Preset{"committee", "proposal", "getProposalInfo", c_toProposalMgr, "getProposalInfo(uint256)", p_proposalId, "address,address,uint8,uint256,uint8,address[],address[]", Access::Anyone, OPS_DIRECT, false},
    Preset{"committee", "proposals", "getProposalInfo", c_toProposalMgr, "getProposalInfoList(uint256,uint256)", p_range, "", Access::Anyone, OPS_DIRECT, false},
    Preset{"committee", "proposal-status", "", c_toProposalMgr, "getProposalStatus(uint256)", p_proposalId, "uint8", Access::Anyone, OPS_DIRECT, false},
    Preset{"committee", "proposal-count", "", c_toProposalMgr, "_proposalCount()", p_none, "uint256", Access::Anyone, OPS_DIRECT, false},
    Preset{"committee", "info", "getCommitteeInfo", c_toCommittee, "getCommitteeInfo()", p_none, "uint8,uint8,address[],uint32[]", Access::Anyone, OPS_DIRECT, false},
    Preset{"committee", "weight", "", c_toCommittee, "getWeight(address)", p_addr, "uint32", Access::Anyone, OPS_DIRECT, false},
    Preset{"committee", "participates-rate", "", c_toCommittee, "_participatesRate()", p_none, "uint8", Access::Anyone, OPS_DIRECT, false},
    Preset{"committee", "win-rate", "", c_toCommittee, "_winRate()", p_none, "uint8", Access::Anyone, OPS_DIRECT, false},
    // hello: bytecode in tx/HelloWorld.h
    Preset{"hello", "set", "", c_toHello, "set(string)", p_text, "", Access::Anyone, OPS_DIRECT, true},
    Preset{"hello", "get", "", c_toHello, "get()", p_none, "string", Access::Anyone, OPS_DIRECT, false},
};
// clang-format on
#undef OPS_PROPOSAL
#undef OPS_DIRECT

std::vector<std::string> splitTypes(std::string_view _list)
{
    std::vector<std::string> types;
    size_t start = 0;
    while (start <= _list.size() && !_list.empty())
    {
        auto comma = _list.find(',', start);
        auto end = comma == std::string_view::npos ? _list.size() : comma;
        types.emplace_back(_list.substr(start, end - start));
        if (comma == std::string_view::npos)
        {
            break;
        }
        start = comma + 1;
    }
    return types;
}
}  // namespace

std::string Preset::methodName() const
{
    auto paren = signature.find('(');
    return std::string(paren == std::string_view::npos ? signature : signature.substr(0, paren));
}

std::string Preset::outputsAbi() const
{
    if (outputs.empty())
    {
        return "";
    }
    Json::Value function;
    function["type"] = "function";
    function["name"] = methodName();
    function["constant"] = !write;
    function["inputs"] = Json::Value(Json::arrayValue);
    for (auto const& type : signatureInputTypes(signature))
    {
        Json::Value input;
        input["name"] = "";
        input["type"] = type;
        function["inputs"].append(input);
    }
    function["outputs"] = Json::Value(Json::arrayValue);
    for (auto const& type : splitTypes(outputs))
    {
        Json::Value output;
        output["name"] = "";
        output["type"] = type;
        function["outputs"].append(output);
    }
    Json::Value abi(Json::arrayValue);
    abi.append(function);
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, abi);
}

std::span<const Preset> allPresets()
{
    return c_presets;
}

std::vector<Preset const*> presetsOf(std::string_view _group)
{
    std::vector<Preset const*> found;
    for (auto const& preset : c_presets)
    {
        if (preset.group == _group)
        {
            found.push_back(&preset);
        }
    }
    return found;
}

Preset const* findPreset(std::string_view _group, std::string_view _name)
{
    for (auto const& preset : c_presets)
    {
        if (preset.group == _group && preset.name == _name)
        {
            return &preset;
        }
    }
    return nullptr;
}

std::vector<std::string> signatureInputTypes(std::string_view _signature)
{
    auto open = _signature.find('(');
    auto close = _signature.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open)
    {
        return {};
    }
    return splitTypes(_signature.substr(open + 1, close - open - 1));
}
}  // namespace bcos::ops
