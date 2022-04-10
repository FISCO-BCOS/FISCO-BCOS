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
    auto executiveState = m_executives.find({contextID, seq});
    if (executiveState == m_executives.end())
    {
        // add to top if not exists
        // std::cout << " push " << contextID << " | " << seq << " | " << txInput->type <<
        // std::endl;
        auto newExecutiveState =
            std::make_shared<ExecutiveState>(m_executiveFactory, std::move(txInput));
        m_runPool.push(newExecutiveState);
        m_executives[{contextID, seq}] = newExecutiveState;
    }
    else
    {
        // update resume params
        // std::cout << " rsme " << contextID << " | " << seq << " | " << txInput->type <<
        // std::endl;
        executiveState->second->setResumeParam(std::move(txInput));
    }
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

void ExecutiveStackFlow::asyncRun(std::function<void(CallParameters::UniquePtr)> onTxFinished,
    std::function<void(std::shared_ptr<std::vector<CallParameters::UniquePtr>>)> onPaused,
    std::function<void(bcos::Error::UniquePtr)> onFinished)
{
    asyncTo([this, onTxFinished = std::move(onTxFinished), onPaused = std::move(onPaused),
                onFinished = std::move(onFinished)]() { run(onTxFinished, onPaused, onFinished); });
}

void ExecutiveStackFlow::run(std::function<void(CallParameters::UniquePtr)> onTxFinished,
    std::function<void(std::shared_ptr<std::vector<CallParameters::UniquePtr>>)> onPaused,
    std::function<void(bcos::Error::UniquePtr)> onFinished)
{
    try
    {
        bcos::WriteGuard lock(x_lock);
        while (!m_runPool.empty())
        {
            auto executiveState = m_runPool.top();
            CallParameters::UniquePtr output;

            // try to execute this state
            switch (executiveState->getStatus())
            {
            case ExecutiveState::NEED_RUN:
            case ExecutiveState::NEED_RESUME:
            {
                // exec this state
                output = executiveState->go();
                break;
            }
            case ExecutiveState::PAUSED:
            {
                // haven't been executed since last pause step
                output = nullptr;
                break;
            }
            case ExecutiveState::FINISHED:
            {
                m_runPool.pop();
                continue;
                break;
            }
            }

            // check this state after exec
            switch (executiveState->getStatus())
            {
            case ExecutiveState::NEED_RUN:
            case ExecutiveState::NEED_RESUME:
            {
                asyncTo([onFinished = std::move(onFinished)]() { onFinished(nullptr); });
                return;
            }
            case ExecutiveState::PAUSED:
            {  // just ignore, need to set resume params
                auto outputs = std::make_shared<std::vector<CallParameters::UniquePtr>>();
                if (output)
                {
                    outputs->push_back(std::move(output));
                }
                asyncTo([onPaused = std::move(onPaused), outputs = std::move(outputs)]() {
                    onPaused(outputs);
                });
                return;
            }
            case ExecutiveState::FINISHED:
            {
                onTxFinished(std::move(output));
                break;
            }
            }
            m_runPool.pop();
        }
        // asyncTo([onFinished = std::move(onFinished)]() {
        //  return
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