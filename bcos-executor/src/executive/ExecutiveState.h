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
 * @brief the state of executive for TransactionFlow run
 * @file ExecutiveState.h
 * @author: jimmyshi
 * @date: 2022-03-23
 */

#pragma once

#include "../CallParameters.h"
#include "ExecutiveFactory.h"

namespace bcos
{
namespace executor
{

class ExecutiveState
{
public:
    using Ptr = std::shared_ptr<ExecutiveState>;

    ExecutiveState(ExecutiveFactory::Ptr executiveFactory, CallParameters::UniquePtr input)
      : m_isStaticCall(input->staticCall),
        m_contextID(input->contextID),
        m_seq(input->seq),
        m_input(std::move(input)),
        m_executiveFactory(executiveFactory){};

    enum Status
    {
        NEED_RUN = 0,
        PAUSED = 1,
        NEED_RESUME = 2,
        FINISHED = 3,
    };

    Status getStatus() { return m_status; }
    CallParameters::UniquePtr go();
    void setResumeParam(CallParameters::UniquePtr pullParam);
    int64_t getContextID() { return m_contextID; }
    int64_t getSeq() { return m_seq; }

    void appendKeyLocks(std::vector<std::string> keyLocks);

private:
    bool m_isStaticCall;
    int64_t m_contextID;
    int64_t m_seq;
    CallParameters::UniquePtr m_input;
    std::shared_ptr<TransactionExecutive> m_executive;
    Status m_status = NEED_RUN;
    ExecutiveFactory::Ptr m_executiveFactory;
};

}  // namespace executor
}  // namespace bcos
