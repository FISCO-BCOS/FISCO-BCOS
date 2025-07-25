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

#include <bcos-utilities/Common.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <string>

namespace bcos::boostssl
{
class MessageFace
{
public:
    using Ptr = std::shared_ptr<MessageFace>;

    MessageFace() = default;
    MessageFace(const MessageFace&) = default;
    MessageFace(MessageFace&&) noexcept = default;
    MessageFace& operator=(const MessageFace&) = default;
    MessageFace& operator=(MessageFace&&) noexcept = default;
    virtual ~MessageFace() = default;

    virtual uint16_t version() const = 0;
    virtual void setVersion(uint16_t) = 0;
    virtual uint16_t packetType() const = 0;
    virtual void setPacketType(uint16_t) = 0;
    virtual std::string const& seq() const = 0;
    virtual void setSeq(std::string) = 0;
    virtual uint16_t ext() const = 0;
    virtual void setExt(uint16_t) = 0;
    virtual std::shared_ptr<bytes> payload() const = 0;
    virtual void setPayload(std::shared_ptr<bcos::bytes>) = 0;

    virtual bool encode(bcos::bytes& _buffer) = 0;
    virtual int64_t decode(bytesConstRef _buffer) = 0;

    virtual bool isRespPacket() const = 0;
    virtual void setRespPacket() = 0;
    virtual uint32_t length() const = 0;
};

class MessageFaceFactory
{
public:
    using Ptr = std::shared_ptr<MessageFaceFactory>;

    MessageFaceFactory() = default;
    MessageFaceFactory(const MessageFaceFactory&) = default;
    MessageFaceFactory(MessageFaceFactory&&) noexcept = default;
    MessageFaceFactory& operator=(const MessageFaceFactory&) = default;
    MessageFaceFactory& operator=(MessageFaceFactory&&) noexcept = default;
    virtual ~MessageFaceFactory() = default;

    virtual MessageFace::Ptr buildMessage() = 0;

    virtual std::string newSeq()
    {
        std::string seq = boost::uuids::to_string(boost::uuids::random_generator()());
        seq.erase(std::remove(seq.begin(), seq.end(), '-'), seq.end());
        return seq;
    }
};

}  // namespace bcos::boostssl
