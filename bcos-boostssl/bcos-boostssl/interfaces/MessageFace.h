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
 * @file MessageFace.h
 * @author: lucasli
 * @date 2022-02-16
 */
#pragma once

#include <bcos-boostssl/interfaces/Common.h>
#include <bcos-utilities/Common.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <string>

namespace bcos
{
namespace boostssl
{
class MessageFace
{
public:
    using Ptr = std::shared_ptr<MessageFace>;

public:
    virtual ~MessageFace() {}

    virtual uint16_t version() const = 0;
    virtual void setVersion(uint16_t) = 0;
    virtual uint16_t packetType() const = 0;
    virtual void setPacketType(uint16_t) = 0;
    virtual std::string seq() const = 0;
    virtual void setSeq(std::string) = 0;
    virtual uint16_t ext() const = 0;
    virtual void setExt(uint16_t) = 0;
    virtual std::shared_ptr<bytes> payload() const = 0;
    virtual void setPayload(std::shared_ptr<bcos::bytes>) = 0;

    virtual bool encode(bcos::bytes& _buffer) = 0;
    virtual int64_t decode(bytesConstRef _buffer) = 0;

public:
    virtual bool isRespPacket() const { return (m_ext & MessageExtFieldFlag::Response) != 0; }
    virtual void setRespPacket() { m_ext |= MessageExtFieldFlag::Response; }

protected:
    uint16_t m_version = 0;
    uint16_t m_packetType = 0;
    std::string m_seq;
    uint16_t m_ext = 0;
    std::shared_ptr<bcos::bytes> m_payload;
};

class MessageFaceFactory
{
public:
    using Ptr = std::shared_ptr<MessageFaceFactory>;

public:
    virtual ~MessageFaceFactory() {}
    virtual MessageFace::Ptr buildMessage() = 0;
    virtual std::string newSeq() = 0;
};

}  // namespace boostssl
}  // namespace bcos