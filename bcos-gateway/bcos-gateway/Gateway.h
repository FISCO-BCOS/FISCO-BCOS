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
 * @file Gateway.h
 * @author: octopus
 * @date 2021-04-19
 */

#pragma once

#include "bcos-gateway/libratelimit/GatewayRateLimiter.h"
#include "bcos-utilities/ObjectAllocatorMonitor.h"
#include "filter/ReadOnlyFilter.h"
#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-gateway/Common.h>
#include <bcos-gateway/GatewayConfig.h>
#include <bcos-gateway/gateway/GatewayNodeManager.h>
#include <bcos-gateway/libamop/AMOPImpl.h>
#include <bcos-gateway/libp2p/Service.h>
#include <bcos-gateway/libratelimit/RateLimiterManager.h>
#include <bcos-gateway/libratelimit/RateLimiterStat.h>
#include <bcos-utilities/BoostLog.h>

namespace bcos
{
namespace gateway
{
class Retry : public std::enable_shared_from_this<Retry>, public ObjectCounter<Retry>
{
public:
    Retry(crypto::NodeIDPtr _srcNodeID, crypto::NodeIDPtr _dstNodeID,
        std::shared_ptr<P2PMessage> _p2pMessage, std::shared_ptr<P2PInterface> _p2pInterface,
        ErrorRespFunc _respFunc, int _moduleID)
      : m_srcNodeID(std::move(_srcNodeID)),
        m_dstNodeID(std::move(_dstNodeID)),
        m_p2pMessage(std::move(_p2pMessage)),
        m_p2pInterface(std::move(_p2pInterface)),
        m_respFunc(std::move(_respFunc)),
        m_moduleID(_moduleID)
    {}
    // random choose one p2pID to send message
    P2pID chooseP2pID()
    {
        auto p2pId = P2pID();
        std::lock_guard<std::mutex> lock(x_mutex);
        if (!m_p2pIDs.empty())
        {
            p2pId = *m_p2pIDs.begin();
            m_p2pIDs.erase(m_p2pIDs.begin());
        }

        return p2pId;
    }

    // send the message with retry
    void trySendMessage()
    {
        if (m_p2pIDs.empty())
        {
            GATEWAY_LOG(DEBUG) << LOG_DESC("[Gateway::Retry]")
                               << LOG_DESC("unable to send the message")
                               << LOG_KV("srcNodeID", m_srcNodeID->hex())
                               << LOG_KV("dstNodeID", m_dstNodeID->hex())
                               << LOG_KV("seq", std::to_string(m_p2pMessage->seq()))
                               << LOG_KV("moduleID", m_moduleID);

            if (m_respFunc)
            {
                auto errorPtr = BCOS_ERROR_PTR(bcos::protocol::CommonError::GatewaySendMsgFailed,
                    "unable to send the message");
                m_respFunc(errorPtr);
            }
            return;
        }

        auto seq = m_p2pMessage->seq();
        auto p2pID = chooseP2pID();
        auto self = shared_from_this();
        auto startT = utcTime();
        auto callback = [moduleID = m_moduleID, seq, self, startT, p2pID](NetworkException e,
                            std::shared_ptr<P2PSession> session,
                            std::shared_ptr<P2PMessage> message) {
            std::ignore = session;
            if (e.errorCode() != P2PExceptionType::Success)
            {
                // bandwidth overflow , do'not try again
                if (e.errorCode() == P2PExceptionType::OutBWOverflow)
                {
                    if (self->m_respFunc)
                    {
                        auto errorPtr = BCOS_ERROR_PTR(
                            bcos::protocol::CommonError::GatewayBandwidthOverFlow, e.what());
                        self->m_respFunc(errorPtr);
                    }

                    return;
                }

                // QPS overflow , do'not try again ???
                if (e.errorCode() == P2PExceptionType::InQPSOverflow)
                {
                    if (self->m_respFunc)
                    {
                        auto errorPtr = BCOS_ERROR_PTR(
                            bcos::protocol::CommonError::GatewayQPSOverFlow, e.what());
                        self->m_respFunc(errorPtr);
                    }

                    return;
                }

                GATEWAY_LOG(DEBUG)
                    << LOG_BADGE("Retry") << LOG_DESC("network callback") << LOG_KV("seq", seq)
                    << LOG_KV("dstP2P", p2pID) << LOG_KV("code", e.errorCode())
                    << LOG_KV("moduleID", moduleID) << LOG_KV("message", e.what())
                    << LOG_KV("timeCost", (utcTime() - startT));
                // try again
                self->trySendMessage();
                return;
            }

            try
            {
                auto payload = message->payload();
                int respCode =
                    boost::lexical_cast<int>(std::string(payload->begin(), payload->end()));
                // the peer gateway not response not ok ,it means the gateway not dispatch the
                // message successfully,find another gateway and try again
                if (respCode != bcos::protocol::CommonError::SUCCESS)
                {
                    GATEWAY_LOG(DEBUG) << LOG_BADGE("Retry") << LOG_KV("p2pid", p2pID)
                                       << LOG_KV("moduleID", moduleID) << LOG_KV("code", respCode)
                                       << LOG_KV("message", e.what());
                    // try again
                    self->trySendMessage();
                    return;
                }
                GATEWAY_LOG(TRACE)
                    << LOG_BADGE("Retry: asyncSendMessageByNodeID success")
                    << LOG_KV("dstP2P", p2pID) << LOG_KV("srcNodeID", self->m_srcNodeID->hex())
                    << LOG_KV("dstNodeID", self->m_dstNodeID->hex())
                    << LOG_KV("moduleID", moduleID);
                // send message successfully
                if (self->m_respFunc)
                {
                    self->m_respFunc(nullptr);
                }
                return;
            }
            catch (const std::exception& e)
            {
                GATEWAY_LOG(ERROR) << LOG_BADGE("trySendMessage and receive response exception")
                                   << LOG_KV("payload", std::string(message->payload()->begin(),
                                                            message->payload()->end()))
                                   << LOG_KV("packetType", message->packetType())
                                   << LOG_KV("src", message->options() ?
                                                        toHex(*(message->options()->srcNodeID())) :
                                                        "unknown")
                                   << LOG_KV("size", message->length())
                                   << LOG_KV("message", e.what()) << LOG_KV("moduleID", moduleID);

                self->trySendMessage();
            }
        };
        m_p2pInterface->asyncSendMessageByNodeID(p2pID, m_p2pMessage, callback, Options(10000));
    }

