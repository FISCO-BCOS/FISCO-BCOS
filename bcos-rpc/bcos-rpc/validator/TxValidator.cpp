/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file TxValidator.cpp
 * @author: kyonGuo
 * @date 2026/1/12
 */

#include "TxValidator.h"

#include "bcos-ledger/LedgerMethods.h"
#include <bcos-framework/ledger/Ledger.h>

namespace bcos::rpc
{
task::Task<protocol::TransactionStatus> TxValidator::checkSenderBalance(
    const protocol::Transaction& _tx, bcos::scheduler::SchedulerInterface::Ptr _scheduler,
    protocol::BlockNumber currentBlockNumber)
{
    auto sender = toHex(_tx.sender());

    u256 balanceValue{};

    // Try to get pending balance from scheduler first
    try
    {
        if (const auto balanceEntry = co_await _scheduler->getPendingStorageAt(
                sender, ledger::ACCOUNT_TABLE_FIELDS::BALANCE, currentBlockNumber))
        {
            if (const auto balanceStr = balanceEntry->get(); !balanceStr.empty())
            {
                balanceValue = boost::lexical_cast<u256>(balanceStr);
                BCOS_LOG(TRACE) << LOG_BADGE("ValidateTransactionWithState")
                                << LOG_DESC("Get balance from scheduler pending storage")
                                << LOG_KV("sender", sender) << LOG_KV("balance", balanceValue);
            }
        }
    }
    catch (std::exception const& e)
    {
        BCOS_LOG(WARNING) << LOG_BADGE("ValidateTransactionWithState")
                          << LOG_DESC("Failed to get balance from scheduler, fallback to ledger")
                          << LOG_KV("error", boost::diagnostic_information(e));
    }
    if (auto txValue = u256(_tx.value()); balanceValue < txValue || balanceValue == 0)
    {
        BCOS_LOG(TRACE) << LOG_BADGE("ValidateTransactionWithState")
                        << LOG_DESC("InsufficientFunds") << LOG_KV("sender", sender)
                        << LOG_KV("balance", balanceValue) << LOG_KV("txValue", txValue);
        co_return protocol::TransactionStatus::InsufficientFunds;
    }

    co_return protocol::TransactionStatus::None;
}
}  // namespace bcos::rpc