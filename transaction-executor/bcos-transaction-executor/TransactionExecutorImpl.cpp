/*
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file TransactionExecutorImpl.cpp
 */

#include "TransactionExecutorImpl.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"

bcos::executor_v1::TransactionExecutorImpl::TransactionExecutorImpl(
    protocol::TransactionReceiptFactory const& receiptFactory, crypto::Hash::Ptr hashImpl,
    PrecompiledManager& precompiledManager)
  : m_receiptFactory(receiptFactory),
    m_hashImpl(std::move(hashImpl)),
    m_precompiledManager(precompiledManager)
{}

evmc_message bcos::executor_v1::newEVMCMessage(protocol::BlockNumber blockNumber,
    protocol::Transaction const& transaction, int64_t gasLimit, const evmc_address& origin)
{
    auto recipientAddress = unhexAddress(transaction.to());
    evmc_message message = {.kind = transaction.to().empty() ? EVMC_CREATE : EVMC_CALL,
        .flags = 0,
        .depth = 0,
        .gas = gasLimit,
        .state_gas = {},
        .recipient = recipientAddress,
        .sender = origin,
        .input_data = transaction.input().data(),
        .input_size = transaction.input().size(),
        .value = toEvmC(transaction.value()),
        .code_address = recipientAddress,
        .code = nullptr,
        .code_size = 0};

    if (blockNumber == 0 && transaction.to() == precompiled::AUTH_COMMITTEE_ADDRESS)
    {
        message.kind = EVMC_CREATE;
    }

    return message;
}
