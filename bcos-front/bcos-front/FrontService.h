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
 * @file FrontService.h
 * @author: octopus
 * @date 2021-04-19
 */

#pragma once
#include "FrontMessage.h"
#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-framework/gateway/GroupNodeInfo.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ThreadPool.h>
#include <oneapi/tbb/task_group.h>
#include <boost/asio.hpp>
#include <utility>

namespace bcos::front
{
class FrontService : public FrontServiceInterface, public std::enable_shared_from_this<FrontService>
{
public:
    using Ptr = std::shared_ptr<FrontService>;

    FrontService();
    FrontService(const FrontService&) = delete;
    FrontService(FrontService&&) = delete;
    ~FrontService() noexcept override;

    FrontService& operator=(const FrontService&) = delete;
    FrontService& operator=(FrontService&&) = delete;

    void start() override;
    void stop() override;

    // check the startup parameters, if the required parameters are not set
    // properly, exception will be thrown
    void checkParams();

    /**
     * @brief: get nodeIDs from frontservice
     * @param _onGetGroupNodeInfoFunc: response callback
     * @return void
     */
    void asyncGetGroupNodeInfo(GetGroupNodeInfoFunc _onGetGroupNodeInfoFunc) override;
    /**
     * @brief: send message
     * @param _moduleID: moduleID
     * @param _nodeID: the receiver nodeID
     * @param _data: send message data
     * @param _timeout: timeout, in milliseconds.
     * @param _callbackFunc: callback
     * @return void
     */
    void asyncSendMessageByNodeID(int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, uint32_t _timeout, CallbackFunc _callbackFunc) override;

    /**
     * @brief: send response
     * @param _id: the request id
     * @param _moduleID: moduleID
     * @param _nodeID: the receiver nodeID
     * @param _data: message
     * @return void
     */
    void asyncSendResponse(const std::string& _id, int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, ReceiveMsgFunc _receiveMsgCallback) override;

    /**
     * @brief: send message to multiple nodes
     * @param _moduleID: moduleID
     * @param _nodeIDs: the receiver nodeIDs
     * @param _data: send message data
     * @return void
     */
    void asyncSendMessageByNodeIDs(
        int _moduleID, const crypto::NodeIDs& _nodeIDs, bytesConstRef _data) override;

    /**
     * @brief: send broadcast message
     * @param _moduleID: moduleID
     * @param _data: send message data
     * @return void
     */
    void asyncSendBroadcastMessage(uint16_t _type, int _moduleID, bytesConstRef _data) override;

    /**
     * @brief: receive nodeIDs from gateway
     * @param _groupID: groupID
     * @param _nodeIDs: nodeIDs pushed by gateway
     * @param _receiveMsgCallback: response callback
     * @return void
     */
    void onReceiveGroupNodeInfo(const std::string& _groupID,
        bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo,
        ReceiveMsgFunc _receiveMsgCallback) override;

    /**
     * @brief: receive message from gateway
     * @param _groupID: groupID
     * @param _nodeID: the node send the message
     * @param _data: received message data
     * @param _receiveMsgCallback: response callback
     * @return void
     */
    void onReceiveMessage(const std::string& _groupID, const bcos::crypto::NodeIDPtr& _nodeID,
        bytesConstRef _data, ReceiveMsgFunc _receiveMsgCallback) override;

    /**
     * @brief: receive broadcast message from gateway
     * @param _groupID: groupID
     * @param _nodeID: the node send the message
     * @param _data: received message data
     * @param _receiveMsgCallback: response callback
     * @return void
     */
    void onReceiveBroadcastMessage(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, ReceiveMsgFunc _receiveMsgCallback) override;

    /**
     * @brief: send message
     * @param _moduleID: moduleID
     * @param _nodeID: the node the message sent to
     * @param _uuid: uuid identify this message
     * @param _data: send data payload
     * @param isResponse: if send response message
     * @param _receiveMsgCallback: response callback
     * @return void
     */
    void sendMessage(int _moduleID, bcos::crypto::NodeIDPtr _nodeID, const std::string& _uuid,
        bytesConstRef _data, bool isResponse, ReceiveMsgFunc _receiveMsgCallback);

    /**
     * @brief: handle message timeout
     * @param _error: boost error code
     * @param _uuid: message uuid
     * @return void
     */
    void onMessageTimeout(const boost::system::error_code& _error, bcos::crypto::NodeIDPtr _nodeID,
        const std::string& _uuid);

    FrontMessageFactory::Ptr messageFactory() const { return m_messageFactory; }

    void setMessageFactory(FrontMessageFactory::Ptr _messageFactory)
    {
        m_messageFactory = std::move(_messageFactory);
    }

    bcos::crypto::NodeIDPtr nodeID() const { return m_nodeID; }
    void setNodeID(bcos::crypto::NodeIDPtr _nodeID) { m_nodeID = std::move(_nodeID); }
    std::string groupID() const { return m_groupID; }
    void setGroupID(const std::string& _groupID) { m_groupID = _groupID; }

