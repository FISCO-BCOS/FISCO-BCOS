
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
#include "bcos-utilities/Error.h"
#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-gateway/libnetwork/Message.h>
#include <bcos-gateway/libnetwork/SessionCallback.h>
#include <boost/asio.hpp>
#include <optional>

namespace bcos
{
namespace gateway
{
class SocketFace;

class SessionFace
{
public:
    using Ptr = std::shared_ptr<SessionFace>;

    SessionFace() = default;
    SessionFace(const SessionFace&) = delete;
    SessionFace(SessionFace&&) = delete;
    SessionFace& operator=(SessionFace&&) = delete;
    SessionFace& operator=(const SessionFace&) = delete;
    virtual ~SessionFace() noexcept = default;

    virtual void start() = 0;
    virtual void disconnect(DisconnectReason) = 0;

    virtual void asyncSendMessage(
        Message::Ptr, Options = Options(), SessionCallbackFunc = SessionCallbackFunc()) = 0;

    virtual std::shared_ptr<SocketFace> socket() = 0;

    virtual void setMessageHandler(
        std::function<void(NetworkException, SessionFace::Ptr, Message::Ptr)> messageHandler) = 0;

    virtual void setBeforeMessageHandler(
        std::function<std::optional<bcos::Error>(SessionFace::Ptr, Message::Ptr)> handler) = 0;

    virtual NodeIPEndpoint nodeIPEndpoint() const = 0;

    virtual bool active() const = 0;

    virtual std::size_t writeQueueSize() = 0;
};
}  // namespace gateway
}  // namespace bcos
