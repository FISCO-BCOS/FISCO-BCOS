/**
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
 * @brief interface for RPC
 * @file Rpc.cpp
 * @author: octopus
 * @date 2021-07-15
 */

#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-framework/libutilities/Log.h>
#include <bcos-rpc/Common.h>
#include <bcos-rpc/Rpc.h>
using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::group;
using namespace bcos::protocol;
using namespace bcos::boostssl::ws;

void Rpc::start()
{
    // start event sub
    m_eventSub->start();
    // start websocket service
    m_wsService->start();
    m_amopClient->start();
    BCOS_LOG(INFO) << LOG_DESC("[RPC][RPC][start]") << LOG_DESC("start rpc successfully");
}

void Rpc::stop()
{
    // stop ws service
    if (m_wsService)
    {
        m_wsService->stop();
    }

    // stop event sub
    if (m_eventSub)
    {
        m_eventSub->stop();
    }
    if (m_amopClient)
    {
        m_amopClient->stop();
    }

    BCOS_LOG(INFO) << LOG_DESC("[RPC][RPC][stop]") << LOG_DESC("stop rpc successfully");
}

/**
 * @brief: notify blockNumber to rpc
 * @param _blockNumber: blockNumber
 * @param _callback: resp callback
 * @return void
 */
void Rpc::asyncNotifyBlockNumber(std::string const& _groupID, std::string const& _nodeName,
    bcos::protocol::BlockNumber _blockNumber, std::function<void(Error::Ptr)> _callback)
{
    auto ss = m_wsService->sessions();
    for (const auto& s : ss)
    {
        if (s && s->isConnected())
        {
            std::string group;
            // eg: {"blockNumber": 11, "group": "group"}
            Json::Value response;
            response["group"] = _groupID;
            response["nodeName"] = _nodeName;
            response["blockNumber"] = _blockNumber;
            auto resp = response.toStyledString();
            auto message =
                m_wsService->messageFactory()->buildMessage(bcos::rpc::MessageType::BLOCK_NOTIFY,
                    std::make_shared<bcos::bytes>(resp.begin(), resp.end()));
            s->asyncSendMessage(message);
        }
    }

    if (_callback)
    {
        _callback(nullptr);
    }
    m_jsonRpcImpl->groupManager()->updateGroupBlockInfo(_groupID, _nodeName, _blockNumber);
    WEBSOCKET_SERVICE(TRACE) << LOG_BADGE("asyncNotifyBlockNumber")
                             << LOG_KV("blockNumber", _blockNumber) << LOG_KV("ss size", ss.size());
}

void Rpc::asyncNotifyGroupInfo(
    bcos::group::GroupInfo::Ptr _groupInfo, std::function<void(Error::Ptr&&)> _callback)
{
    m_jsonRpcImpl->groupManager()->updateGroupInfo(_groupInfo);
    if (_callback)
    {
        _callback(nullptr);
    }
}

void Rpc::notifyGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo)
{
    // notify the groupInfo to SDK
    auto sdkSessions = m_wsService->sessions();
    for (auto const& session : sdkSessions)
    {
        if (!session || !session->isConnected())
        {
            continue;
        }
        Json::Value groupInfoJson;
        groupInfoToJson(groupInfoJson, _groupInfo);
        auto response = groupInfoJson.toStyledString();
        auto message =
            m_wsService->messageFactory()->buildMessage(bcos::rpc::MessageType::GROUP_NOTIFY,
                std::make_shared<bcos::bytes>(response.begin(), response.end()));
        session->asyncSendMessage(message);
    }
}