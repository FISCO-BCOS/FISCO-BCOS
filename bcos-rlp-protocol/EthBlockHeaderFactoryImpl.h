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
 * @file EthBlockHeaderFactoryImpl.h
 * @brief Factory for EthBlockHeaderImpl — inherits BlockHeaderFactory
 * @date 2026/6/24
 */
#pragma once

#include <bcos-framework/protocol/BlockHeaderFactory.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>

namespace bcos::protocol
{

class EthBlockHeaderFactoryImpl : public bcos::protocol::BlockHeaderFactory
{
public:
    explicit EthBlockHeaderFactoryImpl(bcos::crypto::CryptoSuite::Ptr cryptoSuite);
    ~EthBlockHeaderFactoryImpl() override = default;

    bcos::protocol::BlockHeader::Ptr createBlockHeader() override;
    bcos::protocol::BlockHeader::Ptr createBlockHeader(bcos::bytes const& _data) override;
    bcos::protocol::BlockHeader::Ptr createBlockHeader(bcos::bytesConstRef _data) override;
    bcos::protocol::BlockHeader::Ptr createBlockHeader(bcos::protocol::BlockNumber _number) override;

private:
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    bcos::crypto::Hash::Ptr m_hashImpl;
};

}  // namespace bcos::protocol
