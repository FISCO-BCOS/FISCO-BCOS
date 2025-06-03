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
 * @brief implementation for txpool check
 * @file TransactionValidator.h
 * @author: asherli
 * @date 2024-12-11
 */
#pragma once

#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/storage/LegacyStorageMethods.h"
#include "bcos-protocol/TransactionStatus.h"
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/protocol/Transaction.h>
#include <evmc/evmc.h>
#include <memory>

namespace bcos::txpool
{
class TransactionValidator
{
public:
    using Ptr = std::shared_ptr<TransactionValidator>;
    // check simple replay attack protection
    // TODO: get from ledager config
    static bcos::protocol::TransactionStatus ValidateTransaction(
        bcos::protocol::Transaction::ConstPtr _tx);
    static task::Task<protocol::TransactionStatus> ValidateTransactionWithState(
        bcos::protocol::Transaction::ConstPtr _tx,
        std::shared_ptr<bcos::ledger::LedgerInterface> _ledger);
};
}  // namespace bcos::txpool