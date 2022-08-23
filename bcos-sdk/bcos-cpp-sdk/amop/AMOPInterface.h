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
 * @file AMOPInterface.h
 * @author: octopus
 * @date 2021-08-23
 */
#pragma once

#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <functional>
#include <memory>
#include <mutex>
#include <set>

namespace bcos
{
namespace cppsdk
{
namespace amop
{
using SubCallback = std::function<void(bcos::Error::Ptr, const std::string&, const std::string&,
    bcos::bytesConstRef, std::shared_ptr<bcos::boostssl::ws::WsSession>)>;
using PubCallback =
    std::function<void(bcos::Error::Ptr, std::shared_ptr<bcos::boostssl::ws::WsMessage>,
        std::shared_ptr<bcos::boostssl::ws::WsSession>)>;
class AMOPInterface
{
public:
    using Ptr = std::shared_ptr<AMOPInterface>;
    using UniquePtr = std::unique_ptr<AMOPInterface>;

    virtual ~AMOPInterface() {}

public:
    virtual void start() = 0;
    virtual void stop() = 0;

public:
    // subscribe topics
    virtual void subscribe(const std::set<std::string>& _topics) = 0;
    // subscribe topics
    virtual void unsubscribe(const std::set<std::string>& _topics) = 0;
    // subscribe topic with callback
    virtual void subscribe(const std::string& _topic, SubCallback _callback) = 0;
    // publish message
    virtual void publish(const std::string& _topic, bcos::bytesConstRef _data, uint32_t timeout,
        PubCallback _callback) = 0;
    // broadcast message
    virtual void broadcast(const std::string& _topic, bcos::bytesConstRef _data) = 0;
    // query all subscribed topics
    virtual void querySubTopics(std::set<std::string>& _topics) = 0;
    // set default callback
    virtual void setSubCallback(SubCallback _callback) = 0;
    //
    virtual void sendResponse(
        const std::string& _endPoint, const std::string& _seq, bcos::bytesConstRef _data) = 0;
};

}  // namespace amop
}  // namespace cppsdk
}  // namespace bcos