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
 * @brief implementation for BlockHeaderFactory
 * @file BlockHeaderFactoryImpl.h
 * @author: ancelmo
 * @date 2021-04-20
 */
#pragma once
#include "BlockHeaderImpl.h"
#include <bcos-framework/interfaces/protocol/BlockHeaderFactory.h>


namespace bcostars
{
namespace protocol
{
class BlockHeaderFactoryImpl : public bcos::protocol::BlockHeaderFactory
{
public:
    BlockHeaderFactoryImpl(bcos::crypto::CryptoSuite::Ptr cryptoSuite) : m_cryptoSuite(cryptoSuite)
    {}
    ~BlockHeaderFactoryImpl() override {}
    bcos::protocol::BlockHeader::Ptr createBlockHeader() override
    {
        return std::make_shared<bcostars::protocol::BlockHeaderImpl>(
            m_cryptoSuite, [m_header = bcostars::BlockHeader()]() mutable { return &m_header; });
    }
    bcos::protocol::BlockHeader::Ptr createBlockHeader(bcos::bytes const& _data) override
    {
        return createBlockHeader(bcos::ref(_data));
    }
    bcos::protocol::BlockHeader::Ptr createBlockHeader(bcos::bytesConstRef _data) override
    {
        auto blockHeader = createBlockHeader();
        blockHeader->decode(_data);

        return blockHeader;
    }
    bcos::protocol::BlockHeader::Ptr createBlockHeader(bcos::protocol::BlockNumber _number) override
    {
        auto blockHeader = createBlockHeader();
        blockHeader->setNumber(_number);

        return blockHeader;
    }

private:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};
}  // namespace protocol
}  // namespace bcostars
