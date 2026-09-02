/*
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
 * @file EngineStartupGates.h
 * @brief Pure decision function for the Engine API startup gates
 *        (unit-tested in libinitializer/tests/EngineStartupGatesTest.cpp;
 *         Initializer::init applies the result and owns the warning log).
 */

#pragma once

#include "MultiVersionScheduler.h"  // scheduler_v1::ETHEREUM/OPSTACK_EXECUTOR_VERSION
#include "bcos-tool/Exceptions.h"
#include <boost/exception/info.hpp>
#include <string>

namespace bcos::initializer
{
/// Outcome of the [op_engine_rpc]-on-a-v1-executor combination.
enum class V1ExecutorEscape
{
    NotApplicable,       // op_engine_rpc not enabled, or the executor is v2+
    AllowedWithWarning,  // unsafe_allow_v1_executor=true: caller must log the harness warning
};

/// Refuse-at-startup combinations for the Engine API surface. Throws
/// bcos::tool::InvalidConfig; the messages name the exact config fix.
///
/// Gates (order matters — the OP gate reports first):
/// 1. executor_version >= OPSTACK_EXECUTOR_VERSION without engine-driven block production:
///    the legacy PBFT pipeline would drive the OpScheduler slot with non-engine flow.
/// 2. [op_engine_rpc] on a v1 executor: an external op-node trusts the EL and never
///    cross-checks state roots, so v1 (non-Ethereum) semantics must not be served to it.
///    The only escape is the explicit test-only unsafe_allow_v1_executor flag.
inline V1ExecutorEscape checkEngineStartupGates(int executorVersion,
    bool engineDrivenBlockProduction, bool enableOpEngineRpc, bool opEngineAllowV1Executor)
{
    bool const engineApiForV1Only = (executorVersion < scheduler_v1::ETHEREUM_EXECUTOR_VERSION);
    bool const opStackMode = (executorVersion >= scheduler_v1::OPSTACK_EXECUTOR_VERSION);

    if (opStackMode && !engineDrivenBlockProduction)
    {
        BOOST_THROW_EXCEPTION(
            bcos::tool::InvalidConfig{} << bcos::errinfo_comment(
                "executor_version >= 3 (OP mode) requires engine-driven block "
                "production: set [op_engine_rpc] enable=true (external op-node) "
                "or [consensus] enable_single_node_consensus=true (built-in CL)"));
    }

    if (enableOpEngineRpc && engineApiForV1Only)
    {
        if (!opEngineAllowV1Executor)
        {
            BOOST_THROW_EXCEPTION(
                bcos::tool::InvalidConfig{} << bcos::errinfo_comment(
                    "op_engine_rpc requires executor_version >= " +
                    std::to_string(scheduler_v1::ETHEREUM_EXECUTOR_VERSION) +
                    " (the pure-Ethereum executor): on executor_version < 2 the endpoint "
                    "would serve v1 semantics that diverge from an OP Stack chain. For the "
                    "v1 Engine API test harness only, set [op_engine_rpc] "
                    "unsafe_allow_v1_executor=true"));
        }
        return V1ExecutorEscape::AllowedWithWarning;
    }
    return V1ExecutorEscape::NotApplicable;
}
}  // namespace bcos::initializer
