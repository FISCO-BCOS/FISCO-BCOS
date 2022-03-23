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

#include "ExecutiveQueueFlow.h"


using namespace bcos;
using namespace bcos::executor;

void ExecutiveQueueFlow::addTop(CallParameters::UniquePtr input)
{
    auto contextID = input->contextID;
    auto seq = input->seq;
    auto executiveState = std::make_shared<ExecutiveState>(m_executiveFactory, std::move(input));
    m_runPool.push(executiveState);
    m_executives[{contextID, seq}] = executiveState;
}


void ExecutiveQueueFlow::setResumeParam(CallParameters::UniquePtr pullParam)
{
    auto executiveState = m_executives.find({pullParam->contextID, pullParam->seq});
    if (executiveState == m_executives.end())
    {
        EXECUTIVE_LOG(ERROR) << LOG_BADGE("ExecutiveQueueFlow")
                             << "setResumeParams: executive not found"
                             << LOG_KV("codeAddress", pullParam->codeAddress)
                             << LOG_KV("contextID", pullParam->contextID)
                             << LOG_KV("seq", pullParam->seq);
        return;
    }

    executiveState->second->setResumeParam(std::move(pullParam));
}

void ExecutiveQueueFlow::asyncRun(std::function<void(CallParameters::UniquePtr)> onTxFinished,
    std::function<void(std::shared_ptr<std::vector<CallParameters::UniquePtr>>)> onPaused,
    std::function<void(bool, std::string)> onFinished)
{
    run(onTxFinished, onPaused, onFinished);
}

void ExecutiveQueueFlow::run(std::function<void(CallParameters::UniquePtr)> onTxFinished,
    std::function<void(std::shared_ptr<std::vector<CallParameters::UniquePtr>>)> onPaused,
    std::function<void(bool, std::string)> onFinished)
{
    while (!m_runPool.empty())
    {
        auto executiveState = m_runPool.top();
        auto output = executiveState->go();

        switch (executiveState->getStatus())
        {
        case ExecutiveState::NEED_RUN:
        {
            onFinished(false, "Illegal executive state: NEED_RUN ");
        }
        case ExecutiveState::PAUSED:
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

    onFinished(true, "success!");
}