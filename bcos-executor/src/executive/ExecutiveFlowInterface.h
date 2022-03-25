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
 * @brief interface definition of TransactionFlow
 * @file ExecutiveFlowInterface.h
 * @author: jimmyshi
 * @date: 2022-03-22
 */

#pragma once

#include "TransactionExecutive.h"

namespace bcos
{
namespace executor
{

class ExecutiveFlowInterface
{
public:
    using Ptr = std::shared_ptr<ExecutiveFlowInterface>;

    virtual void submit(CallParameters::UniquePtr txInput) = 0;  // add new or set resume params

    virtual void asyncRun(
        // onTxFinished(output)
        std::function<void(CallParameters::UniquePtr)> onTxFinished,

        // onPaused(pushMessages)
        std::function<void(std::shared_ptr<std::vector<CallParameters::UniquePtr>>)> onPaused,

        // onFinished(success, errorMessage)
        std::function<void(bcos::Error::UniquePtr)> onFinished) = 0;
};

}  // namespace executor
}  // namespace bcos
