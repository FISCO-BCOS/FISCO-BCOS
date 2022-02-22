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
 * @brief state machine to execute the transactions
 * @file StateMachine.cpp
 * @author: yujiechen
 * @date 2021-05-18
 */
#include "StateMachine.h"
#include "Common.h"

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::protocol;
using namespace bcos::crypto;

void StateMachine::asyncApply(ssize_t _timeout, ProposalInterface::ConstPtr _lastAppliedProposal,
    ProposalInterface::Ptr _proposal, ProposalInterface::Ptr _executedProposal,
    std::function<void(bool)> _onExecuteFinished)
{
    // Note: async here to increase performance
    m_worker->enqueue(
        [this, _timeout, _lastAppliedProposal, _proposal, _executedProposal, _onExecuteFinished]() {
            this->apply(
                _timeout, _lastAppliedProposal, _proposal, _executedProposal, _onExecuteFinished);
        });
}

void StateMachine::apply(ssize_t, ProposalInterface::ConstPtr _lastAppliedProposal,
    ProposalInterface::Ptr _proposal, ProposalInterface::Ptr _executedProposal,
    std::function<void(bool)> _onExecuteFinished)
{
    if (_proposal->index() <= _lastAppliedProposal->index())
    {
        CONSENSUS_LOG(WARNING) << LOG_DESC("asyncApply: the proposal has already been applied")
                               << LOG_KV("proposalIndex", _proposal->index())
                               << LOG_KV("lastAppliedProposal", _lastAppliedProposal->index());
        if (_onExecuteFinished)
        {
            _onExecuteFinished(false);
        }
        return;
    }
    auto block = m_blockFactory->createBlock(_proposal->data());
    // invalid block
    auto blockHeader = block->blockHeader();
    if (!blockHeader)
    {
        if (_onExecuteFinished)
        {
            _onExecuteFinished(false);
        }
        return;
    }
    // set the parentHash information
    if (_proposal->index() == _lastAppliedProposal->index() + 1)
    {
        ParentInfoList parentInfoList;
        ParentInfo parentInfo{_lastAppliedProposal->index(), _lastAppliedProposal->hash()};
        parentInfoList.push_back(parentInfo);
        blockHeader->setParentInfo(std::move(parentInfoList));
        CONSENSUS_LOG(DEBUG) << LOG_DESC("setParentInfo for the proposal")
                             << LOG_KV("proposalIndex", _proposal->index())
                             << LOG_KV("lastAppliedProposal", _lastAppliedProposal->index())
                             << LOG_KV("parentHash", _lastAppliedProposal->hash().abridged());
    }
    else
    {
        CONSENSUS_LOG(FATAL) << LOG_DESC("invalid lastAppliedProposal")
                             << LOG_KV("lastAppliedIndex", _lastAppliedProposal->index())
                             << LOG_KV("proposal", _proposal->index());
    }
    // calls dispatcher to execute the block
    auto startT = utcTime();
    m_scheduler->executeBlock(block, false,
        [startT, block, _onExecuteFinished, _proposal, _executedProposal](
            Error::Ptr&& _error, BlockHeader::Ptr&& _blockHeader) {
            if (!_onExecuteFinished)
            {
                return;
            }
            auto blockHeader = block->blockHeader();
            if (_error != nullptr)
            {
                CONSENSUS_LOG(WARNING) << LOG_DESC("asyncExecuteBlock failed")
                                       << LOG_KV("number", blockHeader->number())
                                       << LOG_KV("errorCode", _error->errorCode())
                                       << LOG_KV("errorInfo", _error->errorMessage());
                _onExecuteFinished(false);
                return;
            }
            auto execT = (double)(utcTime() - startT) / (double)(block->transactionsHashSize());
            CONSENSUS_LOG(INFO) << LOG_DESC("asyncExecuteBlock success")
                                << LOG_KV("number", _blockHeader->number())
                                << LOG_KV("result", _blockHeader->hash().abridged())
                                << LOG_KV("txsSize", block->transactionsHashSize())
                                << LOG_KV("txsRoot", _blockHeader->txsRoot().abridged())
                                << LOG_KV("receiptsRoot", _blockHeader->receiptsRoot().abridged())
                                << LOG_KV("stateRoot", _blockHeader->stateRoot().abridged())
                                << LOG_KV("txs", block->transactionsHashSize())
                                << LOG_KV("timeCost", (utcTime() - startT))
                                << LOG_KV("execPerTx", execT);
            if (_blockHeader->number() != blockHeader->number())
            {
                CONSENSUS_LOG(WARNING) << LOG_DESC("asyncExecuteBlock exception")
                                       << LOG_KV("expectedNumber", blockHeader->number())
                                       << LOG_KV("number", _blockHeader->number())
                                       << LOG_KV("timeCost", (utcTime() - startT));
                return;
            }
            _executedProposal->setIndex(_blockHeader->number());
            _executedProposal->setHash(_blockHeader->hash());

            bcos::bytes blockHeaderBuffer;
            _blockHeader->encode(blockHeaderBuffer);
            _executedProposal->setData(std::move(blockHeaderBuffer));
            // the transactions hash list
            _executedProposal->setExtraData(_proposal->data());
            // The _onExecuteFinished callback itself does the asynchronous logic, so there is no
            // need to use m_worker to re-synchronize it here.
            _onExecuteFinished(true);
        });
    return;
}