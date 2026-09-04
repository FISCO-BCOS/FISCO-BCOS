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

#include "bcos-framework/engine/EngineService.h"
#include "bcos-task/Task.h"
#include <proxy/v3/proxy.h>
#include <cassert>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
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
/// Backed by pro::proxy<AnyEngineServiceFacade>.  This thin wrapper adapts
/// proxy's operator-> dispatch style to the direct-call style required by
/// the EngineServiceConcept.
///
/// Usage:
/// @code
///   // For non-movable types (recommended):
///   AnyEngineService any(std::in_place_type<EthEngineService<...>>, memPool, storage, ...);
///   auto result = co_await any.updateForkchoice(state, nullptr, 1);
/// @endcode
class AnyEngineService
{
public:
    using ProxyType = pro::proxy<AnyEngineServiceFacade>;

    AnyEngineService() = default;
    ~AnyEngineService() = default;

    AnyEngineService(const AnyEngineService&) = delete;
    AnyEngineService& operator=(const AnyEngineService&) = delete;

    AnyEngineService(AnyEngineService&&) noexcept = default;
    AnyEngineService& operator=(AnyEngineService&&) noexcept = default;

    /// Construct from any type satisfying EngineServiceConcept.
    /// The value is copied/moved into the proxy (owning semantics).
    template <class T>
        requires EngineServiceConcept<std::remove_cvref_t<T>> &&
                 (!std::same_as<std::remove_cvref_t<T>, AnyEngineService>)
    explicit AnyEngineService(T&& engine)
      : m_impl(pro::make_proxy<AnyEngineServiceFacade, std::remove_cvref_t<T>>(
            std::forward<T>(engine)))
    {}

    /// Construct in-place from constructor arguments (for non-movable types).
    template <class T, class... Args>
        requires EngineServiceConcept<T> && std::is_constructible_v<T, Args...>
    explicit AnyEngineService(std::in_place_type_t<T>, Args&&... args)
      : m_impl(pro::make_proxy<AnyEngineServiceFacade, T>(std::forward<Args>(args)...))
    {}

    /// Returns true if this wrapper holds an engine implementation.
    [[nodiscard]] explicit operator bool() const noexcept { return m_impl.has_value(); }

    task::Task<std::vector<std::string>> exchangeCapabilities(
        std::vector<std::string> remoteCapabilities)
    {
        assert(m_impl.has_value() && "AnyEngineService must be initialized before use");
        co_return co_await m_impl->exchangeCapabilities(std::move(remoteCapabilities));
    }

    task::Task<ForkchoiceUpdatedResult> updateForkchoice(const ForkchoiceState& forkchoiceState,
        const PayloadAttributes* payloadAttributes, std::uint32_t version)
    {
        assert(m_impl.has_value() && "AnyEngineService must be initialized before use");
        co_return co_await m_impl->updateForkchoice(forkchoiceState, payloadAttributes, version);
    }

    task::Task<GetPayloadResult> getPayload(const PayloadID& payloadId, std::uint32_t version)
    {
        assert(m_impl.has_value() && "AnyEngineService must be initialized before use");
        co_return co_await m_impl->getPayload(payloadId, version);
    }

    task::Task<PayloadStatus> newPayload(const NewPayloadRequest& request, std::uint32_t version)
    {
        assert(m_impl.has_value() && "AnyEngineService must be initialized before use");
        co_return co_await m_impl->newPayload(request, version);
    }

    std::optional<bcos::protocol::BlockNumber> getSafeBlockNumber() const
    {
        assert(m_impl.has_value() && "AnyEngineService must be initialized before use");
        return m_impl->getSafeBlockNumber();
    }

    std::optional<bcos::protocol::BlockNumber> getFinalizedBlockNumber() const
    {
        assert(m_impl.has_value() && "AnyEngineService must be initialized before use");
        return m_impl->getFinalizedBlockNumber();
    }

    /// Access the underlying proxy for advanced operations.
    [[nodiscard]] ProxyType& proxy() noexcept { return m_impl; }
    [[nodiscard]] const ProxyType& proxy() const noexcept { return m_impl; }

private:
    ProxyType m_impl;
};

/// Verify that AnyEngineService itself satisfies the concept (reflexive check).
static_assert(
    EngineServiceConcept<AnyEngineService>, "AnyEngineService must satisfy EngineServiceConcept");

}  // namespace bcos::engine
