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
 * @brief server for Scheduler
 * @file SchedulerServiceServer.cpp
 * @author: yujiechen
 * @date 2021-10-18
 */
#include "SchedulerServiceServer.h"
#include <bcos-tars-protocol/ErrorConverter.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptImpl.h>
using namespace bcostars;

bcostars::Error SchedulerServiceServer::call(
    const bcostars::Transaction& _tx, bcostars::TransactionReceipt&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto bcosTransaction = std::make_shared<bcostars::protocol::TransactionImpl>(
        m_cryptoSuite, [m_tx = _tx]() mutable { return &m_tx; });
    m_scheduler->call(bcosTransaction,
        [current](bcos::Error::Ptr&& _error, bcos::protocol::TransactionReceipt::Ptr&& _receipt) {
            bcostars::TransactionReceipt tarsReceipt;
            if (_receipt)
            {
                tarsReceipt =
                    std::dynamic_pointer_cast<bcostars::protocol::TransactionReceiptImpl>(_receipt)
                        ->inner();
            }
            async_response_call(current, toTarsError(_error), tarsReceipt);
        });
    return bcostars::Error();
}

bcostars::Error SchedulerServiceServer::getCode(
    const std::string& contract, vector<tars::Char>& code, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_scheduler->getCode(contract, [current](bcos::Error::Ptr error, bcos::bytes code) {
        vector<tars::Char> outCode(code.begin(), code.end());

        async_response_getCode(current, toTarsError(error), outCode);
    });

    return bcostars::Error();
}

bcostars::Error SchedulerServiceServer::getABI(const std::string& contract, std::string& abi, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_scheduler->getABI(contract, [current](bcos::Error::Ptr error, std::string abi) {
        async_response_getABI(current, toTarsError(error), abi);
    });

    return bcostars::Error();
}
