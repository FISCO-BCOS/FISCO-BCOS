/**
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
 * @brief Base implementation for ConsensusEngine
 * @file ConsensusEngine.cpp
 * @author: yujiechen
 * @date 2021-04-22
 */
#pragma once
#include "../framework/ConsensusEngineInterface.h"
#include "Common.h"
#include <bcos-utilities/Worker.h>

namespace bcos
{
namespace consensus
{
class ConsensusEngine : public virtual ConsensusEngineInterface, public Worker
{
public:
    ConsensusEngine(std::string _name, unsigned _idleWaitMs) : Worker(_name, _idleWaitMs) {}

    ~ConsensusEngine() override { stop(); }
    void start() override
    {
        if (m_started)
        {
            CONSENSUS_LOG(WARNING) << LOG_DESC("The consensusEngine has already been started");
            return;
        }
        CONSENSUS_LOG(INFO) << LOG_DESC("Start the consensusEngine");
        // start  a thread to execute task
        startWorking();
        m_started = true;
    }

    void stop() override
    {
        if (m_started == false)
        {
            return;
        }
        CONSENSUS_LOG(INFO) << LOG_DESC("Stop consensusEngine");
        m_started = false;
        finishWorker();
        if (isWorking())
        {
            // stop the worker thread
            stopWorking();
            terminate();
        }
        CONSENSUS_LOG(INFO) << LOG_DESC("ConsensusEngine stopped");
    }

    void workerProcessLoop() override
    {
        while (isWorking())
        {
            try
            {
                executeWorker();
            }
            catch (std::exception const& _e)
            {
                CONSENSUS_LOG(ERROR) << LOG_DESC("Process consensus task exception")
                                     << LOG_KV("error", boost::diagnostic_information(_e));
            }
        }
    }

protected:
    std::atomic_bool m_started = {false};
};
}  // namespace consensus
}  // namespace bcos