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
 */
/**
 * @brief mock ledger
 * @file MockLedger.h
 * @author: xingqiangbai
 * @date: 2021-06-09
 */

#pragma once

#include "bcos-framework/testutils/faker/FakeLedger.h"
#include <mutex>
#include <string>

namespace bcos
{
namespace test
{
class MockLedger : public FakeLedger
{
public:
    typedef std::shared_ptr<MockLedger> Ptr;
    MockLedger(protocol::BlockHeader::Ptr _header, protocol::BlockFactory::Ptr _blockFactory)
      : m_number(_header->number()), m_blockHeader(_header), blockFactory(_blockFactory)
    {}
    virtual ~MockLedger() = default;

    // maybe sync module or rpc module need this interface to return header/txs/receipts

    void asyncGetBlockDataByNumber(BlockNumber _number, int32_t,
        std::function<void(Error::Ptr, Block::Ptr)> _callback) override
    {
        if (_number < 1000)
        {
            auto block = blockFactory->createBlock();
            auto header = blockFactory->blockHeaderFactory()->createBlockHeader(_number);
            block->setBlockHeader(header);
            _callback(nullptr, block);
        }
        else
        {
            _callback(std::make_shared<Error>(-1, "failed"), nullptr);
        }
    }

    void asyncGetBlockNumber(std::function<void(Error::Ptr, BlockNumber)> _onGetBlock) override
    {
        _onGetBlock(nullptr, m_number);
    }

protected:
    protocol::BlockNumber m_number;
    BlockHeader::Ptr m_blockHeader;
    protocol::BlockFactory::Ptr blockFactory;
};

}  // namespace test

}  // namespace bcos
