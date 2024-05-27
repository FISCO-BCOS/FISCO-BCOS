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
 * @brief The serial transaction execute context without coroutine
 * @file PromiseTransactionExecutive.h
 * @author: jimmyshi
 * @date: 2022-07-19
 */

#pragma once

#include "CoroutineTransactionExecutive.h"
#include "SyncStorageWrapper.h"
#include "TransactionExecutive.h"
#include <boost/coroutine2/coroutine.hpp>

namespace bcos
{
namespace executor
{
class MessagePromiseSwapper
{
public:
    using Ptr = std::shared_ptr<MessagePromiseSwapper>;
    MessagePromiseSwapper(ThreadPool::Ptr pool) : m_pool(pool) {}
    void spawnAndCall(std::function<CallParameters::UniquePtr()> spawnCall,
        std::function<void(CallParameters::UniquePtr)> waitAndDo)
    {
        assert(m_pool);

        auto lastPromise = m_currentPromise;

        m_currentPromise = std::make_shared<std::promise<CallParameters::UniquePtr>>();
        m_pool->enqueue([this, lastPromise, spawnCall = std::move(spawnCall)]() {
            auto message = spawnCall();

            auto promise = lastPromise ? lastPromise : m_currentPromise;
            // std::cout << "resume " << promise.get() << std::endl;
            promise->set_value(std::move(message));
        });

        // std::cout << "await " << m_currentPromise.get() << std::endl;
        auto message = m_currentPromise->get_future().get();

        waitAndDo(std::move(message));
    }

private:
    std::shared_ptr<std::promise<CallParameters::UniquePtr>> m_lastPromise;
    std::shared_ptr<std::promise<CallParameters::UniquePtr>> m_currentPromise;

    ThreadPool::Ptr m_pool;
};


class PromiseTransactionExecutive : public CoroutineTransactionExecutive
{
public:
    using Ptr = std::shared_ptr<PromiseTransactionExecutive>;


    PromiseTransactionExecutive(ThreadPool::Ptr pool, const BlockContext& blockContext,
        std::string contractAddress, int64_t contextID, int64_t seq,
        const wasm::GasInjector& gasInjector)
      : CoroutineTransactionExecutive(
            blockContext, std::move(contractAddress), contextID, seq, gasInjector),
        m_messageSwapper(std::make_shared<MessagePromiseSwapper>(pool))
    {}

    CallParameters::UniquePtr start(CallParameters::UniquePtr input) override;  // start a new
    // coroutine to
    // execute

    // External call request
    CallParameters::UniquePtr externalCall(CallParameters::UniquePtr input) override;  // call by
    // hostContext

    // Execute finish and waiting for FINISH or REVERT
    CallParameters::UniquePtr waitingFinish(CallParameters::UniquePtr input) override;

    // External request key locks, throw exception if dead lock detected
    void externalAcquireKeyLocks(std::string acquireKeyLock);


    virtual CallParameters::UniquePtr resume() override
    {
        CallParameters::UniquePtr exchangeMessage;
        m_messageSwapper->spawnAndCall([this]() { return std::move(m_exchangeMessage); },
            [&exchangeMessage](
                CallParameters::UniquePtr message) { exchangeMessage = std::move(message); });

        return exchangeMessage;
    }

    MessagePromiseSwapper::Ptr getPromiseMessageSwapper() { return m_messageSwapper; }
    void setPromiseMessageSwapper(MessagePromiseSwapper::Ptr swapper)
    {
        m_messageSwapper = swapper;
    }

private:
    MessagePromiseSwapper::Ptr m_messageSwapper;
};
}  // namespace executor
}  // namespace bcos
