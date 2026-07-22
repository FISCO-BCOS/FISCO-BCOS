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
 * @file EthOrchestrationProfile.h
 */

#pragma once

#include "bcos-evm/eth/reference/EthOrchestrationErrorPolicy.h"
#include "bcos-evm/eth/reference/EthPrecheckPolicy.h"
#include "bcos-evm/eth/reference/EthReferenceBridge.h"

namespace bcos::evm
{

struct EthOrchestrationProfile
{
    /// Orchestration policy bind input (not kernel ExecutionSession; ADR-027 naming follow-up).
    struct BindingsContext
    {
        EthReferenceRequest const& input;
        EthReferenceResult& output;
    };

    struct Bindings
    {
        EthPrecheckPolicy precheckPolicy;
        EthOrchestrationErrorPolicy errorPolicy;
    };

    static EthPrecheckPolicy buildPrecheckPolicy(BindingsContext& bindingsCtx);
    static EthOrchestrationErrorPolicy buildErrorPolicy(BindingsContext const& bindingsCtx);
    static Bindings bind(BindingsContext& bindingsCtx);
};

}  // namespace bcos::evm
