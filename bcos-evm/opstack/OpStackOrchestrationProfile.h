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
 * @file OpStackOrchestrationProfile.h
 */

#pragma once

#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-evm/opstack/OpStackOrchestrationErrorPolicy.h"
#include "bcos-evm/opstack/OpStackPrecheckPolicy.h"
#include "bcos-evm/opstack/OpStackSettlementView.h"

namespace bcos::evm
{

struct OpStackOrchestrationProfile
{
    /// Orchestration policy bind input (not kernel ExecutionSession; ADR-027 naming follow-up).
    struct BindingsContext
    {
        OpStackExecutionRequest const& input;
        OpStackSettlementView view;
    };

    struct Bindings
    {
        OpStackPrecheckPolicy precheckPolicy;
        OpStackOrchestrationErrorPolicy errorPolicy;
    };

    static OpStackPrecheckPolicy buildPrecheckPolicy(BindingsContext& bindingsCtx);
    static OpStackOrchestrationErrorPolicy buildErrorPolicy(BindingsContext const& bindingsCtx);
    static Bindings bind(BindingsContext& bindingsCtx);
};

}  // namespace bcos::evm
