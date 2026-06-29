/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief Unified execution frame entry (top-level and nested).
 * @file ExecutionFrame.h
 */

#pragma once

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/execution/FrameScope.h"
#include "bcos-evm/eth/state/VmHostPolicy.h"
#include <evmc/evmc.hpp>

namespace bcos::evm
{
struct ChainCallTargetPort;
}

namespace bcos::evm::state
{
class State;
class EthHost;
}  // namespace bcos::evm::state

namespace bcos::evm::execution
{

struct FrameContext
{
    state::State& state;
    evmc::VM& vm;
    bcos::evm_standard::RevisionConfig const& revisionConfig;
    state::VmHostPolicy* extension{nullptr};
    ChainCallTargetPort* chainPort{nullptr};
    evmc_address txOrigin{};
    evmc_address& executionAddress;
    bool fixNonceInit{false};

    FrameContext(state::State& state_, evmc::VM& vm_,
        bcos::evm_standard::RevisionConfig const& revisionConfig_, state::VmHostPolicy* extension_,
        evmc_address txOrigin_, evmc_address& executionAddress_, bool fixNonceInit_ = false,
        ChainCallTargetPort* chainPort_ = nullptr) noexcept
      : state(state_),
        vm(vm_),
        revisionConfig(revisionConfig_),
        extension(extension_),
        chainPort(chainPort_),
        txOrigin(txOrigin_),
        executionAddress(executionAddress_),
        fixNonceInit(fixNonceInit_)
    {}
};

struct FrameResult
{
    evmc::Result result{evmc_result{}};
    int64_t gasRefund{0};
    bool precompileHit{false};
};

FrameResult runExecutionFrame(
    FrameContext& ctx, evmc_message message, FrameScope scope, state::EthHost& host);

}  // namespace bcos::evm::execution
