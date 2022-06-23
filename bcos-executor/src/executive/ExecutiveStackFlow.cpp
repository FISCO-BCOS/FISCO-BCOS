/*
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief interface definition of ExecutiveFlow
 * @file ExecutiveStackFlow.cpp
 * @author: jimmyshi
 * @date: 2022-03-22
 */

#include "ExecutiveStackFlow.h"
#include "../Common.h"

using namespace bcos;
using namespace bcos::executor;

void ExecutiveStackFlow::submit(CallParameters::UniquePtr txInput)
{
    auto contextID = txInput->contextID;
    auto seq = txInput->seq;
    auto type = txInput->type;
    auto executiveState = m_executives[{contextID, seq}];
    if (executiveState == nullptr)
    {
        // add to top if not exists
        executiveState = std::make_shared<ExecutiveState>(m_executiveFactory, std::move(txInput));
        m_executives[{contextID, seq}] = executiveState;
    }
    else
    {
        // update resume params
        executiveState->setResumeParam(std::move(txInput));
    }

    if (seq == 0 && type == CallParameters::MESSAGE)
    {
        // the tx has not been executed ever (created by a user)
        m_originFlow.push(executiveState);
    }
    else
    {
        // the tx is not first run:
        // 1. created by sending from a contract
        // 2. is a revert message, seq = 0 but type = REVERT
        m_pausedPool.erase({contextID, seq});
        m_waitingFlow.insert({contextID, seq});
    };
}

void ExecutiveStackFlow::submit(std::shared_ptr<std::vector<CallParameters::UniquePtr>> txInputs)
{
    WriteGuard lock(x_lock);

    // from back to front, push in stack, so stack's tx can be executed from top
    for (unsigned long i = 0; i < txInputs->size(); i++)
    {
        submit(std::move((*txInputs)[i]));
    }
}

void ExecutiveStackFlow::asyncRun(std::function<void(CallParameters::UniquePtr)> onTxReturn,
    std::function<void(bcos::Error::UniquePtr)> onFinished)
{
    asyncTo([this, onTxReturn = std::move(onTxReturn), onFinished = std::move(onFinished)]() {
        run(onTxReturn, onFinished);
    });
}

void ExecutiveStackFlow::run(std::function<void(CallParameters::UniquePtr)> onTxReturn,
    std::function<void(bcos::Error::UniquePtr)> onFinished)
{
    // origin flow: all messages received in first DMC iteration. if paused, move to paused pool
    // paused poll: all paused messages during DMC iteration. if resumed, move to waiting pool
    // waiting flow: include all messages received during DMC iteration.

    // These three pool above run as a stack manner.
    // We must run waiting flow before origin flow and push message in waiting flow during DMC
    // iteration.

    try
    {
        bcos::WriteGuard lock(x_lock);

        // must run all messages in waiting pool before origin pool
        if (!m_waitingFlow.empty())
        {
            runWaitingFlow(onTxReturn);
        }

        if (m_pausedPool.empty())
        {
            // origin flow can only be run if there is no paused message
            runOriginFlow(onTxReturn);
        }

        onFinished(nullptr);
    }
    catch (std::exception& e)
    {
        EXECUTIVE_LOG(ERROR) << "ExecutiveStackFlow run error: "
                             << boost::diagnostic_information(e);
        onFinished(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "ExecutiveStackFlow run error", e));
    }
}


void ExecutiveStackFlow::runWaitingFlow(std::function<void(CallParameters::UniquePtr)> onTxReturn)
{
    std::vector<std::string> lastKeyLocks;
    auto callback = [&lastKeyLocks, onTxReturn = std::move(onTxReturn)](
                        CallParameters::UniquePtr output) {
        if (output->type == CallParameters::MESSAGE || output->type == CallParameters::KEY_LOCK)
        {
            lastKeyLocks = output->keyLocks;
        }
        else
        {
            lastKeyLocks = {};
        }

        onTxReturn(std::move(output));
    };

    for (auto contextIDAndSeq : m_waitingFlow)
    {
        auto executiveState = m_executives[contextIDAndSeq];
        executiveState->appendKeyLocks(std::move(lastKeyLocks));

        runOne(executiveState, callback);
    }

    m_waitingFlow.clear();
}

void ExecutiveStackFlow::runOriginFlow(std::function<void(CallParameters::UniquePtr)> onTxReturn)
{
    while (!m_originFlow.empty())
    {
        auto executiveState = m_originFlow.front();
        m_originFlow.pop();
        runOne(executiveState, onTxReturn);

        if (executiveState->getStatus() == ExecutiveState::PAUSED)
        {
            break;  // break at once paused
        }
    }
}

void ExecutiveStackFlow::runOne(
    ExecutiveState::Ptr executiveState, std::function<void(CallParameters::UniquePtr)> onTxReturn)
{
    CallParameters::UniquePtr output;

    output = executiveState->go();

    switch (executiveState->getStatus())
    {
    case ExecutiveState::NEED_RUN:
    case ExecutiveState::NEED_RESUME:
    {
        // assume never goes here

        assert(false);
        EXECUTIVE_LOG(FATAL) << "Invalid executiveState type";
        break;
    }
    case ExecutiveState::PAUSED:
    {
        m_pausedPool.insert({executiveState->getContextID(), executiveState->getSeq()});
        onTxReturn(std::move(output));
        break;
    }
    case ExecutiveState::FINISHED:
    {
        onTxReturn(std::move(output));
        break;
    }
    }
}