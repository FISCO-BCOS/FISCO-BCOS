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
 * @brief AMOP pub/sub behind a small interface so the amop command runs without a node in tests.
 *        AMOP publish is request/response: the node answers the publisher only after a
 *        subscriber replied (sendResponse) or with an error status (no subscriber anywhere:
 *        3001 NotFoundPeerByTopicSendMsg, CommonError.h).
 * @file AmopClient.h
 */
#pragma once

#include "bcos-ops/RpcCall.h"
#include <bcos-utilities/Common.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace bcos::ops
{
struct AmopMessage
{
    std::string topic;
    std::string from;  // endpoint of the node that delivered the request
    bytes data;
    /// answers the publisher; without a reply the publisher's call times out
    std::function<void(bytesConstRef)> reply;
};

struct AmopPublishResult
{
    bool ok = false;
    int32_t code = 0;
    std::string message;
    bytes response;  // the subscriber's reply payload
};

class AmopTransport
{
public:
    virtual ~AmopTransport() = default;
    /// registers _topic with the node; _onMessage runs on the transport's io thread
    virtual void subscribe(
        std::string const& _topic, std::function<void(AmopMessage)> _onMessage) = 0;
    /// blocks until the node answers or _timeoutMs elapses
    virtual AmopPublishResult publish(
        std::string const& _topic, bytesConstRef _data, int _timeoutMs) = 0;
    /// waits, at most _limit, until every AmopMessage::reply made so far has left the process.
    /// reply() only queues the response; one still queued when the process exits is lost with
    /// the connection and its publisher times out (#5722)
    virtual void drainReplies(std::chrono::milliseconds _limit) = 0;
};

/// the SDK AMOP client of a WebSocket Connection (connect() with allowIpc=false; the unix
/// socket carries JSON-RPC only). Throws OpsError{1} for a connection without an SDK.
std::unique_ptr<AmopTransport> makeAmopTransport(Connection const& _connection);
}  // namespace bcos::ops
