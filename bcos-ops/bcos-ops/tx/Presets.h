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
 * @brief the one static table of governance / precompiled / HelloWorld calls that the CLI
 *        groups (consensus, sysconfig, balance, auth, committee) and the TUI send page both
 *        read: contract address, method signature, parameter names, output types, who may call
 *        it, and the CommitteeManager proposal that replaces it on an auth chain.
 * @file Presets.h
 */
#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::ops
{
/// who the precompiled lets through (read from the executor sources, see Presets.cpp)
enum class Access
{
    Anyone,
    Caller,         // BalancePrecompiled: sender registered through registerCaller
    ContractAdmin,  // AuthManager method-level: sender == the contract's admin
    Governor,       // origin in getGovernorList
    Committee,      // sender must be the committee contract 0x…10001: EOA calls get -50000
};

struct ParamSpec
{
    std::string_view name;
    std::string_view type;  // solidity ABI type, no tuples
    std::string_view hint;
};

/// the CommitteeManager proposal an auth chain needs instead of the direct call. `args` maps
/// the proposal's parameters from the direct call's: comma-separated tokens, `$N` = direct
/// parameter N, `$interval` = blockNumberInterval, anything else a literal.
struct ProposalSpec
{
    std::string_view signature;
    std::string_view args;
};

/// `to` sentinels resolved at run time instead of a fixed address
constexpr std::string_view c_toRpc = "";  // not a contract call: `signature` is an RPC method
constexpr std::string_view c_toHello = "@hello";              // HelloWorld deployed by the caller
constexpr std::string_view c_toCommittee = "@committee";      // CommitteeManager._committee()
constexpr std::string_view c_toProposalMgr = "@proposalMgr";  // CommitteeManager._proposalMgr()

struct Preset
{
    std::string_view group;      // consensus / sysconfig / balance / auth / committee / hello
    std::string_view name;       // add-sealer
    std::string_view console;    // console command name for help and the CI mapping table
    std::string_view to;         // 0x-prefixed 40-hex address, c_toRpc, or a sentinel above
    std::string_view signature;  // addSealer(string,uint256)
    std::span<const ParamSpec> params;
    std::string_view outputs;  // comma-separated output types ("" = none); fragment via outputsAbi
    Access access;
    std::optional<ProposalSpec> proposal;  // auth-chain replacement; nullopt = direct everywhere
    bool write;                            // needs a key and a confirmation

    std::string methodName() const;  // "addSealer" from the signature
    bool isRpc() const { return to.empty(); }
    /// true when the output is a single int32 code written by the precompiled (status stays 0
    /// on failure, the code is negative)
    bool returnsCode() const { return outputs == "int32"; }
    /// minimal ABI JSON fragment with one function entry (inputs from the signature, outputs
    /// from `outputs`) for Abi::decodeOutput; empty when there are no outputs
    std::string outputsAbi() const;
};

std::span<const Preset> allPresets();
std::vector<Preset const*> presetsOf(std::string_view _group);
/// nullptr when unknown
Preset const* findPreset(std::string_view _group, std::string_view _name);
/// parameter types of a solidity signature: "addSealer(string,uint256)" → {"string","uint256"}
std::vector<std::string> signatureInputTypes(std::string_view _signature);
}  // namespace bcos::ops
