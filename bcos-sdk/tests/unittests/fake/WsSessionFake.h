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
 * @file WsSessionFake.h
 * @author: octopus
 * @date 2021-09-24
 */
#pragma once
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/IOServicePool.h>

namespace bcos
{
namespace cppsdk
{
namespace test
{
class WsSessionFake
{
public:
    using Ptr = std::shared_ptr<WsSessionFake>;

    // WsSession is no longer inheritable for overriding, the fake wraps a real
    // session and only exposes the methods used by the tests
    WsSessionFake(IOServicePool::Ptr ioServicePool)
      : m_session(std::make_shared<bcos::boostssl::ws::WsSession>(std::move(ioServicePool)))
    {
        WEBSOCKET_SESSION(INFO) << LOG_KV("[NEWOBJ][WSSESSION]", this);
    }

public:
    std::shared_ptr<bcos::boostssl::ws::WsSession> session() const { return m_session; }

private:
    std::shared_ptr<bcos::boostssl::ws::WsSession> m_session;
};
}  // namespace test
}  // namespace cppsdk
}  // namespace bcos