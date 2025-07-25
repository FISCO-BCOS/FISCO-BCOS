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
 * @file Common.h
 * @author: octopus
 * @date 2021-07-08
 */
#pragma once

#include <bcos-boostssl/interfaces/MessageFace.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Error.h>
#include <boost/asio/ssl.hpp>
#include <boost/beast/websocket.hpp>

#define BOOST_SSL_LOG(LEVEL) BCOS_LOG(LEVEL) << "[BOOSTSSL]"
#define WEBSOCKET_TOOL(LEVEL) BCOS_LOG(LEVEL) << "[WS][TOOL]"
#define WEBSOCKET_CONNECTOR(LEVEL) BCOS_LOG(LEVEL) << "[WS][CONNECTOR]"
#define WEBSOCKET_VERSION(LEVEL) BCOS_LOG(LEVEL) << "[WS][VERSION]"
#define WEBSOCKET_SESSION(LEVEL) BCOS_LOG(LEVEL) << "[WS][SESSION]"
#define WEBSOCKET_MESSAGE(LEVEL) BCOS_LOG(LEVEL) << "[WS][MESSAGE]"
#define WEBSOCKET_SERVICE(LEVEL) BCOS_LOG(LEVEL) << "[WS][SERVICE]"
#define WEBSOCKET_STREAM(LEVEL) BCOS_LOG(LEVEL) << "[WS][STREAM]"
#define WEBSOCKET_SSL_STREAM(LEVEL) BCOS_LOG(LEVEL) << "[WS][SSL][STREAM]"
#define WEBSOCKET_INITIALIZER(LEVEL) BCOS_LOG(LEVEL) << "[WS][INITIALIZER]"

// Note!!! 0xffff is raw ws message type, user should not use this type
constexpr uint16_t WS_RAW_MESSAGE_TYPE = 0xffff;

namespace bcos::boostssl::ws
{
class WsSession;

using RespCallBack = std::function<void(
    bcos::Error::Ptr, std::shared_ptr<boostssl::MessageFace>, std::shared_ptr<WsSession>)>;

using WsConnectHandler = std::function<void(bcos::Error::Ptr, std::shared_ptr<WsSession>)>;
using WsDisconnectHandler = std::function<void(bcos::Error::Ptr, std::shared_ptr<WsSession>)>;
using WsRecvMessageHandler =
    std::function<void(std::shared_ptr<boostssl::MessageFace>, std::shared_ptr<WsSession>)>;
using VerifyCallback = boost::function<bool(bool, boost::asio::ssl::verify_context&)>;

struct Options
{
    Options(uint32_t _timeout) : timeout(_timeout) {}
    Options() = default;
    uint32_t timeout = 0;  ///< The timeout value of async function, in milliseconds.
};

}  // namespace bcos::boostssl::ws
