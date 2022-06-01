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
 * @file ServiceV2.h
 * @author: yujiechen
 * @date 2022-5-24
 */
#pragma once
#include "Service.h"
#include "router/RouterTableInterface.h"
namespace bcos
{
namespace gateway
{
class ServiceV2 : public Service
{
public:
    using Ptr = std::shared_ptr<ServiceV2>;
    ServiceV2(std::string const& _nodeID, std::shared_ptr<boostssl::ws::WsService> _wsService,
        RouterTableFactory::Ptr _routerTableFactory);
    ~ServiceV2() override {}

    void start() override;
    void stop() override;

    void asyncSendMessageByNodeID(P2pID p2pID, std::shared_ptr<P2PMessage> message,
        CallbackFuncWithSession callback,
        boostssl::ws::Options options = boostssl::ws::Options()) override;

    void onMessage(std::shared_ptr<P2PSession> _session, std::shared_ptr<P2PMessage> _msg) override;

    void sendRespMessageBySession(
        bytesConstRef _payload, P2PMessage::Ptr _p2pMessage, P2PSession::Ptr _p2pSession) override;

    void asyncBroadcastMessage(
        std::shared_ptr<P2PMessage> _message, boostssl::ws::Options options) override;
    bool isReachable(P2pID const& _nodeID) const override;

    // handlers called when the node is unreachable
    void registerUnreachableHandler(std::function<void(std::string)> _handler)
    {
        WriteGuard l(x_unreachableHandlers);
        m_unreachableHandlers.emplace_back(_handler);
    }

protected:
    // called when the nodes become unreachable
    void onP2PNodesUnreachable(std::set<std::string> const& _p2pNodeIDs)
    {
        std::vector<std::function<void(std::string)>> handlers;
        {
            ReadGuard l(x_unreachableHandlers);
            handlers = m_unreachableHandlers;
        }
        // TODO: async here
        for (auto const& node : _p2pNodeIDs)
        {
            for (auto const& it : m_unreachableHandlers)
            {
                it(node);
            }
        }
    }
    // router related
    virtual void onReceivePeersRouterTable(
        std::shared_ptr<P2PSession> _session, std::shared_ptr<P2PMessage> _message);
    virtual void joinRouterTable(
        std::string const& _generatedFrom, RouterTableInterface::Ptr _routerTable);
    virtual void onReceiveRouterTableRequest(
        std::shared_ptr<P2PSession> _session, std::shared_ptr<P2PMessage> _message);
    virtual void broadcastRouterSeq();
    virtual void onReceiveRouterSeq(
        std::shared_ptr<P2PSession> _session, std::shared_ptr<P2PMessage> _message);

    virtual void onNewSession(P2PSession::Ptr _session);
    virtual void onEraseSession(P2PSession::Ptr _session);
    bool tryToUpdateSeq(std::string const& _p2pNodeID, uint32_t _seq);
    bool eraseSeq(std::string const& _p2pNodeID);

    virtual void asyncSendMessageByNodeIDWithMsgForward(std::shared_ptr<P2PMessage> _message,
        CallbackFuncWithSession _callback,
        boostssl::ws::Options _options = boostssl::ws::Options());

    virtual void asyncBroadcastMessageWithoutForward(
        std::shared_ptr<P2PMessage> message, bcos::boostssl::ws::Options options);

protected:
    // for message forward
    std::shared_ptr<bcos::Timer> m_routerTimer;
    std::atomic<uint32_t> m_statusSeq{1};

    RouterTableFactory::Ptr m_routerTableFactory;
    RouterTableInterface::Ptr m_routerTable;

    std::map<std::string, uint32_t> m_node2Seq;
    mutable SharedMutex x_node2Seq;

    const int c_unreachableTTL = 10;

    // called when the given node unreachable
    std::vector<std::function<void(std::string)>> m_unreachableHandlers;
    mutable SharedMutex x_unreachableHandlers;
};
}  // namespace gateway
}  // namespace bcos