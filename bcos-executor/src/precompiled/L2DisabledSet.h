/*
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file L2DisabledSet.h
 * @brief FISCO-private stateful precompiles that become invisible in L2 mode
 *        (feature_l2_ethereum_compat). When the flag is unset (pbft mode) the
 *        predicate is a no-op and behavior is identical to today. (A6.8)
 */
#pragma once
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/ledger/Features.h>
#include <array>
#include <string_view>

namespace bcos::executor
{

/// The 18 FISCO-private precompiles hidden when running in L2 mode. Split into
/// two groups by the dispatch path that reaches them in HostContext:
///
/// 1. m_precompiled (stateful) — gated via the PrecompiledMap predicate. The
///    predicate is `disabledInL2()` (or `predicateAnd(existing, disabledInL2())`
///    when an older gate already exists).
/// 2. m_staticPrecompiled (stateless) — CAST/GROUP_SIG/RING_SIG used to live in
///    the static-precompile set, which `HostContext::call` checks BEFORE the
///    PrecompiledMap predicate path. To keep the L2-disable gating effective
///    on these three, they are removed from the static set in
///    TransactionExecutor::initEvmEnvironment (so they flow through the
///    predicate path) — see PR review thread on #5286 for the rationale.
///    DISCRETE_ZKP and PAILLIER were already on the predicate path.
///
/// Each entry is the 40-hex EVM address constant from PrecompiledTypeDef.h.
inline constexpr auto kL2DisabledSet = std::to_array<std::string_view>({
    precompiled::SYS_CONFIG_ADDRESS,            // 0x1000  SystemConfig
    precompiled::TABLE_ADDRESS,                 // 0x1001  Table
    precompiled::TABLE_MANAGER_ADDRESS,         // 0x1002  TableManager
    precompiled::CONSENSUS_ADDRESS,             // 0x1003  Consensus
    precompiled::AUTH_MANAGER_ADDRESS,          // 0x1005  AuthManager
    precompiled::KV_TABLE_ADDRESS,              // 0x1009  KVTable
    precompiled::DAG_TRANSFER_ADDRESS,          // 0x100c  DagTransfer
    precompiled::BFS_ADDRESS,                   // 0x100e  BFS
    precompiled::CAST_ADDRESS,                  // 0x100f  Cast (was static)
    precompiled::SHARDING_PRECOMPILED_ADDRESS,  // 0x1010  Sharding
    precompiled::BALANCE_PRECOMPILED_ADDRESS,   // 0x1011  Balance
    precompiled::AUTH_CONTRACT_MGR_ADDRESS,     // 0x10002 ContractAuthMgr
    precompiled::ACCOUNT_MGR_ADDRESS,           // 0x10003 AccountManager
    precompiled::ACCOUNT_ADDRESS,               // 0x10004 Account
    precompiled::PAILLIER_ADDRESS,              // 0x5003  Paillier
    precompiled::GROUP_SIG_ADDRESS,             // 0x5004  GroupSig (was static)
    precompiled::RING_SIG_ADDRESS,              // 0x5005  RingSig (was static)
    precompiled::DISCRETE_ZKP_ADDRESS,          // 0x5100  DiscreteZKP
});
static_assert(kL2DisabledSet.size() == 18, "kL2DisabledSet must hold exactly 18 entries");

// Compile-time guard: no member may collide with the Ethereum single-byte
// precompile range 0x01-0x0a. Those serialize to a 40-hex address with 38
// leading zeros ("00..0" + 2 hex). Every FISCO address has a non-zero digit
// before index 38, so none starts with 38 zeros.
namespace detail
{
inline constexpr std::string_view k38Zeros = "00000000000000000000000000000000000000";
static_assert(k38Zeros.size() == 38, "expected 38 zero characters");
inline constexpr bool noEthereumRangeCollision()
{
    for (auto addr : kL2DisabledSet)
    {
        if (addr.starts_with(k38Zeros))
        {
            return false;
        }
    }
    return true;
}
// Compile-time guard: no member may collide with the OP predeploy range,
// whose addresses begin with the high byte 0x42 ("4200..."). FISCO system
// precompiles live in [0x1000, 0x20000) (PrecompiledTypeDef.h), so their
// 40-hex form has 36+ leading zeros and cannot start with "4200". This guard
// protects against a future edit adding an out-of-range address, not the
// current members.
inline constexpr bool noOpPredeployCollision()
{
    for (auto addr : kL2DisabledSet)
    {
        if (addr.starts_with("4200"))
        {
            return false;
        }
    }
    return true;
}
}  // namespace detail
static_assert(detail::noEthereumRangeCollision(),
    "kL2DisabledSet member collides with Ethereum 0x01-0x0a precompile range");
static_assert(detail::noOpPredeployCollision(),
    "kL2DisabledSet member collides with OP 0x42.. predeploy range");

/// Returns a PrecompiledMap predicate (uint32_t version, bool isAuth, Features const&).
/// Result is `false` (precompile hidden) iff feature_l2_ethereum_compat is set;
/// otherwise `true` (visible, identical to pbft mode).
inline auto disabledInL2()
{
    return [](uint32_t /*version*/, bool /*isAuth*/, ledger::Features const& features) -> bool {
        return !features.get(ledger::Features::Flag::feature_l2_ethereum_compat);
    };
}

/// Composes two PrecompiledMap predicates with logical AND. Used to layer
/// disabledInL2() on top of an existing feature gate (e.g. Sharding/Balance):
/// the precompile is visible only when BOTH predicates hold.
template <typename A, typename B>
auto predicateAnd(A a, B b)
{
    return [a = std::move(a), b = std::move(b)](
               uint32_t version, bool isAuth, ledger::Features const& features) -> bool {
        return a(version, isAuth, features) && b(version, isAuth, features);
    };
}

}  // namespace bcos::executor
