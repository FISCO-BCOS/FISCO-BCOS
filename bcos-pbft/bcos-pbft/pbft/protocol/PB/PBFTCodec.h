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
 * @brief implementation for PBFTCodec
 * @file PBFTCodec.h
 * @author: yujiechen
 * @date 2021-04-13
 */
#pragma once
#include "../../interfaces/PBFTCodecInterface.h"
#include "../../interfaces/PBFTMessageFactory.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
namespace bcos::consensus
{
class PBFTCodec : public PBFTCodecInterface
{
public:
    using Ptr = std::shared_ptr<PBFTCodec>;
    PBFTCodec(bcos::crypto::KeyPairInterface::Ptr _keyPair,
        bcos::crypto::CryptoSuite::Ptr _cryptoSuite, PBFTMessageFactory::Ptr _pbftMessageFactory)
      : m_keyPair(std::move(_keyPair)),
        m_cryptoSuite(std::move(_cryptoSuite)),
        m_pbftMessageFactory(std::move(_pbftMessageFactory))
    {}
    PBFTCodec(PBFTCodec const&) = delete;
    PBFTCodec& operator=(PBFTCodec const&) = delete;
    PBFTCodec(PBFTCodec&&) = delete;
    PBFTCodec& operator=(PBFTCodec&&) = delete;

    ~PBFTCodec() override = default;

    bytesPointer encode(
        PBFTBaseMessageInterface::Ptr _pbftMessage, int32_t _version = 0) const override;

    PBFTBaseMessageInterface::Ptr decode(bytesConstRef _data) const override;

protected:
    virtual bool shouldHandleSignature(PacketType _packetType) const
    {
        return (_packetType == PacketType::ViewChangePacket ||
                _packetType == PacketType::NewViewPacket);
    }

private:
    bcos::crypto::KeyPairInterface::Ptr m_keyPair;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;

    PBFTMessageFactory::Ptr m_pbftMessageFactory;
};
}  // namespace bcos::consensus