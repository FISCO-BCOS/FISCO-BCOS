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

#include "BlockExecutiveFactory.h"
#include "BlockExecutive.h"
#include "SerialBlockExecutive.h"
#include "SerialDmcBlockExecutive.h"


using namespace std;
using namespace bcos::protocol;
using namespace bcos::scheduler;


std::shared_ptr<BlockExecutive> BlockExecutiveFactory::build(bcos::protocol::Block::Ptr block,
    SchedulerImpl* scheduler, size_t startContextID,
    bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
    bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory,
    bcos::txpool::TxPoolInterface::Ptr _txPool)
{
    if (m_isSerialExecute)
    {
        auto serialBlockExecutive = std::make_shared<SerialBlockExecutive>(block, scheduler,
            startContextID, transactionSubmitResultFactory, staticCall, _blockFactory, _txPool);
        return serialBlockExecutive;
    }
    else
    {
        auto blockExecutive = std::make_shared<BlockExecutive>(block, scheduler, startContextID,
            transactionSubmitResultFactory, staticCall, _blockFactory, _txPool);
        return blockExecutive;
    }
}

std::shared_ptr<BlockExecutive> BlockExecutiveFactory::build(bcos::protocol::Block::Ptr block,
    SchedulerImpl* scheduler, size_t startContextID,
    bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
    bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory,
    bcos::txpool::TxPoolInterface::Ptr _txPool, uint64_t _gasLimit, bool _syncBlock)
{
    if (m_isSerialExecute)
    {
        auto serialBlockExecutive = std::make_shared<SerialBlockExecutive>(block, scheduler,
            startContextID, transactionSubmitResultFactory, staticCall, _blockFactory, _txPool,
            _gasLimit, _syncBlock);
        return serialBlockExecutive;
    }
    else
    {
        auto blockExecutive = std::make_shared<BlockExecutive>(block, scheduler, startContextID,
            transactionSubmitResultFactory, staticCall, _blockFactory, _txPool, _gasLimit,
            _syncBlock);
        return blockExecutive;
    }
}