    // insert p2pIDs
    void insertP2pIDs(RANGES::range auto const& _p2pIDs)
    {
        std::lock_guard<std::mutex> lock(x_mutex);
        m_p2pIDs.insert(m_p2pIDs.end(), _p2pIDs.begin(), _p2pIDs.end());
    }

private:
    // mutex for p2pIDs
    mutable std::mutex x_mutex;
    std::vector<P2pID> m_p2pIDs;
    crypto::NodeIDPtr m_srcNodeID;
    crypto::NodeIDPtr m_dstNodeID;
    std::shared_ptr<P2PMessage> m_p2pMessage;
    std::shared_ptr<P2PInterface> m_p2pInterface;
    ErrorRespFunc m_respFunc;
    int m_moduleID;
};

class Gateway : public GatewayInterface, public std::enable_shared_from_this<Gateway>
{
public:
    using Ptr = std::shared_ptr<Gateway>;
    Gateway(GatewayConfig::Ptr _gatewayConfig, P2PInterface::Ptr _p2pInterface,
        GatewayNodeManager::Ptr _gatewayNodeManager, bcos::amop::AMOPImpl::Ptr _amop,
        ratelimiter::GatewayRateLimiter::Ptr _gatewayRateLimiter,
        std::string _gatewayServiceName = "localGateway")
      : m_gatewayServiceName(_gatewayServiceName),
        m_gatewayConfig(_gatewayConfig),
        m_p2pInterface(_p2pInterface),
        m_gatewayNodeManager(_gatewayNodeManager),
        m_amop(_amop),
        m_gatewayRateLimiter(_gatewayRateLimiter)
    {
        m_p2pInterface->registerHandlerByMsgType(GatewayMessageType::PeerToPeerMessage,
            boost::bind(&Gateway::onReceiveP2PMessage, this, boost::placeholders::_1,
                boost::placeholders::_2, boost::placeholders::_3));

        m_p2pInterface->registerHandlerByMsgType(GatewayMessageType::BroadcastMessage,
            boost::bind(&Gateway::onReceiveBroadcastMessage, this, boost::placeholders::_1,
                boost::placeholders::_2, boost::placeholders::_3));
    }
    ~Gateway() override { stop(); }

    void start() override;
    void stop() override;

    /**
     * @brief: get connected peers
     * @return void
     */
    void asyncGetPeers(
        std::function<void(Error::Ptr, GatewayInfo::Ptr, GatewayInfosPtr)> _onGetPeers) override;
    /**
     * @brief: get nodeIDs from gateway
     * @param _groupID:
     * @param _onGetGroupNodeInfo: get nodeIDs callback
     * @return void
     */
    void asyncGetGroupNodeInfo(
        const std::string& _groupID, GetGroupNodeInfoFunc _onGetGroupNodeInfo) override;
    /**
     * @brief: send message
     * @param _groupID: groupID
     * @param _moduleID: moduleID
     * @param _srcNodeID: the sender nodeID
     * @param _dstNodeID: the receiver nodeID
     * @param _payload: message payload
     * @param _errorRespFunc: error func
     * @return void
     */
    void asyncSendMessageByNodeID(const std::string& _groupID, int _moduleID,
        bcos::crypto::NodeIDPtr _srcNodeID, bcos::crypto::NodeIDPtr _dstNodeID,
        bytesConstRef _payload, ErrorRespFunc _errorRespFunc) override;

