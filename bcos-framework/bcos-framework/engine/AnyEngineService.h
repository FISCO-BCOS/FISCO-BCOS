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
 * @brief Type-erased wrapper around any type satisfying EngineServiceConcept
 */

#pragma once

#include "bcos-framework/engine/EngineService.h"
#include "bcos-task/Task.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bcos::engine
{

/// Type-erased wrapper around any type satisfying EngineServiceConcept.
///
/// Uses the "PIMPL with virtual Concept/Model" pattern to erase the concrete
/// type while still providing the full Engine API.  The stored implementation
/// must satisfy EngineServiceConcept.
///
/// Usage:
/// @code
///   EngineService<MyMemPool, MyStorage, MyExecutor, MyScheduler> concrete{...};
///   AnyEngineService any(std::move(concrete));
///   auto result = co_await any.updateForkchoice(state, nullptr, 1);
/// @endcode
class AnyEngineService
{
private:
    /// Virtual interface for type-erased engine operations.
    struct Concept
    {
        virtual ~Concept() = default;
        virtual task::Task<std::vector<std::string>> exchangeCapabilities(
            std::vector<std::string> remoteCapabilities) = 0;
        virtual task::Task<ForkchoiceUpdatedResult> updateForkchoice(
            const ForkchoiceState& forkchoiceState, const PayloadAttributes* payloadAttributes,
            std::uint32_t version) = 0;
        virtual task::Task<GetPayloadResult> getPayload(
            const PayloadID& payloadId, std::uint32_t version) = 0;
        virtual task::Task<PayloadStatus> newPayload(
            const NewPayloadRequest& request, std::uint32_t version) = 0;
        virtual std::optional<bcos::protocol::BlockNumber> getSafeBlockNumber() const = 0;
        virtual std::optional<bcos::protocol::BlockNumber> getFinalizedBlockNumber() const = 0;
    };

    /// Concrete model wrapping a type-erased EngineService implementation.
    template <class T>
        requires EngineServiceConcept<T>
    struct Model final : Concept
    {
        T m_engine;

        template <class U>
        explicit Model(U&& engine) : m_engine(std::forward<U>(engine))
        {}

        task::Task<std::vector<std::string>> exchangeCapabilities(
            std::vector<std::string> remoteCapabilities) override
        {
            co_return co_await m_engine.exchangeCapabilities(std::move(remoteCapabilities));
        }

        task::Task<ForkchoiceUpdatedResult> updateForkchoice(
            const ForkchoiceState& forkchoiceState, const PayloadAttributes* payloadAttributes,
            std::uint32_t version) override
        {
            co_return co_await m_engine.updateForkchoice(
                forkchoiceState, payloadAttributes, version);
        }

        task::Task<GetPayloadResult> getPayload(
            const PayloadID& payloadId, std::uint32_t version) override
        {
            co_return co_await m_engine.getPayload(payloadId, version);
        }

        task::Task<PayloadStatus> newPayload(
            const NewPayloadRequest& request, std::uint32_t version) override
        {
            co_return co_await m_engine.newPayload(request, version);
        }

        std::optional<bcos::protocol::BlockNumber> getSafeBlockNumber() const override
        {
            return m_engine.getSafeBlockNumber();
        }

        std::optional<bcos::protocol::BlockNumber> getFinalizedBlockNumber() const override
        {
            return m_engine.getFinalizedBlockNumber();
        }
    };

    std::unique_ptr<Concept> m_impl;

public:
    AnyEngineService() = default;
    ~AnyEngineService() = default;

    AnyEngineService(const AnyEngineService&) = delete;
    AnyEngineService& operator=(const AnyEngineService&) = delete;

    AnyEngineService(AnyEngineService&&) noexcept = default;
    AnyEngineService& operator=(AnyEngineService&&) noexcept = default;

    /// Construct from any type satisfying EngineServiceConcept.
    template <class T>
        requires EngineServiceConcept<std::remove_cvref_t<T>> &&
                 (!std::same_as<std::remove_cvref_t<T>, AnyEngineService>)
    explicit AnyEngineService(T&& engine)
      : m_impl(std::make_unique<Model<std::remove_cvref_t<T>>>(std::forward<T>(engine)))
    {}

    /// Returns true if this wrapper holds an engine implementation.
    explicit operator bool() const noexcept { return m_impl != nullptr; }

    task::Task<std::vector<std::string>> exchangeCapabilities(
        std::vector<std::string> remoteCapabilities)
    {
        co_return co_await m_impl->exchangeCapabilities(std::move(remoteCapabilities));
    }

    task::Task<ForkchoiceUpdatedResult> updateForkchoice(
        const ForkchoiceState& forkchoiceState, const PayloadAttributes* payloadAttributes,
        std::uint32_t version)
    {
        co_return co_await m_impl->updateForkchoice(forkchoiceState, payloadAttributes, version);
    }

    task::Task<GetPayloadResult> getPayload(const PayloadID& payloadId, std::uint32_t version)
    {
        co_return co_await m_impl->getPayload(payloadId, version);
    }

    task::Task<PayloadStatus> newPayload(const NewPayloadRequest& request, std::uint32_t version)
    {
        co_return co_await m_impl->newPayload(request, version);
    }

    std::optional<bcos::protocol::BlockNumber> getSafeBlockNumber() const
    {
        return m_impl->getSafeBlockNumber();
    }

    std::optional<bcos::protocol::BlockNumber> getFinalizedBlockNumber() const
    {
        return m_impl->getFinalizedBlockNumber();
    }
};

/// Verify that AnyEngineService itself satisfies the concept (reflexive check).
static_assert(EngineServiceConcept<AnyEngineService>,
    "AnyEngineService must satisfy EngineServiceConcept");

}  // namespace bcos::engine
