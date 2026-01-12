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
 * @file TxValidator.h
 * @author: kyonGuo
 * @date 2026/1/12
 */

#pragma once
#include "bcos-protocol/TransactionStatus.h"
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-task/Task.h>

namespace bcos::rpc
{
class TxValidator
{
public:
    static task::Task<protocol::TransactionStatus> checkSenderBalance(
        const protocol::Transaction& _tx, bcos::scheduler::SchedulerInterface::Ptr _scheduler,
        protocol::BlockNumber _blockNumber = 0);
};
}  // namespace bcos::rpc
