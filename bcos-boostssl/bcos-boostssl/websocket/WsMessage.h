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
 * @file WsMessage.h
 * @author: octopus
 * @date 2021-07-28
 */
#pragma once

#include <bcos-boostssl/interfaces/MessageFace.h>
#include <bcos-utilities/Common.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#define CHECK_OFFSET(offset, length)                                                         \
    do                                                                                       \
    {                                                                                        \
        if ((offset) > (length))                                                             \
        {                                                                                    \
            throw std::out_of_range("Out of range error, offset:" + std::to_string(offset) + \
                                    " ,length: " + std::to_string(length));                  \
        }                                                                                    \
    } while (0);

namespace bcos
{
namespace boostssl
{
namespace ws
{
// the message format for ws protocol
class WsMessage : public boostssl::MessageFace
{
public:

    // version(2) + type(2) + status(2) + seqLength(2) + ext(2) + payload(N)
    const static size_t MESSAGE_MIN_LENGTH;

    using Ptr = std::shared_ptr<WsMessage>;
    WsMessage() { m_payload = std::make_shared<bcos::bytes>(); }
    virtual ~WsMessage() {}


    virtual uint16_t version() const override { return m_version; }
    virtual void setVersion(uint16_t) override {}
    virtual uint16_t packetType() const override { return m_packetType; }
    virtual void setPacketType(uint16_t _packetType) override { m_packetType = _packetType; }
    virtual int16_t status() { return m_status; }
    virtual void setStatus(int16_t _status) { m_status = _status; }
    virtual std::string const& seq() const override { return m_seq; }
    virtual void setSeq(std::string _seq) override { m_seq = _seq; }
    virtual std::shared_ptr<bcos::bytes> payload() const override { return m_payload; }
    virtual void setPayload(std::shared_ptr<bcos::bytes> _payload) override
    {
        m_payload = _payload;
    }
    virtual uint16_t ext() const override { return m_ext; }
    virtual void setExt(uint16_t _ext) override { m_ext = _ext; }


    virtual bool encode(bcos::bytes& _buffer) override;
    virtual int64_t decode(bytesConstRef _buffer) override;

    bool isRespPacket() const override { return (m_ext & MessageExtFieldFlag::Response) != 0; }
    void setRespPacket() override { m_ext |= MessageExtFieldFlag::Response; }

    virtual uint32_t length() const override { return m_length; }

private:
    uint16_t m_version = 0;
    uint16_t m_packetType = 0;
    std::string m_seq;
    uint16_t m_ext = 0;
    std::shared_ptr<bcos::bytes> m_payload;

    int16_t m_status{0};
    uint32_t m_length;
};

class WsMessageFactory : public boostssl::MessageFaceFactory
{
public:
    using Ptr = std::shared_ptr<WsMessageFactory>;
    WsMessageFactory() = default;
    virtual ~WsMessageFactory() {}

public:
    virtual std::string newSeq() override
    {
        std::string seq = boost::uuids::to_string(boost::uuids::random_generator()());
        seq.erase(std::remove(seq.begin(), seq.end(), '-'), seq.end());
        return seq;
    }

    virtual boostssl::MessageFace::Ptr buildMessage() override
    {
        auto msg = std::make_shared<WsMessage>();
        return msg;
    }

    virtual std::shared_ptr<WsMessage> buildMessage(
        uint16_t _type, std::shared_ptr<bcos::bytes> _data)
    {
        auto msg = std::make_shared<WsMessage>();

        msg->setPacketType(_type);
        msg->setPayload(_data);

        return msg;
    }
};

}  // namespace ws
}  // namespace boostssl
}  // namespace bcos
