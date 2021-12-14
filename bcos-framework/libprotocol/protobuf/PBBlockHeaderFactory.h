/*
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
 * @brief factory for PBBlockHeader
 * @file PBBlockHeaderFactory.h
 * @author: yujiechen
 * @date: 2021-03-23
 */
#pragma once
#include "PBBlockHeader.h"
#include "interfaces/crypto/CryptoSuite.h"
#include "interfaces/protocol/BlockHeaderFactory.h"

namespace bcos
{
namespace protocol
{
class PBBlockHeaderFactory : public BlockHeaderFactory
{
public:
    using Ptr = std::shared_ptr<PBBlockHeaderFactory>;
    explicit PBBlockHeaderFactory(bcos::crypto::CryptoSuite::Ptr _cryptoSuite)
      : m_cryptoSuite(_cryptoSuite)
    {}
    ~PBBlockHeaderFactory() override {}

    BlockHeader::Ptr createBlockHeader() override
    {
        return std::make_shared<PBBlockHeader>(m_cryptoSuite);
    }

    BlockHeader::Ptr createBlockHeader(bytesConstRef _data) override
    {
        return std::make_shared<PBBlockHeader>(m_cryptoSuite, _data);
    }

    BlockHeader::Ptr createBlockHeader(bytes const& _data) override
    {
        return std::make_shared<PBBlockHeader>(m_cryptoSuite, _data);
    }

    BlockHeader::Ptr createBlockHeader(BlockNumber _number) override
    {
        auto header = std::make_shared<PBBlockHeader>(m_cryptoSuite);
        header->setNumber(_number);
        return header;
    }

private:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
};
}  // namespace protocol
}  // namespace bcos