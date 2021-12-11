
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
#include <bcos-boostssl/network/Common.h>
#include <bcos-boostssl/network/Message.h>
#include <boost/asio.hpp>

namespace boostssl
{
namespace net
{
class SocketFace;

using SessionCallbackFunc = std::function<void(NetworkException, Message::Ptr)>;
struct ResponseCallback : public std::enable_shared_from_this<ResponseCallback>
{
    using Ptr = std::shared_ptr<ResponseCallback>;

    SessionCallbackFunc callback;
    std::shared_ptr<boost::asio::deadline_timer> timeoutHandler;
};

class SessionFace
{
public:
    virtual ~SessionFace(){};

    using Ptr = std::shared_ptr<SessionFace>;

    virtual void start() = 0;
    virtual void disconnect(DisconnectReason) = 0;

    virtual void asyncSendMessage(
        Message::Ptr, Options = Options(), SessionCallbackFunc = SessionCallbackFunc()) = 0;

    virtual std::shared_ptr<SocketFace> socket() = 0;

    virtual void setMessageHandler(
        std::function<void(NetworkException, std::shared_ptr<SessionFace>, Message::Ptr)>
            messageHandler) = 0;

    virtual NodeIPEndpoint nodeIPEndpoint() const = 0;

    virtual bool actived() const = 0;
};
}  // namespace net
}  // namespace boostssl
