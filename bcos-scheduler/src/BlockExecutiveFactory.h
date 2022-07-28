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
 * @brief The block executive(context) for serial transaction execution
 * @file SerialBlockExecutive.h
 * @author: wenlinli
 * @date: 2022-07-13
 */

#pragma once
#include "BlockExecutive.h"
#include "Common.h"
#include "SerialBlockExecutive.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/txpool/TxPoolInterface.h>

namespace bcos::scheduler
{
class BlockExecutiveFactory
{
public:
    using Ptr = std::shared_ptr<BlockExecutiveFactory>;
    BlockExecutiveFactory(bool isSerialExecute) : m_isSerialExecute(isSerialExecute) {}
    ~BlockExecutiveFactory() {}

    virtual std::shared_ptr<BlockExecutive> build(bcos::protocol::Block::Ptr block,
        SchedulerImpl* scheduler, size_t startContextID,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::txpool::TxPoolInterface::Ptr _txPool);

    virtual std::shared_ptr<BlockExecutive> build(bcos::protocol::Block::Ptr block,
        SchedulerImpl* scheduler, size_t startContextID,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::txpool::TxPoolInterface::Ptr _txPool, uint64_t _gasLimit, bool _syncBlock);

private:
    bool m_isSerialExecute;
};
}  // namespace bcos::scheduler
