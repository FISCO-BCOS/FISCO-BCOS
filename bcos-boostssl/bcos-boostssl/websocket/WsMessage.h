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

#include <bcos-boostssl/utilities/Common.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <iterator>
#include <memory>
#include <utility>

namespace bcos
{
namespace boostssl
{
namespace ws
{
// the message format for ws protocol
class WsMessage
{
public:
    using Ptr = std::shared_ptr<WsMessage>;
    // seq field length
    const static size_t SEQ_LENGTH = 32;
    /// type(2) + status(2) + seq(32) + data(N)
    const static size_t MESSAGE_MIN_LENGTH = 36;

public:
    WsMessage()
    {
        m_seq = std::make_shared<bcos::boostssl::utilities::bytes>(SEQ_LENGTH, 0);
        m_data = std::make_shared<bcos::boostssl::utilities::bytes>();
    }

    virtual ~WsMessage() {}

public:
    virtual uint16_t type() { return m_type; }
    virtual void setType(uint16_t _type) { m_type = _type; }
    virtual uint16_t status() { return m_status; }
    virtual void setStatus(uint16_t _status) { m_status = _status; }
    virtual std::shared_ptr<bcos::boostssl::utilities::bytes> seq() { return m_seq; }
    virtual void setSeq(std::shared_ptr<bcos::boostssl::utilities::bytes> _seq) { m_seq = _seq; }
    virtual std::shared_ptr<bcos::boostssl::utilities::bytes> data() { return m_data; }
    virtual void setData(std::shared_ptr<bcos::boostssl::utilities::bytes> _data)
    {
        m_data = _data;
    }

public:
    virtual bool encode(bcos::boostssl::utilities::bytes& _buffer);
    virtual int64_t decode(const bcos::boostssl::utilities::byte* _buffer, std::size_t _size);

private:
    uint16_t m_type{0};
    uint16_t m_status{0};
    std::shared_ptr<bcos::boostssl::utilities::bytes> m_seq;
    std::shared_ptr<bcos::boostssl::utilities::bytes> m_data;
};


class WsMessageFactory
{
public:
    using Ptr = std::shared_ptr<WsMessageFactory>;
    WsMessageFactory() = default;
    virtual ~WsMessageFactory() {}

public:
    virtual std::string newSeq()
    {
        std::string seq = boost::uuids::to_string(boost::uuids::random_generator()());
        seq.erase(std::remove(seq.begin(), seq.end(), '-'), seq.end());
        return seq;
    }

    virtual std::shared_ptr<WsMessage> buildMessage()
    {
        auto msg = std::make_shared<WsMessage>();
        auto seq = newSeq();
        msg->setSeq(std::make_shared<boostssl::utilities::bytes>(seq.begin(), seq.end()));
        return msg;
    }

    virtual std::shared_ptr<WsMessage> buildMessage(
        uint16_t _type, std::shared_ptr<boostssl::utilities::bytes> _data)
    {
        auto msg = std::make_shared<WsMessage>();
        auto seq = newSeq();
        msg->setType(_type);
        msg->setData(_data);
        msg->setSeq(std::make_shared<utilities::bytes>(seq.begin(), seq.end()));
        return msg;
    }
};

}  // namespace ws
}  // namespace boostssl
}  // namespace bcos