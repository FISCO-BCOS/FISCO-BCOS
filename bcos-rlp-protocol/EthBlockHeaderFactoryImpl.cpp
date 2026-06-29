/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file EthBlockHeaderFactoryImpl.cpp
 * @brief Factory implementation for EthBlockHeaderImpl
 * @date 2026/6/24
 */
#include "EthBlockHeaderFactoryImpl.h"
#include "EthBlockHeaderImpl.h"

namespace bcos::protocol
{

EthBlockHeaderFactoryImpl::EthBlockHeaderFactoryImpl(
    bcos::crypto::CryptoSuite::Ptr cryptoSuite)
  : m_cryptoSuite(std::move(cryptoSuite)), m_hashImpl(m_cryptoSuite->hashImpl())
{}

BlockHeader::Ptr EthBlockHeaderFactoryImpl::createBlockHeader()
{
    return std::make_shared<EthBlockHeaderImpl>();
}

BlockHeader::Ptr EthBlockHeaderFactoryImpl::createBlockHeader(bytes const& _data)
{
    return createBlockHeader(bcos::ref(_data));
}

BlockHeader::Ptr EthBlockHeaderFactoryImpl::createBlockHeader(bcos::bytesConstRef _data)
{
    auto header = std::make_shared<EthBlockHeaderImpl>();
    header->decode(_data);
    if (header->dataPtr()->dataHash.empty())
    {
        header->calculateHash(*m_hashImpl);
    }
    return header;
}

BlockHeader::Ptr EthBlockHeaderFactoryImpl::createBlockHeader(BlockNumber _number)
{
    auto header = createBlockHeader();
    header->setNumber(_number);
    return header;
}

}  // namespace bcos::protocol
