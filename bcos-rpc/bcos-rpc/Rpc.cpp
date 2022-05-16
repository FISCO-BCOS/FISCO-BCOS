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

#include <bcos-boostssl/websocket/WsError.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-framework/interfaces/Common.h>
#include <bcos-framework/interfaces/protocol/GlobalConfig.h>
#include <bcos-framework/interfaces/rpc/HandshakeRequest.h>
#include <bcos-rpc/Common.h>
#include <bcos-rpc/Rpc.h>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::group;
using namespace bcos::protocol;
using namespace bcos::boostssl::ws;

Rpc::Rpc(std::shared_ptr<boostssl::ws::WsService> _wsService,
    bcos::rpc::JsonRpcImpl_2_0::Ptr _jsonRpcImpl, bcos::event::EventSub::Ptr _eventSub,
    AMOPClient::Ptr _amopClient)
  : m_wsService(_wsService),
    m_jsonRpcImpl(_jsonRpcImpl),
    m_eventSub(_eventSub),
    m_amopClient(_amopClient),
    m_groupManager(m_jsonRpcImpl->groupManager())
{
    // init the local protocol
    m_localProtocol = g_BCOSConfig.protocolInfo(ProtocolModuleID::RpcService);
    // group information notification
    m_jsonRpcImpl->groupManager()->registerGroupInfoNotifier(
        [this](bcos::group::GroupInfo::Ptr _groupInfo) { notifyGroupInfo(_groupInfo); });
    // block number notification
    m_jsonRpcImpl->groupManager()->registerBlockNumberNotifier(
        [this](std::string const& _groupID, std::string const& _nodeName,
            bcos::protocol::BlockNumber _blockNumber) {
            asyncNotifyBlockNumber(_groupID, _nodeName, _blockNumber, [](Error::Ptr _error) {
                if (_error)
                {
                    RPC_LOG(ERROR) << LOG_DESC("asyncNotifyBlockNumber error")
                                   << LOG_KV("code", _error->errorCode())
                                   << LOG_KV("msg", _error->errorMessage());
                }
            });
        });

    // handshake msgHandler
    m_wsService->registerMsgHandler(bcos::protocol::MessageType::HANDESHAKE,
        boost::bind(
            &Rpc::onRecvHandshakeRequest, this, boost::placeholders::_1, boost::placeholders::_2));
}

void Rpc::start()
{
    // start event sub
    m_eventSub->start();
    // start websocket service
    m_wsService->start();
    m_amopClient->start();
    RPC_LOG(INFO) << LOG_DESC("start rpc successfully");
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

    RPC_LOG(INFO) << LOG_DESC("[RPC][RPC][stop]") << LOG_DESC("stop rpc successfully");
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
            auto wsMessageFactory =
                std::dynamic_pointer_cast<WsMessageFactory>(m_wsService->messageFactory());
            auto message = wsMessageFactory->buildMessage(bcos::protocol::MessageType::BLOCK_NOTIFY,
                std::make_shared<bcos::bytes>(resp.begin(), resp.end()));
            s->asyncSendMessage(message);
        }
    }

    if (_callback)
    {
        _callback(nullptr);
    }
    m_jsonRpcImpl->groupManager()->updateGroupBlockInfo(_groupID, _nodeName, _blockNumber);
    WEBSOCKET_SERVICE(TRACE) << LOG_BADGE("asyncNotifyBlockNumber") << LOG_KV("group", _groupID)
                             << LOG_KV("blockNumber", _blockNumber)
                             << LOG_KV("sessions", ss.size());
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
        auto wsMessageFactory = std::dynamic_pointer_cast<boostssl::ws::WsMessageFactory>(
            m_wsService->messageFactory());
        auto message = wsMessageFactory->buildMessage(bcos::protocol::MessageType::GROUP_NOTIFY,
            std::make_shared<bcos::bytes>(response.begin(), response.end()));
        session->asyncSendMessage(message);
    }
}

