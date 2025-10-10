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
 * @brief faker for the Scheduler
 * @file FakeScheduler.h
 * @author: yujiechen
 * @date 2021-09-07
 */
#pragma once
#include "../../dispatcher/SchedulerInterface.h"
#include "FakeLedger.h"

using namespace bcos;
using namespace bcos::scheduler;
namespace bcos
{
namespace test
{
class FakeScheduler : public SchedulerInterface
{
public:
    using Ptr = std::shared_ptr<FakeScheduler>;
    FakeScheduler(FakeLedger::Ptr _ledger, BlockFactory::Ptr _blockFactory)
      : m_ledger(_ledger), m_blockFactory(_blockFactory)
    {}
    ~FakeScheduler() override = default;
    void executeBlock(bcos::protocol::Block::Ptr _block, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)>
            _callback) noexcept override
    {
        auto blockHeader = _block->blockHeader();
        if (m_blockFactory)
        {
            auto oldBlockHeader = _block->blockHeader();
            blockHeader = m_blockFactory->blockHeaderFactory()->populateBlockHeader(oldBlockHeader);
            blockHeader->calculateHash(*m_blockFactory->cryptoSuite()->hashImpl());
        }
        _callback(nullptr, std::move(blockHeader), false);
        return;
    }

    // Consensus and block-sync module use this interface to commit block
    void commitBlock(bcos::protocol::BlockHeader::Ptr _blockHeader,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)>
            _onCommitBlock) noexcept override
    {
        m_ledger->asyncCommitBlock(_blockHeader, _onCommitBlock);
    }

    // by console, query committed committing executing
    void status(
        std::function<void(Error::Ptr, bcos::protocol::Session::ConstPtr)>) noexcept override
    {}

    // by rpc
    void call(protocol::Transaction::Ptr,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)>) noexcept override
    {}

    // clear all status
    void reset(std::function<void(Error::Ptr)>) noexcept override {}
    void getCode(std::string_view, std::function<void(Error::Ptr, bcos::bytes)>) override {}
    void getABI(std::string_view, std::function<void(Error::Ptr, std::string)>) override {}

    // for performance, do the things before executing block in executor.
    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(Error::Ptr)>) override {};

private:
    FakeLedger::Ptr m_ledger;
    BlockFactory::Ptr m_blockFactory;
};
}  // namespace test
}  // namespace bcos