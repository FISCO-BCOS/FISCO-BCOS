/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/**
 * @file: Message.h
 * @author: monan
 *
 * @date: 2017
 */

#pragma once

#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/easylog.h>
#include <boost/lexical_cast.hpp>
#include <memory>
#include <queue>
#include <string>
#include <thread>

namespace dev
{
namespace channel
{
class Message : public std::enable_shared_from_this<Message>
{
public:
    typedef std::shared_ptr<Message> Ptr;

    Message() { _data = std::make_shared<bytes>(); }
    virtual ~Message(){};

    virtual void encode(bytes& buffer) = 0;

    virtual ssize_t decode(const byte* buffer, size_t size) = 0;

    virtual uint32_t length() { return _length; }

    virtual uint16_t type() { return _type; }
    virtual void setType(uint16_t type) { _type = type; }

    virtual std::string seq() { return _seq; }
    virtual void setSeq(std::string seq) { _seq = seq; }

    virtual int result() { return _result; }
    virtual void setResult(int result) { _result = result; }

    virtual byte* data() { return _data->data(); }
    virtual size_t dataSize() { return _data->size(); }

    virtual void setData(const byte* p, size_t size) { _data->assign(p, p + size); }

    virtual void clearData() { _data->clear(); }

protected:
    uint32_t _length = 0;
    uint16_t _type = 0;
    std::string _seq = std::string(32, '0');
    int _result = 0;

    std::shared_ptr<bytes> _data;
};

class MessageFactory : public std::enable_shared_from_this<MessageFactory>
{
public:
    typedef std::shared_ptr<MessageFactory> Ptr;

    virtual ~MessageFactory(){};
    virtual Message::Ptr buildMessage() = 0;
};

}  // namespace channel

}  // namespace dev