    /**
     * @brief: send message to multiple nodes
     * @param _groupID: groupID
     * @param _moduleID: moduleID
     * @param _srcNodeID: the sender nodeID
     * @param _nodeIDs: the receiver nodeIDs
     * @param _payload: message payload
     * @return void
     */
    void asyncSendMessageByNodeIDs(const std::string& _groupID, int _moduleID,
        bcos::crypto::NodeIDPtr _srcNodeID, const bcos::crypto::NodeIDs& _nodeIDs,
        bytesConstRef _payload) override;

    /**
     * @brief: send broadcast message
     * @param _groupID: groupID
     * @param _moduleID: moduleID
     * @param _srcNodeID: the sender nodeID
     * @param _payload: message payload
     * @return void
     */
    void asyncSendBroadcastMessage(uint16_t _type, const std::string& _groupID, int _moduleID,
        bcos::crypto::NodeIDPtr _srcNodeID, bytesConstRef _payload) override;

    /**
     * @brief: receive p2p message
     * @param _groupID: groupID
     * @param _srcNodeID: the sender nodeID
     * @param _dstNodeID: the receiver nodeID
     * @param _payload: message content
     * @param _errorRespFunc: error func
     * @return void
     */
    virtual void onReceiveP2PMessage(const std::string& _groupID,
        bcos::crypto::NodeIDPtr _srcNodeID, bcos::crypto::NodeIDPtr _dstNodeID,
        bytesConstRef _payload, ErrorRespFunc _errorRespFunc = ErrorRespFunc());

    P2PInterface::Ptr p2pInterface() const { return m_p2pInterface; }
    GatewayNodeManager::Ptr gatewayNodeManager() { return m_gatewayNodeManager; }
    /**
     * @brief receive the latest group information notification from the GroupManagerInterface
     *
     * @param _groupInfo the latest group information
     */
    void asyncNotifyGroupInfo(
        bcos::group::GroupInfo::Ptr, std::function<void(Error::Ptr&&)>) override;

    /// for AMOP
    void asyncSendMessageByTopic(const std::string& _topic, bcos::bytesConstRef _data,
        std::function<void(bcos::Error::Ptr&&, int16_t, bytesPointer)> _respFunc) override
    {
        if (m_amop)
        {
            m_amop->asyncSendMessageByTopic(_topic, _data, std::move(_respFunc));
            return;
        }
        _respFunc(BCOS_ERROR_PTR(-1, "AMOP is not initialized"), 0, nullptr);
    }
    void asyncSendBroadcastMessageByTopic(
        const std::string& _topic, bcos::bytesConstRef _data) override
    {
        if (m_amop)
        {
            m_amop->asyncSendBroadcastMessageByTopic(_topic, _data);
        }
    }

    void asyncSubscribeTopic(std::string const& _clientID, std::string const& _topicInfo,
        std::function<void(Error::Ptr&&)> _callback) override
    {
        if (m_amop)
        {
            m_amop->asyncSubscribeTopic(_clientID, _topicInfo, std::move(_callback));
            return;
        }
        _callback(BCOS_ERROR_PTR(-1, "AMOP is not initialized"));
    }

    void asyncRemoveTopic(std::string const& _clientID, std::vector<std::string> const& _topicList,
        std::function<void(Error::Ptr&&)> _callback) override
    {
        if (m_amop)
        {
            m_amop->asyncRemoveTopic(_clientID, _topicList, std::move(_callback));
            return;
        }
        _callback(BCOS_ERROR_PTR(-1, "AMOP is not initialized"));
    }

    bcos::amop::AMOPImpl::Ptr amop() { return m_amop; }

    bool registerNode(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::protocol::NodeType _nodeType, bcos::front::FrontServiceInterface::Ptr _frontService,
        bcos::protocol::ProtocolInfo::ConstPtr _protocolInfo) override
    {
        return m_gatewayNodeManager->registerNode(
            _groupID, _nodeID, _nodeType, _frontService, _protocolInfo);
    }

    virtual bool unregisterNode(const std::string& _groupID, std::string const& _nodeID)
    {
        return m_gatewayNodeManager->unregisterNode(_groupID, _nodeID);
    }

    void enableReadOnlyMode();

protected:
    // for UT
    Gateway() {}
    virtual void onReceiveP2PMessage(
        NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg);

    /**
     * @brief: receive group broadcast message
     * @param _groupID: groupID
     * @param _srcNodeID: the sender nodeID
     * @param _payload: message content
     * @return void
     */
    virtual void onReceiveBroadcastMessage(
        NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg);


    bool checkGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo);

private:
    std::string m_gatewayServiceName;
    GatewayConfig::Ptr m_gatewayConfig;
    // p2p service interface
    P2PInterface::Ptr m_p2pInterface;
    // GatewayNodeManager
    GatewayNodeManager::Ptr m_gatewayNodeManager;
    bcos::amop::AMOPImpl::Ptr m_amop;

    // For rate limit
    ratelimiter::GatewayRateLimiter::Ptr m_gatewayRateLimiter;
    std::optional<ReadOnlyFilter> m_readonlyFilter;
};
}  // namespace gateway
}  // namespace bcos