    std::shared_ptr<gateway::GatewayInterface> gatewayInterface() { return m_gatewayInterface; }

    bcos::gateway::GroupNodeInfo::Ptr groupNodeInfo() const override
    {
        Guard guard(x_groupNodeInfo);
        return m_groupNodeInfo;
    }

    void setGatewayInterface(std::shared_ptr<gateway::GatewayInterface> _gatewayInterface)
    {
        m_gatewayInterface = std::move(_gatewayInterface);
    }

    std::shared_ptr<boost::asio::io_service> ioService() const { return m_ioService; }
    void setIoService(std::shared_ptr<boost::asio::io_service> _ioService)
    {
        m_ioService = std::move(_ioService);
    }

    // register message _dispatcher for module
    void registerModuleMessageDispatcher(int _moduleID,
        std::function<void(bcos::crypto::NodeIDPtr, const std::string&, bytesConstRef)> _dispatcher)
    {
        m_moduleID2MessageDispatcher[_moduleID] = std::move(_dispatcher);
    }

    // only for ut
    std::unordered_map<int,
        std::function<void(bcos::crypto::NodeIDPtr, const std::string&, bytesConstRef)>>
    moduleID2MessageDispatcher() const
    {
        return m_moduleID2MessageDispatcher;
    }

    // only for ut
    std::unordered_map<int, std::function<void(bcos::gateway::GroupNodeInfo::Ptr, ReceiveMsgFunc)>>
    module2GroupNodeInfoNotifier() const
    {
        return m_module2GroupNodeInfoNotifier;
    }
    // register nodeIDs _dispatcher for module
    void registerGroupNodeInfoNotification(int _moduleID,
        std::function<void(
            bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo, ReceiveMsgFunc _receiveMsgCallback)>
            _dispatcher)
    {
        m_module2GroupNodeInfoNotifier[_moduleID] = _dispatcher;
    }

    bcos::protocol::ProtocolInfo::ConstPtr getLocalProtocolInfo() const
    {
        auto ret = std::make_shared<bcos::protocol::ProtocolInfo>(*m_localProtocol);
        ret->setVersion(m_localProtocolVersion);
        return ret;
    }

    struct Callback : public std::enable_shared_from_this<Callback>
    {
        using Ptr = std::shared_ptr<Callback>;
        uint64_t startTime = utcSteadyTime();
        CallbackFunc callbackFunc;
        std::shared_ptr<boost::asio::deadline_timer> timeoutHandler;
    };
    // lock m_callback
    mutable bcos::Mutex x_callback;
    // uuid to callback
    std::unordered_map<std::string, Callback::Ptr> m_callback;

    // only for ut
    std::unordered_map<std::string, Callback::Ptr> callback() const { return m_callback; }

    Callback::Ptr getAndRemoveCallback(const std::string& _uuid)
    {
        Callback::Ptr callback = nullptr;

        {
            Guard guard(x_callback);
            auto it = m_callback.find(_uuid);
            if (it != m_callback.end())
            {
                callback = it->second;
                m_callback.erase(it);
            }
        }

        return callback;
    }

    void addCallback(const std::string& _uuid, Callback::Ptr callback)
    {
        Guard guard(x_callback);
        m_callback[_uuid] = std::move(callback);
    }

protected:
    virtual void handleCallback(bcos::Error::Ptr _error, bytesConstRef _payLoad,
        std::string const& _uuid, int _moduleID, bcos::crypto::NodeIDPtr _nodeID);
    void notifyGroupNodeInfo(
        const std::string& _groupID, bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo);

    virtual void protocolNegotiate(bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo);

private:
    tbb::task_group m_asyncGroup;
    // timer
    std::shared_ptr<boost::asio::io_service> m_ioService;
    /// gateway interface
    std::shared_ptr<bcos::gateway::GatewayInterface> m_gatewayInterface;
    FrontMessageFactory::Ptr m_messageFactory;

    std::unordered_map<int,
        std::function<void(bcos::crypto::NodeIDPtr, const std::string&, bytesConstRef)>>
        m_moduleID2MessageDispatcher;

    std::unordered_map<int, std::function<void(bcos::gateway::GroupNodeInfo::Ptr, ReceiveMsgFunc)>>
        m_module2GroupNodeInfoNotifier;

    // service is running or not
    bool m_run = false;
    //
    std::shared_ptr<std::thread> m_frontServiceThread;
    // NodeID
    bcos::crypto::NodeIDPtr m_nodeID;
    // GroupID
    std::string m_groupID;
    // lock notifyNodeIDs
    mutable bcos::Mutex x_notifierLock;

    // groupNodeInfo pushed by the gateway
    bcos::gateway::GroupNodeInfo::Ptr m_groupNodeInfo = nullptr;
    // lock m_nodeID
    mutable bcos::Mutex x_groupNodeInfo;

    // the local protocolInfo
    // Note: frontService is responsible for version negotiation of blockchain nodes
    bcos::protocol::ProtocolInfo::ConstPtr m_localProtocol;
    bcos::protocol::ProtocolVersion m_localProtocolVersion = {};
};
}  // namespace bcos::front
