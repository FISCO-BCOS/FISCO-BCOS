/**
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
 * @file AnyEngineService.h
 * @brief Type-erased wrapper around any type satisfying EngineServiceConcept,
 *        using Microsoft proxy library's facade for zero-overhead dispatch.
 */

#pragma once

#include "Types.h"
#include "bcos-task/Task.h"
#include <proxy/proxy.h>
#include <cassert>
#include <optional>
#include <string>
#include <vector>

namespace bcos::engine
{

/// Dispatch structs — each maps to a member function of the underlying
/// EngineService implementation.
PRO_DEF_MEM_DISPATCH(MemExchangeCapabilities, exchangeCapabilities);
PRO_DEF_MEM_DISPATCH(MemUpdateForkchoice, updateForkchoice);
PRO_DEF_MEM_DISPATCH(MemGetPayload, getPayload);
PRO_DEF_MEM_DISPATCH(MemNewPayload, newPayload);
PRO_DEF_MEM_DISPATCH(MemGetSafeBlockNumber, getSafeBlockNumber);
PRO_DEF_MEM_DISPATCH(MemGetFinalizedBlockNumber, getFinalizedBlockNumber);

/// Facade declaring the EngineServiceConcept interface for proxy.
struct AnyEngineServiceFacade
  : pro::facade_builder ::add_convention<MemExchangeCapabilities,
        task::Task<std::vector<std::string>>(
            std::vector<std::string>)>::add_convention<MemUpdateForkchoice,
        task::Task<ForkchoiceUpdatedResult>(const ForkchoiceState&, const PayloadAttributes*,
            std::uint32_t)>::add_convention<MemGetPayload,
        task::Task<GetPayloadResult>(const PayloadID&, std::uint32_t)>::
        add_convention<MemNewPayload, task::Task<PayloadStatus>(const NewPayloadRequest&,
                                          std::uint32_t)>::add_convention<MemGetSafeBlockNumber,
            std::optional<bcos::protocol::BlockNumber>()
                const>::add_convention<MemGetFinalizedBlockNumber,
            std::optional<bcos::protocol::BlockNumber>()
                const>::support_relocation<pro::constraint_level::nothrow>::
            support_destruction<pro::constraint_level::nothrow>::build
{
};

/// Type-erased owning wrapper around any type satisfying EngineServiceConcept.
///
/// This is a simple alias for pro::proxy<AnyEngineServiceFacade>.
/// Use pro::make_proxy<AnyEngineServiceFacade>(...) to construct, and
/// operator-> for dispatch (pointer semantics):
///
/// Usage:
/// @code
///   // For non-movable types (recommended):
///   auto any = pro::make_proxy<AnyEngineServiceFacade, EngineServiceImpl<...>>(
///       memPool, storage, ...);
///   auto result = co_await any->updateForkchoice(state, nullptr, 1);
/// @endcode
using AnyEngineService = pro::proxy<AnyEngineServiceFacade>;

}  // namespace bcos::engine
