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

#include <bcos-framework/interfaces/front/FrontServiceInterface.h>
#include <bcos-framework/interfaces/gateway/GatewayInterface.h>
#include <bcos-front/FrontMessage.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ThreadPool.h>
#include <boost/asio.hpp>

namespace bcos
{
namespace front
{
class FrontService : public FrontServiceInterface, public std::enable_shared_from_this<FrontService>
{
public:
    using Ptr = std::shared_ptr<FrontService>;

    FrontService();
    FrontService(const FrontService&) = delete;
    FrontService(FrontService&&) = delete;
    virtual ~FrontService();

    FrontService& operator=(const FrontService&) = delete;
    FrontService& operator=(FrontService&&) = delete;

public:
    void start() override;
    void stop() override;

    // check the startup parameters, if the required parameters are not set
    // properly, exception will be thrown
    void checkParams();

public:
    /**
     * @brief: get nodeIDs from frontservice
     * @param _getNodeIDsFunc: response callback
     * @return void
     */
    void asyncGetNodeIDs(GetNodeIDsFunc _getNodeIDsFunc) override;
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
    void onReceiveNodeIDs(const std::string& _groupID,
        std::shared_ptr<const crypto::NodeIDs> _nodeIDs,
        ReceiveMsgFunc _receiveMsgCallback) override;

    /**
     * @brief: receive message from gateway
     * @param _groupID: groupID
     * @param _nodeID: the node send the message
     * @param _data: received message data
     * @param _receiveMsgCallback: response callback
     * @return void
     */
    void onReceiveMessage(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
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

public:
    FrontMessageFactory::Ptr messageFactory() const { return m_messageFactory; }

    void setMessageFactory(FrontMessageFactory::Ptr _messageFactory)
    {
        m_messageFactory = _messageFactory;
    }

    bcos::crypto::NodeIDPtr nodeID() const { return m_nodeID; }
    void setNodeID(bcos::crypto::NodeIDPtr _nodeID) { m_nodeID = _nodeID; }
    std::string groupID() const { return m_groupID; }
    void setGroupID(const std::string& _groupID) { m_groupID = _groupID; }

    std::shared_ptr<gateway::GatewayInterface> gatewayInterface() { return m_gatewayInterface; }

    void setGatewayInterface(std::shared_ptr<gateway::GatewayInterface> _gatewayInterface)
    {
        m_gatewayInterface = _gatewayInterface;
    }

    std::shared_ptr<boost::asio::io_service> ioService() const { return m_ioService; }
    void setIoService(std::shared_ptr<boost::asio::io_service> _ioService)
    {
        m_ioService = _ioService;
    }

    bcos::ThreadPool::Ptr threadPool() const { return m_threadPool; }
    void setThreadPool(bcos::ThreadPool::Ptr _threadPool) { m_threadPool = _threadPool; }

    // register message _dispatcher for module
    void registerModuleMessageDispatcher(int _moduleID,
        std::function<void(
            bcos::crypto::NodeIDPtr _nodeID, const std::string& _id, bytesConstRef _data)>
            _dispatcher)
    {
        m_moduleID2MessageDispatcher[_moduleID] = _dispatcher;
    }

    // register nodeIDs _dispatcher for module
    void registerModuleNodeIDsDispatcher(int _moduleID,
        std::function<void(
            std::shared_ptr<const crypto::NodeIDs> _nodeIDs, ReceiveMsgFunc _receiveMsgCallback)>
            _dispatcher)
    {
        m_moduleID2NodeIDsDispatcher[_moduleID] = _dispatcher;
    }

public:
    struct Callback : public std::enable_shared_from_this<Callback>
    {
        using Ptr = std::shared_ptr<Callback>;
        uint64_t startTime = utcSteadyTime();
        CallbackFunc callbackFunc;
        std::shared_ptr<boost::asio::deadline_timer> timeoutHandler;
    };
    // lock m_callback
    mutable bcos::RecursiveMutex x_callback;
    // uuid to callback
    std::unordered_map<std::string, Callback::Ptr> m_callback;

    const std::unordered_map<std::string, Callback::Ptr>& callback() const { return m_callback; }

    const std::unordered_map<int, std::function<void(bcos::crypto::NodeIDPtr _nodeID,
                                      const std::string& _id, bytesConstRef _data)>>
    moduleID2MessageDispatcher() const
    {
        return m_moduleID2MessageDispatcher;
    }

    std::unordered_map<int, std::function<void(std::shared_ptr<const crypto::NodeIDs> _nodeIDs,
                                ReceiveMsgFunc _receiveMsgCallback)>>
    moduleID2NodeIDsDispatcher() const
    {
        return m_moduleID2NodeIDsDispatcher;
    }

    Callback::Ptr getAndRemoveCallback(const std::string& _uuid)
    {
        Callback::Ptr callback = nullptr;

        {
            RecursiveGuard l(x_callback);
            auto it = m_callback.find(_uuid);
            if (it != m_callback.end())
            {
                callback = it->second;
                m_callback.erase(it);
            }
        }

        return callback;
    }

    void addCallback(const std::string& _uuid, Callback::Ptr _callback)
    {
        RecursiveGuard l(x_callback);
        m_callback[_uuid] = _callback;
    }

protected:
    virtual void handleCallback(bcos::Error::Ptr _error, bytesConstRef _payLoad,
        std::string const& _uuid, int _moduleID, bcos::crypto::NodeIDPtr _nodeID);
    void notifyNodeIDs(
        const std::string& _groupID, std::shared_ptr<const crypto::NodeIDs> _nodeIDs);

private:
    // thread pool
    bcos::ThreadPool::Ptr m_threadPool;
    // timer
    std::shared_ptr<boost::asio::io_service> m_ioService;
    /// gateway interface
    std::shared_ptr<bcos::gateway::GatewayInterface> m_gatewayInterface;

    FrontMessageFactory::Ptr m_messageFactory;

    std::unordered_map<int, std::function<void(bcos::crypto::NodeIDPtr _nodeID,
                                const std::string& _id, bytesConstRef _data)>>
        m_moduleID2MessageDispatcher;

    std::unordered_map<int, std::function<void(std::shared_ptr<const crypto::NodeIDs> _nodeIDs,
                                ReceiveMsgFunc _receiveMsgCallback)>>
        m_moduleID2NodeIDsDispatcher;

    // service is running or not
    bool m_run = false;
    //
    std::shared_ptr<std::thread> m_frontServiceThread;
    // NodeID
    bcos::crypto::NodeIDPtr m_nodeID;
    // GroupID
    std::string m_groupID;
    // lock m_nodeID
    mutable bcos::Mutex x_nodeIDs;
    // lock notifyNodeIDs
    mutable bcos::Mutex x_notifierLock;
    // nodeIDs pushed by the gateway
    std::shared_ptr<const bcos::crypto::NodeIDs> m_nodeIDs;
};
}  // namespace front
}  // namespace bcos