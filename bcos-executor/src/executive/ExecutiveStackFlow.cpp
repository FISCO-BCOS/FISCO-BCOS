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
    auto executiveState = m_executives[{contextID, seq}];
    if (executiveState == nullptr)
    {
        // add to top if not exists
        /*
        std::cout << "[EXECUTOR] >>>> " << contextID << " | " << seq << " | " << txInput->toString()
                  << " | NEED_RUN" << std::endl;
                  */
        executiveState = std::make_shared<ExecutiveState>(m_executiveFactory, std::move(txInput));
        m_executives[{contextID, seq}] = executiveState;
    }
    else
    {
        // update resume params
        /*
        std::cout << "[EXECUTOR] >>>> " << contextID << " | " << seq << " | " << txInput->toString()
                  << " | NEED_RESUME" << std::endl;
                  */
        executiveState->setResumeParam(std::move(txInput));
    }

    if (!m_hasFirstRun)
    {
        m_originStack.push(executiveState);
    }
    else
    {
        m_pausedPool.erase({contextID, seq});
        m_waitingPool.insert({contextID, seq});
    };
}

void ExecutiveStackFlow::submit(std::shared_ptr<std::vector<CallParameters::UniquePtr>> txInputs)
{
    WriteGuard lock(x_lock);

    // from back to front, push in stack, so stack's tx can be executed from top
    for (auto i = txInputs->size(); i > 0; i--)
    {
        submit(std::move((*txInputs)[i - 1]));
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
    try
    {
        bcos::WriteGuard lock(x_lock);
        m_hasFirstRun = true;

        if (!m_waitingPool.empty())
        {
            runWaitingPool(onTxReturn);
        }

        if (m_pausedPool.empty())
        {
            runOriginStack(onTxReturn);
        }

        onFinished(nullptr);

        // });
    }
    catch (std::exception& e)
    {
        EXECUTIVE_LOG(ERROR) << "ExecutiveStackFlow run error: "
                             << boost::diagnostic_information(e);
        onFinished(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "ExecutiveStackFlow run error", e));
    }
}


void ExecutiveStackFlow::runWaitingPool(std::function<void(CallParameters::UniquePtr)> onTxReturn)
{
    for (auto contextIDAndSeq : m_waitingPool)
    {
        auto executiveState = m_executives[contextIDAndSeq];
        runOne(executiveState, onTxReturn);
    }

    m_waitingPool.clear();
}

void ExecutiveStackFlow::runOriginStack(std::function<void(CallParameters::UniquePtr)> onTxReturn)
{
    while (!m_originStack.empty())
    {
        auto executiveState = m_originStack.top();
        m_originStack.pop();
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