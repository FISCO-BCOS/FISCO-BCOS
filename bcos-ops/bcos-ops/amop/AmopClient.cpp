/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file AmopClient.cpp
 */
#include "AmopClient.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/RpcClient.h"
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-boostssl/websocket/WsStream.h>
#include <bcos-cpp-sdk/Sdk.h>
#include <boost/asio/post.hpp>
#include <algorithm>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace bcos::ops
{
namespace
{
class SdkAmopTransport : public AmopTransport
{
public:
    explicit SdkAmopTransport(std::shared_ptr<bcos::cppsdk::Sdk> _sdk) : m_sdk(std::move(_sdk)) {}

    void subscribe(std::string const& _topic, std::function<void(AmopMessage)> _onMessage) override
    {
        auto amop = m_sdk->amop();
        amop->subscribe(_topic, [_topic, _onMessage, amop, replied = m_replied](
                                    bcos::Error::Ptr _error, std::string const& _endpoint,
                                    std::string const& _seq, bytesConstRef _data, auto _session) {
            if (_error && _error->errorCode() != 0)
            {
                return;
            }
            AmopMessage message;
            message.topic = _topic;
            message.from = _endpoint;
            message.data.assign(_data.begin(), _data.end());
            // a broadcast arrives through the same callback with an empty seq; the node
            // has no handler for a response to it (WsService warns on every one), so only
            // request/response deliveries get a reply
            if (!_seq.empty())
            {
                message.reply = [amop, _endpoint, _seq, replied, session = std::weak_ptr(_session)](
                                    bytesConstRef _reply) {
                    // asyncSendMessage only enqueues; safe from inside the io callback
                    amop->sendResponse(_endpoint, _seq, _reply);
                    replied->add(session);
                };
            }
            _onMessage(std::move(message));
        });
    }

    AmopPublishResult publish(
        std::string const& _topic, bytesConstRef _data, int _timeoutMs) override
    {
        auto promise = std::make_shared<std::promise<AmopPublishResult>>();
        auto future = promise->get_future();
        m_sdk->amop()->publish(_topic, _data, static_cast<uint32_t>(_timeoutMs),
            [promise](bcos::Error::Ptr _error,
                std::shared_ptr<bcos::boostssl::ws::WsMessage> _message, auto /*session*/) {
                AmopPublishResult result;
                if (_error && _error->errorCode() != 0)
                {
                    result.code = _error->errorCode();
                    result.message = _error->errorMessage();
                }
                else
                {
                    result.ok = true;
                    if (_message)
                    {
                        auto payload = _message->payload();
                        result.response.assign(payload.begin(), payload.end());
                    }
                }
                try
                {
                    promise->set_value(std::move(result));
                }
                catch (std::future_error const&)
                {}
            });
        if (future.wait_for(std::chrono::milliseconds(_timeoutMs + 500)) !=
            std::future_status::ready)
        {
            AmopPublishResult timedOut;
            timedOut.code = -1;
            timedOut.message = "publish timed out after " + std::to_string(_timeoutMs) + " ms";
            return timedOut;
        }
        return future.get();
    }

    void drainReplies(std::chrono::milliseconds _limit) override
    {
        // reply() does not touch the write queue: WsSession::asyncSendMessage posts
        // WsSession::send onto the session's executor. The SDK runs a single io thread
        // (SdkFactory: IOServicePool(1, "sdk")), so a barrier posted now runs after every send
        // posted before it. Past the barrier, WsSession::send has queued the reply and started
        // the head write under x_writeQueue, and asio hands a starting write to the kernel at
        // once (its speculative send), which std::_Exit does not discard. So an empty queue
        // after the barrier means every reply left; one only waits there behind a write that
        // was already in flight
        std::vector<std::weak_ptr<bcos::boostssl::ws::WsSession>> sessions;
        {
            std::lock_guard<std::mutex> lock(m_replied->mutex);
            sessions.swap(m_replied->sessions);
        }
        auto deadline = std::chrono::steady_clock::now() + _limit;
        for (auto const& weak : sessions)
        {
            auto session = weak.lock();
            if (!session || !session->isConnected())
            {
                continue;
            }
            auto barrier = std::make_shared<std::promise<void>>();
            auto passed = barrier->get_future();
            boost::asio::post(session->wsStreamDelegate()->tcpStream().get_executor(),
                [barrier]() { barrier->set_value(); });
            if (passed.wait_until(deadline) != std::future_status::ready)
            {
                return;
            }
            while (session->isConnected() && session->writeQueueSize() != 0 &&
                   std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

private:
    /// the sessions replies were queued on, for drainReplies; shared with the io callbacks
    struct RepliedSessions
    {
        std::mutex mutex;
        std::vector<std::weak_ptr<bcos::boostssl::ws::WsSession>> sessions;
        /// each session once: a long `sub --count 0` replies on one connection for ever
        void add(std::weak_ptr<bcos::boostssl::ws::WsSession> const& _session)
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto same = [&](auto const& _known) {
                return !_known.owner_before(_session) && !_session.owner_before(_known);
            };
            if (std::none_of(sessions.begin(), sessions.end(), same))
            {
                sessions.push_back(_session);
            }
        }
    };
    std::shared_ptr<bcos::cppsdk::Sdk> m_sdk;
    std::shared_ptr<RepliedSessions> m_replied = std::make_shared<RepliedSessions>();
};
}  // namespace

std::unique_ptr<AmopTransport> makeAmopTransport(Connection const& _connection)
{
    return std::make_unique<SdkAmopTransport>(sdkOf(_connection));
}
}  // namespace bcos::ops
