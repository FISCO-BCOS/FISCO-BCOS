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
 * @file ExecutiveQueueFlow.cpp
 * @author: jimmyshi
 * @date: 2022-03-22
 */

#include "ExecutiveQueueFlow.h"
#include "../Common.h"

using namespace bcos;
using namespace bcos::executor;

void ExecutiveQueueFlow::submit(CallParameters::UniquePtr txInput)
{
    auto contextID = txInput->contextID;
    auto seq = txInput->seq;
    auto executiveState = m_executives.find({contextID, seq});
    if (executiveState == m_executives.end())
    {
        // add to top if not exists
        auto newExecutiveState =
            std::make_shared<ExecutiveState>(m_executiveFactory, std::move(txInput));
        m_runPool.push(newExecutiveState);
        m_executives[{contextID, seq}] = newExecutiveState;
    }
    else
    {
        // update resume params
        executiveState->second->setResumeParam(std::move(txInput));
    }
}

void ExecutiveQueueFlow::asyncRun(std::function<void(CallParameters::UniquePtr)> onTxFinished,
    std::function<void(std::shared_ptr<std::vector<CallParameters::UniquePtr>>)> onPaused,
    std::function<void(bcos::Error::UniquePtr)> onFinished)
{
    run(onTxFinished, onPaused, onFinished);
}

void ExecutiveQueueFlow::run(std::function<void(CallParameters::UniquePtr)> onTxFinished,
    std::function<void(std::shared_ptr<std::vector<CallParameters::UniquePtr>>)> onPaused,
    std::function<void(bcos::Error::UniquePtr)> onFinished)
{
    try
    {
        while (!m_runPool.empty())
        {
            auto executiveState = m_runPool.top();
            auto output = executiveState->go();

            switch (executiveState->getStatus())
            {
            case ExecutiveState::NEED_RUN:
            {
                onFinished(nullptr);
            }
            case ExecutiveState::PAUSED:
            {  // just ignore, need to set resume params
                break;
            }
            case ExecutiveState::NEED_RESUME:
            {
                auto outputs = std::make_shared<std::vector<CallParameters::UniquePtr>>();
                outputs->push_back(std::move(output));
                onPaused(outputs);
                break;
            }
            case ExecutiveState::FINISHED:
            {
                onTxFinished(std::move(output));
                break;
            }
            }
        }

        onFinished(nullptr);
    }
    catch (std::exception& e)
    {
        EXECUTIVE_LOG(ERROR) << "ExecutiveQueueFlow run error: "
                             << boost::diagnostic_information(e);
        onFinished(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "ExecutiveQueueFlow run error", e));
    }
}