bool Rpc::negotiatedVersion(
    std::shared_ptr<boostssl::MessageFace> _msg, std::shared_ptr<boostssl::ws::WsSession> _session)
{
    auto seq = _msg->seq();
    HandshakeRequest handshakeRequest;
    auto ret = handshakeRequest.decode(*(_msg->payload()));
    if (!ret)
    {
        RPC_LOG(WARNING) << LOG_DESC(
                                "negotiatedVersion error for receive invalid handshake request")
                         << LOG_KV("seq", seq);
        return false;
    }
    // negotiated protocol Version
    auto const& protocolInfo = handshakeRequest.protocol();
    // negotiated failed
    if (protocolInfo.minVersion() > m_localProtocol->maxVersion() ||
        protocolInfo.maxVersion() < m_localProtocol->minVersion())
    {
        RPC_LOG(WARNING) << LOG_DESC("negotiatedVersion failed, disconnect the session")
                         << LOG_KV("peer", _session->endPoint())
                         << LOG_KV("minVersion", protocolInfo.minVersion())
                         << LOG_KV("maxVersion", protocolInfo.maxVersion())
                         << LOG_KV("supportMinVersion", m_localProtocol->minVersion())
                         << LOG_KV("supportMaxVersion", m_localProtocol->maxVersion())
                         << LOG_KV("seq", seq);
        _session->drop(WsError::UserDisconnect);
    }
    auto version = std::min(m_localProtocol->maxVersion(), protocolInfo.maxVersion());
    _session->setVersion(version);
    RPC_LOG(INFO) << LOG_DESC("negotiatedVersion success") << LOG_KV("peer", _session->endPoint())
                  << LOG_KV("minVersion", protocolInfo.minVersion())
                  << LOG_KV("maxVersion", protocolInfo.maxVersion())
                  << LOG_KV("supportMinVersion", m_localProtocol->minVersion())
                  << LOG_KV("supportMaxVersion", m_localProtocol->maxVersion())
                  << LOG_KV("negotiatedVersion", version) << LOG_KV("seq", seq);
    return true;
}

void Rpc::onRecvHandshakeRequest(
    std::shared_ptr<boostssl::MessageFace> _msg, std::shared_ptr<boostssl::ws::WsSession> _session)
{
    if (!negotiatedVersion(_msg, _session))
    {
        return;
    }
    auto self = std::weak_ptr<Rpc>(shared_from_this());

    // notify the handshakeResonse
    m_jsonRpcImpl->getGroupInfoList([_msg, _session, self](
                                        bcos::Error::Ptr _error, Json::Value& _groupListResponse) {
        if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
        {
            RPC_LOG(ERROR)
                << LOG_BADGE(
                       "onRecvHandshakeRequest and response failed for get groupInfoList failed")
                << LOG_KV("endpoint", _session ? _session->endPoint() : "unknown")
                << LOG_KV("errorCode", _error->errorCode())
                << LOG_KV("errorMessage", _error->errorMessage());
            return;
        }
        auto rpc = self.lock();
        if (!rpc)
        {
            return;
        }
        rpc->m_jsonRpcImpl->getGroupBlockNumber(
            [_groupListResponse, _session, _msg](bcos::Error::Ptr, Json::Value& _blockNumber) {
                Json::Value handshakeResponse;
                handshakeResponse["groupInfoList"] = _groupListResponse;
                handshakeResponse["groupBlockNumber"] = _blockNumber;
                handshakeResponse["protocolVersion"] = _session->version();
                Json::FastWriter writer;
                auto response = writer.write(handshakeResponse);

                _msg->setPayload(std::make_shared<bcos::bytes>(response.begin(), response.end()));
                _session->asyncSendMessage(_msg);
                RPC_LOG(INFO) << LOG_DESC("onRecvHandshakeRequest success")
                              << LOG_KV("version", _session->version())
                              << LOG_KV("endpoint", _session ? _session->endPoint() : "unknown")
                              << LOG_KV("handshakeResponse", response);
            });
    });
}