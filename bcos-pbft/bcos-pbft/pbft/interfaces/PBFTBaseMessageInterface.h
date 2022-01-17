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
 * @brief interface for PBFTBaseMessage
 * @file PBFTBaseMessageInterface.h
 * @author: yujiechen
 * @date 2021-04-13
 */
#pragma once
#include "../utilities/Common.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <bcos-framework/interfaces/consensus/ConsensusTypeDef.h>
#include <stdint.h>
#include <memory>
namespace bcos
{
namespace consensus
{
class PBFTBaseMessageInterface
{
public:
    using Ptr = std::shared_ptr<PBFTBaseMessageInterface>;
    PBFTBaseMessageInterface() = default;
    virtual ~PBFTBaseMessageInterface() {}

    virtual int64_t timestamp() const = 0;
    virtual int32_t version() const = 0;
    virtual ViewType view() const = 0;
    virtual IndexType generatedFrom() const = 0;
    virtual int64_t index() const = 0;
    virtual void setIndex(int64_t _proposalStartIndex) = 0;

    virtual bcos::crypto::HashType const& hash() const = 0;
    virtual PacketType packetType() const = 0;

    virtual void setTimestamp(int64_t _timestamp) = 0;
    virtual void setVersion(int32_t _version) = 0;
    virtual void setView(ViewType _view) = 0;
    virtual void setGeneratedFrom(IndexType _generatedFrom) = 0;
    virtual void setHash(bcos::crypto::HashType const& _hash) = 0;
    virtual void setPacketType(PacketType _packetType) = 0;

    virtual bytesPointer encode(bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        bcos::crypto::KeyPairInterface::Ptr _keyPair) const = 0;
    virtual void decode(bytesConstRef _data) = 0;

    virtual bytesConstRef signatureData() = 0;
    virtual bcos::crypto::HashType const& signatureDataHash() = 0;

    virtual void setSignatureData(bytes&& _signatureData) = 0;

    virtual void setSignatureData(bytes const& _signatureData) = 0;
    virtual void setSignatureDataHash(bcos::crypto::HashType const& _hash) = 0;
    virtual bool verifySignature(
        bcos::crypto::CryptoSuite::Ptr _cryptoSuite, bcos::crypto::PublicPtr _pubKey) = 0;

    virtual void setFrom(bcos::crypto::PublicPtr _from) = 0;
    virtual bcos::crypto::PublicPtr from() const = 0;
};
inline std::string printPBFTMsgInfo(PBFTBaseMessageInterface::Ptr _pbftMsg)
{
    std::ostringstream stringstream;
    stringstream << LOG_KV("reqHash", _pbftMsg->hash().abridged())
                 << LOG_KV("reqIndex", _pbftMsg->index()) << LOG_KV("reqV", _pbftMsg->view())
                 << LOG_KV("fromIdx", _pbftMsg->generatedFrom());
    return stringstream.str();
}
}  // namespace consensus
}  // namespace bcos