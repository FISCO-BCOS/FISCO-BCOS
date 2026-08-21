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
 * @file WsServiceFake.h
 * @author: octopus
 * @date 2021-09-24
 */
#pragma once
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-utilities/Common.h>

namespace bcos
{
namespace cppsdk
{
namespace test
{
class WsServiceFake
{
public:
    using Ptr = std::shared_ptr<WsServiceFake>;

public:
    // WsService is no longer inheritable for overriding, the fake is
    // self-contained and only exposes the methods used by the tests
    void asyncSendMessage(const bcos::boostssl::ws::WsMessage& _msg,
        bcos::boostssl::ws::Options _options = bcos::boostssl::ws::Options(),
        bcos::boostssl::ws::RespCallBack _respFunc = bcos::boostssl::ws::RespCallBack())
    {
        (void)_msg;
        (void)_options;

        _respFunc(m_error, bcos::boostssl::ws::WsMessage(), m_session);
    }

public:
    void setError(bcos::Error::Ptr _error) { m_error = _error; }
    void setResp(std::shared_ptr<bcos::bytes> _resp) { m_resp = _resp; }
    void setSession(std::shared_ptr<bcos::boostssl::ws::WsSession> _session)
    {
        m_session = _session;
    }

private:
    bcos::Error::Ptr m_error;
    std::shared_ptr<bcos::bytes> m_resp;
    std::shared_ptr<bcos::boostssl::ws::WsSession> m_session;
};
}  // namespace test
}  // namespace cppsdk
}  // namespace bcos