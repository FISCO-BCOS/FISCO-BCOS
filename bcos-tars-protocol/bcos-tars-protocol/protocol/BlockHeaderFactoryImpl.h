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
#include "../impl/TarsHashable.h"
#include "BlockHeaderImpl.h"
#include <bcos-concepts/Hash.h>
#include <bcos-framework/protocol/BlockHeaderFactory.h>
#include <utility>


namespace bcostars::protocol
{
class BlockHeaderFactoryImpl : public bcos::protocol::BlockHeaderFactory
{
public:
    BlockHeaderFactoryImpl(bcos::crypto::CryptoSuite::Ptr cryptoSuite)
      : m_cryptoSuite(std::move(cryptoSuite)), m_hashImpl(m_cryptoSuite->hashImpl())
    {}
    ~BlockHeaderFactoryImpl() override = default;
    bcos::protocol::BlockHeader::Ptr createBlockHeader() override
    {
        return std::make_shared<bcostars::protocol::BlockHeaderImpl>(
            [m_header = bcostars::BlockHeader()]() mutable { return &m_header; });
    }
    bcos::protocol::BlockHeader::Ptr createBlockHeader(bcos::bytes const& _data) override
    {
        return createBlockHeader(bcos::ref(_data));
    }
    bcos::protocol::BlockHeader::Ptr createBlockHeader(bcos::bytesConstRef _data) override
    {
        auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>(
            [m_header = bcostars::BlockHeader()]() mutable { return &m_header; });
        blockHeader->decode(_data);

        auto& inner = blockHeader->mutableInner();
        if (inner.dataHash.empty())
        {
            // Update the hash field
            bcos::concepts::hash::calculate(inner, m_hashImpl->hasher(), inner.dataHash);

            BCOS_LOG(TRACE) << LOG_BADGE("createBlockHeader")
                            << LOG_DESC("recalculate blockHeader dataHash");
        }

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
    bcos::crypto::Hash::Ptr m_hashImpl;
};
}  // namespace bcostars::protocol
