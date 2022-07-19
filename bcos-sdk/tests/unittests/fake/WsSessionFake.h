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

namespace bcos
{
namespace cppsdk
{
namespace test
{
class WsSessionFake : public bcos::boostssl::ws::WsSession
{
public:
    WsSessionFake(std::string _moduleName) : bcos::boostssl::ws::WsSession(_moduleName)
    {
        WEBSOCKET_SESSION(INFO) << LOG_KV("[NEWOBJ][WSSESSION]", this);
    }
    using Ptr = std::shared_ptr<WsSessionFake>;

public:
    virtual void asyncSendMessage(std::shared_ptr<bcos::boostssl::MessageFace> _msg,
        bcos::boostssl::ws::Options _options = bcos::boostssl::ws::Options(-1),
        bcos::boostssl::ws::RespCallBack _respCallback =
            bcos::boostssl::ws::RespCallBack()) override
    {
        (void)_msg;
        (void)_options;
        auto msg = std::make_shared<bcos::boostssl::ws::WsMessage>();
        msg->setPayload(m_resp);
        auto session = shared_from_this();
        _respCallback(m_error, msg, session);
    }

public:
    virtual bool isConnected() override { return true; }

public:
    void setError(bcos::Error::Ptr _error) { m_error = _error; }
    void setResp(std::shared_ptr<bcos::bytes> _resp) { m_resp = _resp; }

private:
    bcos::Error::Ptr m_error;
    std::shared_ptr<bcos::bytes> m_resp;
};
}  // namespace test
}  // namespace cppsdk
}  // namespace bcos