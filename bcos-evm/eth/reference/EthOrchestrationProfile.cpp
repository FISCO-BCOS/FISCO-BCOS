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
 * @file EthOrchestrationProfile.cpp
 */

#include "bcos-evm/eth/reference/EthOrchestrationProfile.h"

namespace bcos::evm
{

EthPrecheckPolicy EthOrchestrationProfile::buildPrecheckPolicy(BindingsContext& bindingsCtx)
{
    return EthPrecheckPolicy{bindingsCtx.input};
}

EthOrchestrationErrorPolicy EthOrchestrationProfile::buildErrorPolicy(
    BindingsContext const& /*bindingsCtx*/)
{
    return EthOrchestrationErrorPolicy{};
}

EthOrchestrationProfile::Bindings EthOrchestrationProfile::bind(BindingsContext& bindingsCtx)
{
    return Bindings{buildPrecheckPolicy(bindingsCtx), buildErrorPolicy(bindingsCtx)};
}

}  // namespace bcos::evm
