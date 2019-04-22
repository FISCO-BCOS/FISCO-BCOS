/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Session.h
 * @author Gav Wood <i@gavwood.com>
 * @author Alex Leverington <nessence@gmail.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 *
 * @author: yujiechen
 * @date: 2018-09-19
 * @modification: remove addNote interface
 */

#pragma once
#include "Common.h"
#include <memory>

namespace dev
{
namespace network
{
class SocketFace;
#define CallbackFunc std::function<void(dev::network::NetworkException, dev::network::Message::Ptr)>

struct ResponseCallback : public std::enable_shared_from_this<ResponseCallback>
{
    typedef std::shared_ptr<ResponseCallback> Ptr;

    uint64_t m_startTime;
    CallbackFunc callbackFunc;
    std::shared_ptr<boost::asio::deadline_timer> timeoutHandler;
};

class SessionFace
{
public:
    virtual ~SessionFace(){};

    typedef std::shared_ptr<SessionFace> Ptr;

    virtual void start() = 0;
    virtual void disconnect(DisconnectReason) = 0;

    virtual void asyncSendMessage(
        Message::Ptr, Options = Options(), CallbackFunc = CallbackFunc()) = 0;

    virtual std::shared_ptr<SocketFace> socket() = 0;

    virtual void setMessageHandler(
        std::function<void(NetworkException, std::shared_ptr<SessionFace>, Message::Ptr)>
            messageHandler) = 0;

    virtual NodeIPEndpoint nodeIPEndpoint() const = 0;

    virtual bool actived() const = 0;
};
}  // namespace network
}  // namespace dev
