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
 * @brief AMOP client
 * @file AMOPClient.h
 * @author: yujiechen
 * @date 2021-10-28
 */
#pragma once
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-framework/interfaces/gateway/GatewayInterface.h>
#include <bcos-framework/interfaces/protocol/AMOPRequest.h>
#include <bcos-framework/interfaces/rpc/RPCInterface.h>
#include <bcos-utilities/Timer.h>
#include <tarscpp/servant/Application.h>

#define AMOP_CLIENT_LOG(level) BCOS_LOG(level) << LOG_BADGE("AMOPClient")
namespace bcos
{
namespace rpc
{
class AMOPClient : public std::enable_shared_from_this<AMOPClient>
{
public:
    using Ptr = std::shared_ptr<AMOPClient>;
    AMOPClient(std::shared_ptr<boostssl::ws::WsService> _wsService,
        std::shared_ptr<bcos::boostssl::ws::WsMessageFactory> _wsMessageFactory,
        std::shared_ptr<bcos::protocol::AMOPRequestFactory> _requestFactory,
        bcos::gateway::GatewayInterface::Ptr _gateway, std::string const& _gatewayServiceName)
      : m_wsService(_wsService),
        m_wsMessageFactory(_wsMessageFactory),
        m_requestFactory(_requestFactory),
        m_gateway(_gateway),
        m_gatewayServiceName(_gatewayServiceName)
    {
        initMsgHandler();
        // create gatewayStatusDetector to detect status of gateway periodically
        m_gatewayStatusDetector = std::make_shared<Timer>(5000, "gatewayDetector");
        m_gatewayStatusDetector->registerTimeoutHandler([this]() { pingGatewayAndNotifyTopics(); });
    }

    virtual ~AMOPClient() {}
    /**
     * @brief receive amop request message from the gateway
     *
     */
    virtual void asyncNotifyAMOPMessage(int16_t _type, std::string const& _topic,
        bytesConstRef _data, std::function<void(Error::Ptr&&, bytesPointer)> _callback)
    {
        try
        {
            switch (_type)
            {
            case AMOPNotifyMessageType::Unicast:
                asyncNotifyAMOPMessage(_topic, _data, _callback);
                break;
            case AMOPNotifyMessageType::Broadcast:
                asyncNotifyAMOPBroadcastMessage(_topic, _data, _callback);
                break;
            default:
                BCOS_LOG(WARNING) << LOG_DESC("asyncNotifyAMOPMessage: unknown message type")
                                  << LOG_KV("type", _type);
            }
        }
        catch (std::exception const& e)
        {
            BCOS_LOG(WARNING) << LOG_DESC("asyncNotifyAMOPMessage exception")
                              << LOG_KV("error", boost::diagnostic_information(e));
        }
    }

    void setClientID(std::string const& _clientID) { m_clientID = _clientID; }

    // start m_gatewayStatusDetector
    virtual void start()
    {
        if (m_gatewayStatusDetector)
        {
            auto activeEndPoints = getActiveGatewayEndPoints();
            if (activeEndPoints.size() == 0)
            {
                m_gatewayActivated.store(false);
            }
            m_gatewayStatusDetector->start();
        }
    }

    virtual void stop()
    {
        if (m_gatewayStatusDetector)
        {
            m_gatewayStatusDetector->stop();
        }
    }

    // the gateway notify the RPC client to subscribe topic if receive publish
    virtual void asyncNotifySubscribeTopic();

protected:
    /// for AMOP requests from SDK
    virtual void onRecvSubTopics(std::shared_ptr<boostssl::ws::WsMessage> _msg,
        std::shared_ptr<boostssl::ws::WsSession> _session);
    /**
     * @brief: receive amop request message from sdk
     */
    virtual void onRecvAMOPRequest(std::shared_ptr<boostssl::ws::WsMessage> _msg,
        std::shared_ptr<boostssl::ws::WsSession> _session);
    /**
     * @brief: receive amop broadcast message from sdk
     */
    virtual void onRecvAMOPBroadcast(std::shared_ptr<boostssl::ws::WsMessage> _msg,
        std::shared_ptr<boostssl::ws::WsSession> _session);

    std::shared_ptr<boostssl::ws::WsSession> randomChooseSession(std::string const& _topic);

    virtual void asyncNotifyAMOPMessage(std::string const& _topic, bytesConstRef _data,
        std::function<void(Error::Ptr&&, bytesPointer)> _callback);
    virtual void asyncNotifyAMOPBroadcastMessage(std::string const& _topic, bytesConstRef _data,
        std::function<void(Error::Ptr&&, bytesPointer)> _callback);

    std::map<std::string, std::shared_ptr<boostssl::ws::WsSession>> querySessionsByTopic(
        std::string const& _topic) const
    {
        ReadGuard l(x_topicToSessions);
        if (m_topicToSessions.count(_topic))
        {
            return m_topicToSessions.at(_topic);
        }
        return std::map<std::string, std::shared_ptr<boostssl::ws::WsSession>>();
    }

    void onClientDisconnect(std::shared_ptr<boostssl::ws::WsSession> _session);

    bool updateTopicInfos(
        std::string const& _topicInfo, std::shared_ptr<boostssl::ws::WsSession> _session);
    std::vector<tars::EndpointInfo> getActiveGatewayEndPoints();
    virtual bool gatewayInactivated();

    virtual void subscribeTopicToAllNodes();
    virtual void removeTopicFromAllNodes(std::vector<std::string> const& _topicName);
    std::string endPointToString(std::string const& _serviceName, TC_Endpoint const& _endPoint)
    {
        return _serviceName + "@tcp -h " + _endPoint.getHost() + " -p " +
               boost::lexical_cast<std::string>(_endPoint.getPort());
    }

    virtual void initMsgHandler();

    void sendMessageToClient(std::string const& _topic,
        std::shared_ptr<boostssl::ws::WsSession> _selectSession,
        std::shared_ptr<boostssl::ws::WsMessage> _msg,
        std::function<void(bcos::Error::Ptr&&, bytesPointer)> _callback);

    bool trySendAMOPRequestToLocalNode(std::shared_ptr<boostssl::ws::WsSession> _session,
        std::string const& _topic, std::shared_ptr<boostssl::ws::WsMessage> _msg);

    void broadcastAMOPMessage(
        std::string const& _topic, std::shared_ptr<boostssl::ws::WsMessage> _msg);

    virtual void pingGatewayAndNotifyTopics();

    virtual bool onGatewayInactivated(std::shared_ptr<boostssl::ws::WsMessage> _msg,
        std::shared_ptr<boostssl::ws::WsSession> _session);
    std::string generateTopicInfo();

protected:
    std::shared_ptr<boostssl::ws::WsService> m_wsService;
    std::shared_ptr<bcos::boostssl::ws::WsMessageFactory> m_wsMessageFactory;
    std::shared_ptr<bcos::protocol::AMOPRequestFactory> m_requestFactory;

    bcos::gateway::GatewayInterface::Ptr m_gateway;
    std::string m_clientID = "localAMOP";
    std::string m_gatewayServiceName;

    // for AMOP: [topic->[endpoint->session]]
    std::map<std::string, std::map<std::string, std::shared_ptr<boostssl::ws::WsSession>>>
        m_topicToSessions;
    mutable SharedMutex x_topicToSessions;

    std::shared_ptr<Timer> m_gatewayStatusDetector;
    std::atomic_bool m_gatewayActivated = {true};
};
}  // namespace rpc
}  // namespace bcos