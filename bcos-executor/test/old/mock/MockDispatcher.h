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
 * @brief mock the Dispatcher
 * @file MockDispatcher.h
 * @author: xingqiangbai
 * @date 2021-06-09

 */
#pragma once
#include "bcos-framework/interfaces/dispatcher/DispatcherInterface.h"
#include <map>

using namespace bcos;
using namespace bcos::dispatcher;
using namespace bcos::protocol;

namespace bcos
{
namespace test
{
class MockDispatcher : public DispatcherInterface
{
public:
    using Ptr = std::shared_ptr<MockDispatcher>;
    MockDispatcher() = default;
    ~MockDispatcher() override {}

    void asyncExecuteBlock(const Block::Ptr& _block, bool,
        std::function<void(const Error::Ptr&, const BlockHeader::Ptr&)> _callback, ssize_t) override
    {
        if (m_blocks.empty())
        {
            m_blocks.push_back(_block);
            _callback(nullptr, _block->blockHeader());
            return;
        }
        auto latestBlock = m_blocks[m_blocks.size() - 1];
        if (_block->blockHeader()->number() != latestBlock->blockHeader()->number() + 1)
        {
            _callback(std::make_shared<Error>(-1, "invalid block for not consistent"), nullptr);
            return;
        }
        std::vector<ParentInfo> parentList;
        if (_block->blockHeader()->parentInfo().size() == 0)
        {
            parentList.push_back(ParentInfo{
                latestBlock->blockHeader()->number(), latestBlock->blockHeader()->hash()});
            _block->blockHeader()->setParentInfo(parentList);
        }
        m_blocks.push_back(_block);
        _callback(nullptr, _block->blockHeader());
    }

    void asyncGetLatestBlock(
        std::function<void(const Error::Ptr&, const Block::Ptr&)> _callback) override
    {
        if (m_blocks.empty())
        {
            _callback(std::make_shared<Error>(-1, "no new block"), nullptr);
        }
        _callback(nullptr, m_blocks.front());
    }

    void asyncNotifyExecutionResult(const Error::Ptr&, bcos::crypto::HashType const&,
        const std::shared_ptr<BlockHeader>& _header,
        std::function<void(const Error::Ptr&)>) override
    {
        // the process it in order
        m_blockHeaders[_header->number()] = _header;
        m_blocks.erase(m_blocks.begin());
    }

    void stop() override {}
    void start() override {}

private:
    std::vector<Block::Ptr> m_blocks;
    std::map<BlockNumber, BlockHeader::Ptr> m_blockHeaders;
};
}  // namespace test
}  // namespace bcos