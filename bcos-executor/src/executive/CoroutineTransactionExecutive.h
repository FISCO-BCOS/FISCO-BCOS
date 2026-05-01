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
 * @brief Execute transaction context with coroutine
 * @file CoroutineTransactionExecutive.h
 * @author: jimmyshi
 * @date: 2022-07-19
 */

#pragma once

#include "SyncStorageWrapper.h"
#include "TransactionExecutive.h"
#include <boost/coroutine2/coroutine.hpp>

namespace bcos::executor
{

class CoroutineTransactionExecutive : public TransactionExecutive
{
public:
    using Ptr = std::shared_ptr<CoroutineTransactionExecutive>;


    class ResumeHandler;

    using CoroutineMessage = std::function<void(ResumeHandler resume)>;
    using Coroutine = boost::coroutines2::coroutine<CoroutineMessage>;

    class ResumeHandler
    {
    public:
        ResumeHandler(CoroutineTransactionExecutive& executive);

        void operator()();

    private:
        CoroutineTransactionExecutive& m_executive;
    };


    CoroutineTransactionExecutive(const BlockContext& blockContext, std::string contractAddress,
        int64_t contextID, int64_t seq, const wasm::GasInjector& gasInjector);

    CallParameters::UniquePtr start(CallParameters::UniquePtr input) override;  // start a new
    // coroutine to
    // execute

    // External call request
    CallParameters::UniquePtr externalCall(CallParameters::UniquePtr input) override;  // call by
    // hostContext

    // Execute finish and waiting for FINISH or REVERT
    virtual CallParameters::UniquePtr waitingFinish(CallParameters::UniquePtr input);

    // External request key locks, throw exception if dead lock detected
    void externalAcquireKeyLocks(std::string acquireKeyLock);

    virtual void setExchangeMessage(CallParameters::UniquePtr callParameters);

    std::string getExchangeMessageStr();


    virtual void appendResumeKeyLocks(std::vector<std::string> keyLocks);

    virtual CallParameters::UniquePtr resume();

    virtual std::optional<Coroutine::pull_type>& getPullMessage();
    virtual std::optional<Coroutine::push_type>& getPushMessage();
    virtual CallParameters::UniquePtr& getExchangeMessageRef();

    std::shared_ptr<SyncStorageWrapper> getSyncStorageWrapper();

protected:
    CallParameters::UniquePtr m_exchangeMessage = nullptr;
    std::shared_ptr<SyncStorageWrapper> m_syncStorageWrapper;

private:
    CallParameters::UniquePtr dispatcher();
    void spawnAndCall(std::function<void(ResumeHandler)> function);

    std::optional<Coroutine::pull_type> m_pullMessage;
    std::optional<Coroutine::push_type> m_pushMessage;
};
}  // namespace bcos::executor
