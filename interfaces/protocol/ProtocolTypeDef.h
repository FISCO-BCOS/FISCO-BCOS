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
 * @brief typedef for protocol module
 * @file ProtocolTypeDef.h
 * @author: yujiechen
 * @date: 2021-04-9
 */
#pragma once
#include "Protocol.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
namespace bcos
{
namespace protocol
{
using BlockNumber = int64_t;
using NonceType = u256;
using NonceList = std::vector<NonceType>;
using NonceListPtr = std::shared_ptr<NonceList>;
using BytesList = std::vector<std::shared_ptr<bytes>>;
using BytesListPtr = std::shared_ptr<BytesList>;

struct ParentInfo
{
    BlockNumber blockNumber;
    bcos::crypto::HashType blockHash;

    bool operator==(const ParentInfo& rhs) const
    {
        return this->blockNumber == rhs.blockNumber && this->blockHash == rhs.blockHash;
    }

    template <class Stream, typename = std::enable_if_t<Stream::is_decoder_stream>>
    friend Stream& operator>>(Stream& _stream, ParentInfo& parentInfo)
    {
        return _stream >> parentInfo.blockNumber >> parentInfo.blockHash;
    }

    template <class Stream, typename = std::enable_if_t<Stream::is_encoder_stream>>
    friend Stream& operator<<(Stream& _stream, ParentInfo const& parentInfo)
    {
        return _stream << parentInfo.blockNumber << parentInfo.blockHash;
    }
};
using ParentInfoList = std::vector<ParentInfo>;
using ParentInfoListPtr = std::shared_ptr<ParentInfoList>;

struct Signature
{
    int64_t index;
    bytes signature;

    template <class Stream, typename = std::enable_if_t<Stream::is_decoder_stream>>
    friend Stream& operator>>(Stream& _stream, Signature& signature)
    {
        return _stream >> signature.index >> signature.signature;
    }

    template <class Stream, typename = std::enable_if_t<Stream::is_encoder_stream>>
    friend Stream& operator<<(Stream& _stream, Signature const& signature)
    {
        return _stream << signature.index << signature.signature;
    }
};
using SignatureList = std::vector<Signature>;
using SignatureListPtr = std::shared_ptr<SignatureList>;

// the weight list
using WeightList = std::vector<uint64_t>;
using WeightListPtr = std::shared_ptr<WeightList>;

int64_t constexpr InvalidSealerIndex = INT64_MAX;

struct Session
{
    using Ptr = std::shared_ptr<Session>;
    using ConstPtr = std::shared_ptr<const Session>;

    enum Status
    {
        STARTED = 0,
        DIRTY,
        COMMITTED,
        ROLLBACKED
    };

    long sessionID;
    Status status;
    bcos::protocol::BlockNumber beginNumber;  // [
    bcos::protocol::BlockNumber endNumber;    // )
};

inline std::ostream& operator<<(std::ostream& _out, NodeType const& _nodeType)
{
    switch (_nodeType)
    {
    case NodeType::None:
        _out << "None";
        break;
    case NodeType::CONSENSUS_NODE:
        _out << "consensus";
        break;
    case NodeType::OBSERVER_NODE:
        _out << "observer";
        break;
    case NodeType::NODE_OUTSIDE_GROUP:
        _out << "nodeOutsideGroup";
        break;
    default:
        _out << "Unknown";
        break;
    }
    return _out;
}
}  // namespace protocol
}  // namespace bcos