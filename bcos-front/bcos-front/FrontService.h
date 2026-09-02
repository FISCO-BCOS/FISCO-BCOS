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
#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-framework/gateway/GroupNodeInfo.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/IOServicePool.h>
#include <boost/asio.hpp>
#include <atomic>
#include <functional>
#include <utility>

namespace bcos::front
{
class FrontService : public FrontServiceInterface
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
     * @brief: send response
     * @param _id: the request id
     * @param _moduleID: moduleID
     * @param _nodeID: the receiver nodeID
     * @param _data: message
     * @return void
     */
    void asyncSendResponse(const std::string& _id, int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, ReceiveMsgFunc _receiveMsgCallback) override;

    task::Task<void> broadcastMessage(
        uint16_t type, int moduleID,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads) override;

    /**
     * @brief: (coroutine, zero-copy) send message to one node and await the module-level response.
     *         The payload rides as views that the caller keeps alive for the duration of the
     *         co_await; the coroutine resumes with the peer's response (or timeout / gateway
     *         failure). See FrontServiceInterface::sendMessageByNodeID for the contract.
     */
    task::Task<SendResult> sendMessageByNodeID(int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads,
        uint32_t _timeout) override;

    // FIB-185: dispatch the gateway broadcast onto a serial send queue (off the caller thread) so a
    // caller holding a lock (PBFT under m_mutex) is not coupled to gateway session-lock contention.
    // The owned payload is captured by the queued task -> the message body is never copied.
    void asyncBroadcastMessageByOwnedPayload(
        uint16_t type, int moduleID, bytesPointer payload) override;

    // FIB-185: dispatch the point-to-point gateway send onto the serial send queue (off the caller
    // thread), so sendViewChange / sendRecoverResponse run under m_mutex without contending the
    // gateway session lock. Point-to-point encodes the wire frame, so this is not zero-copy; the
    // owned payload is captured only to keep it alive across the deferred encode.
    void asyncSendMessageByNodeIDByOwnedPayload(
        int moduleID, bcos::crypto::NodeIDPtr nodeID, bytesPointer payload) override;

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
        bytesConstRef _data, bool isResponse, const ReceiveMsgFunc& _receiveMsgCallback);

    /**
     * @brief: handle message timeout
     * @param _error: boost error code
     * @param _uuid: message uuid
     * @return void
     */
    void onMessageTimeout(const boost::system::error_code& _error, bcos::crypto::NodeIDPtr _nodeID,
        const std::string& _uuid);

    bcos::crypto::NodeIDPtr nodeID() const;
    void setNodeID(bcos::crypto::NodeIDPtr _nodeID);
    std::string groupID() const;
    void setGroupID(const std::string& _groupID);

    std::shared_ptr<gateway::GatewayInterface> gatewayInterface();

    bcos::gateway::GroupNodeInfo::Ptr groupNodeInfo() const override;

    void setGatewayInterface(std::shared_ptr<gateway::GatewayInterface> _gatewayInterface);

    std::shared_ptr<boost::asio::io_context> ioService() const;
    void setIoService(std::shared_ptr<boost::asio::io_context> _ioService);
    void setIOServicePool(bcos::IOServicePool::Ptr _ioServicePool);

    // register message _dispatcher for module
    void registerModuleMessageDispatcher(int _moduleID,
        std::function<void(bcos::crypto::NodeIDPtr, const std::string&, bytesConstRef)>
            _dispatcher);

    // only for ut
    std::unordered_map<int,
        std::function<void(bcos::crypto::NodeIDPtr, const std::string&, bytesConstRef)>>
    moduleID2MessageDispatcher() const;

    // only for ut
    std::unordered_map<int, std::function<void(bcos::gateway::GroupNodeInfo::Ptr, ReceiveMsgFunc)>>
    module2GroupNodeInfoNotifier() const;
    // register nodeIDs _dispatcher for module
    void registerGroupNodeInfoNotification(int _moduleID,
        std::function<void(
            bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo, ReceiveMsgFunc _receiveMsgCallback)>
            _dispatcher);

    bcos::protocol::ProtocolInfo::ConstPtr getLocalProtocolInfo() const;

    struct Callback : public std::enable_shared_from_this<Callback>
    {
        using Ptr = std::shared_ptr<Callback>;
        uint64_t startTime = utcSteadyTime();
        CallbackFunc callbackFunc;
        std::shared_ptr<boost::asio::steady_timer> timeoutHandler;
    };
    // lock m_callback
    mutable bcos::Mutex x_callback;
    // uuid to callback
    std::unordered_map<std::string, Callback::Ptr> m_callback;

    // only for ut
    std::unordered_map<std::string, Callback::Ptr> callback() const;

    Callback::Ptr getAndRemoveCallback(const std::string& _uuid);

    void addCallback(const std::string& _uuid, Callback::Ptr callback);

protected:
    virtual void handleCallback(bcos::Error::Ptr _error, bytesConstRef _payLoad,
        std::string const& _uuid, int _moduleID, bcos::crypto::NodeIDPtr _nodeID);
    void notifyGroupNodeInfo(
        const std::string& _groupID, const bcos::gateway::GroupNodeInfo::Ptr& _groupNodeInfo);

    virtual void protocolNegotiate(bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo);

    // FIB-185: hand a send task to the serial send strand and return immediately; tasks run FIFO
    // on the shared IOServicePool (serialized, never concurrently), so no caller thread runs the
    // gateway send.
    void enqueueSend(std::function<void()> _sendTask);

    // shared uuid/timer/callback registration used by both asyncSendMessageByNodeID and the
    // coroutine sendMessageByNodeID, so the uuid/timer/addCallback logic is not duplicated.
    // Returns the generated uuid (used as the message id for the module-level response routing).
    std::string registerCallback(
        bcos::crypto::NodeIDPtr _nodeID, uint32_t _timeout, CallbackFunc _callbackFunc);

private:
    bcos::IOServicePool::Ptr m_ioServicePool;
    // FIB-185: serial async send strand over the shared IOServicePool + a pending-send counter
    // that bounds it. The strand provides the FIFO/serialized execution the fix needs (off the
    // CALLER's thread, no new thread) without the hand-rolled queue + single-drainer CAS. It
    // exposes no queue depth, so the backpressure below (warn, then shed at the hard cap — the OOM
    // guard FIB-185 closes) tracks the pending count here instead. See enqueueSend().
    std::unique_ptr<bcos::Strand> m_sendStrand;
    std::atomic<size_t> m_pendingSendCount{0};
    // timer
    std::shared_ptr<boost::asio::io_context> m_ioService;
    /// gateway interface
    std::shared_ptr<bcos::gateway::GatewayInterface> m_gatewayInterface;

    std::unordered_map<int,
        std::function<void(bcos::crypto::NodeIDPtr, const std::string&, bytesConstRef)>>
        m_moduleID2MessageDispatcher;

    std::unordered_map<int, std::function<void(bcos::gateway::GroupNodeInfo::Ptr, ReceiveMsgFunc)>>
        m_module2GroupNodeInfoNotifier;

    // service is running or not. Atomic because the FIB-185 send path reads it from the caller's
    // thread (enqueueSend's post-stop guard) while stop() clears it from the shutdown thread, and
    // the drain-at-stop reasoning depends on that store being visible.
    std::atomic_bool m_run = false;
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
