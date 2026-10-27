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
 * @file Gateway.cpp
 * @author: octopus
 * @date 2021-04-19
 */

#include "bcos-gateway/Gateway.h"
#include "bcos-framework/protocol/CommonError.h"
#include "bcos-front/FrontMessage.h"
#include "bcos-gateway/Common.h"
#include "bcos-gateway/gateway/GatewayMessageExtAttributes.h"
#include "bcos-gateway/libp2p/Message.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/LogStream.h"
#include "filter/Filter.h"
#include <bcos-task/OneShot.h>
#include <bcos-task/Wait.h>
#include <boost/lexical_cast.hpp>
#include <cassert>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::gateway;
using namespace bcos::group;
using namespace bcos::amop;
using namespace bcos::crypto;

class Gateway::DispatchBudget
{
private:
    struct State
    {
        State(std::size_t _maxBytes, std::size_t _maxCount, InboundChannel::Poster _poster)
          : maxBytes(_maxBytes), maxCount(_maxCount), poster(std::move(_poster))
        {}

        std::size_t maxBytes;
        std::size_t maxCount;
        InboundChannel::Poster poster;
        std::mutex mutex;
        std::size_t usedBytes = 0;
        std::size_t usedCount = 0;
        bool closed = false;
        std::size_t waitBytes = 0;
        std::shared_ptr<task::OneShot<>> waiter;
    };

    static bool fitsLocked(State const& _state, std::size_t _bytes)
    {
        return _state.usedCount < _state.maxCount && _state.usedBytes <= _state.maxBytes &&
               _bytes <= _state.maxBytes - _state.usedBytes;
    }

    static void post(
        std::shared_ptr<State> const& _state, std::function<void()> const& _wake) noexcept
    {
        try
        {
            _state->poster(_wake);
        }
        catch (...)
        {
            try
            {
                _wake();
            }
            catch (...)
            {}
        }
    }

    static void release(std::shared_ptr<State> const& _state, std::size_t _bytes) noexcept
    {
        std::shared_ptr<task::OneShot<>> wake;
        {
            std::lock_guard lock(_state->mutex);
            --_state->usedCount;
            _state->usedBytes -= _bytes;
            if (_state->waiter && fitsLocked(*_state, _state->waitBytes))
            {
                wake = std::move(_state->waiter);
                _state->waitBytes = 0;
            }
        }
        if (wake)
        {
            post(_state, [wake] {
                try
                {
                    (void)wake->complete();
                }
                catch (...)
                {}
            });
        }
    }

public:
    class Permit
    {
    public:
        Permit() = default;
        Permit(std::shared_ptr<State> _state, std::size_t _bytes)
          : m_state(std::move(_state)), m_bytes(_bytes)
        {}
        Permit(const Permit&) = delete;
        Permit& operator=(const Permit&) = delete;
        Permit(Permit&& _other) noexcept
          : m_state(std::move(_other.m_state)), m_bytes(std::exchange(_other.m_bytes, 0))
        {}
        Permit& operator=(Permit&& _other) noexcept
        {
            if (this != &_other)
            {
                reset();
                m_state = std::move(_other.m_state);
                m_bytes = std::exchange(_other.m_bytes, 0);
            }
            return *this;
        }
        ~Permit() { reset(); }

        explicit operator bool() const { return static_cast<bool>(m_state); }
        void reset() noexcept
        {
            if (m_state)
            {
                release(m_state, m_bytes);
                m_state.reset();
                m_bytes = 0;
            }
        }

    private:
        std::shared_ptr<State> m_state;
        std::size_t m_bytes = 0;
    };

    DispatchBudget(std::size_t _maxBytes, std::size_t _maxCount, InboundChannel::Poster _poster)
      : m_state(std::make_shared<State>(_maxBytes, _maxCount, std::move(_poster)))
    {}
    ~DispatchBudget() { close(); }

    // Called by the single PeerToPeerMessage handler pump.
    task::Task<Permit> acquire(std::size_t _bytes)
    {
        auto state = m_state;
        if (_bytes > state->maxBytes || state->maxCount == 0)
        {
            co_return Permit{};
        }
        while (true)
        {
            std::shared_ptr<task::OneShot<>> wait;
            {
                std::lock_guard lock(state->mutex);
                if (state->closed)
                {
                    co_return Permit{};
                }
                if (fitsLocked(*state, _bytes))
                {
                    ++state->usedCount;
                    state->usedBytes += _bytes;
                    co_return Permit{state, _bytes};
                }
                assert(!state->waiter);
                state->waiter = std::make_shared<task::OneShot<>>();
                state->waitBytes = _bytes;
                wait = state->waiter;
            }
            try
            {
                (void)co_await *wait;
            }
            catch (...)
            {
                co_return Permit{};
            }
        }
    }

    bool closed() const
    {
        std::lock_guard lock(m_state->mutex);
        return m_state->closed;
    }

    void close()
    {
        auto state = m_state;
        std::shared_ptr<task::OneShot<>> wake;
        {
            std::lock_guard lock(state->mutex);
            if (state->closed)
            {
                return;
            }
            state->closed = true;
            wake = std::move(state->waiter);
            state->waitBytes = 0;
        }
        if (wake)
        {
            post(state, [wake] {
                try
                {
                    (void)wake->cancel();
                }
                catch (...)
                {}
            });
        }
    }

private:
    std::shared_ptr<State> m_state;
};

void Gateway::start()
{
    if (m_gatewayRateLimiter)
    {
        m_gatewayRateLimiter->start();
    }
    if (m_p2pInterface)
    {
        m_p2pInterface->start();
    }
    if (m_amop)
    {
        m_amop->start();
    }
    if (m_gatewayNodeManager)
    {
        m_gatewayNodeManager->start();
    }

    GATEWAY_LOG(INFO) << LOG_DESC("start end.");
}

void Gateway::stop()
{
    // A pump waiting for dispatch budget is not waiting on its channel, so wake it before
    // Service::stop() closes the channels and releases the handlers' captured owners.
    if (m_dispatchBudget)
    {
        m_dispatchBudget->close();
    }
    if (m_p2pInterface)
    {
        m_p2pInterface->stop();
    }
    if (m_amop)
    {
        m_amop->stop();
    }
    if (m_gatewayNodeManager)
    {
        m_gatewayNodeManager->stop();
    }
    if (m_gatewayRateLimiter)
    {
        m_gatewayRateLimiter->stop();
    }

    GATEWAY_LOG(INFO) << LOG_DESC("stop end.");
}

bcos::task::Task<std::tuple<Error::Ptr, GatewayInfo::Ptr, GatewayInfosPtr>> Gateway::getPeers()
{
    auto sessionInfos = m_p2pInterface->sessionInfos();
    auto peersNodeIDList = m_gatewayNodeManager->peersRouterTable()->getAllPeers();
    GatewayInfosPtr peerGatewayInfos = std::make_shared<GatewayInfos>();
    // append the peers sessionInfos
    for (auto const& info : sessionInfos)
    {
        auto gatewayInfo = std::make_shared<GatewayInfo>(info);
        auto nodeIDList = m_gatewayNodeManager->peersNodeIDList(info.rawP2pID);
        gatewayInfo->setNodeIDInfo(std::move(nodeIDList));
        peerGatewayInfos->emplace_back(gatewayInfo);
        peersNodeIDList.erase(info.rawP2pID);
    }
    // append peers that are not directly connected to nodeSelf
    for (auto const& peer : peersNodeIDList)
    {
        P2PInfo p2pInfo;
        p2pInfo.p2pID = peer;
        auto gatewayInfo = std::make_shared<GatewayInfo>(p2pInfo);
        auto nodeIDList = m_gatewayNodeManager->peersNodeIDList(peer);
        gatewayInfo->setNodeIDInfo(std::move(nodeIDList));
        peerGatewayInfos->emplace_back(gatewayInfo);
    }
    auto localP2pInfo = m_p2pInterface->localP2pInfo();
    auto localGatewayInfo = std::make_shared<GatewayInfo>(localP2pInfo);
    auto localNodeInfo = m_gatewayNodeManager->localRouterTable()->nodeListInfo();
    localGatewayInfo->setNodeIDInfo(std::move(localNodeInfo));
    co_return std::make_tuple(nullptr, std::move(localGatewayInfo), std::move(peerGatewayInfos));
}

/**
 * @brief: get nodeIDs from gateway
 * @param _groupID:
 * @return {error, groupNodeInfo}: error is nullptr on success
 */
bcos::task::Task<std::tuple<Error::Ptr, bcos::gateway::GroupNodeInfo::Ptr>>
Gateway::getGroupNodeInfo(const std::string& _groupID)
{
    auto groupNodeInfo = m_gatewayNodeManager->getGroupNodeInfoList(_groupID);
    co_return std::make_tuple(nullptr, std::move(groupNodeInfo));
}


bcos::task::Task<Error::Ptr> bcos::gateway::Gateway::sendMessageByNodeID(
    const std::string& _groupID, int _moduleID, bcos::crypto::NodeIDPtr _srcNodeID,
    bcos::crypto::NodeIDPtr _dstNodeID,
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads)
{
    auto p2pIDs =
        m_gatewayNodeManager->peersRouterTable()->queryP2pIDs(_groupID, _dstNodeID->hex());
    if (p2pIDs.empty())
    {
        // no remote gateway available: deliver locally if the destination front is local (the
        // views are joined because the local dispatch API consumes a single contiguous buffer)
        bcos::bytes buffer;
        for (auto const& data : _payloads)
        {
            buffer.insert(buffer.end(), data.begin(), data.end());
        }
        GATEWAY_LOG(DEBUG) << LOG_DESC("local delivery of sendMessageByNodeID")
                           << LOG_KV("groupID", _groupID) << LOG_KV("srcNodeID", _srcNodeID->hex())
                           << LOG_KV("dstNodeID", _dstNodeID->hex());
        // the joined buffer lives in this frame, so the view stays valid across the co_await
        co_return co_await m_gatewayNodeManager->localRouterTable()->sendMessage(
            _groupID, std::move(_srcNodeID), std::move(_dstNodeID), bcos::ref(buffer));
    }

    // zero-copy: the message (header/options only) and the payload views both live in this frame
    Message message;
    GatewayMessageExtAttributes msgExtAttr;
    msgExtAttr.setGroupID(_groupID);
    msgExtAttr.setModuleID(_moduleID);
    message.setPacketType(GatewayMessageType::PeerToPeerMessage);
    message.setSeq(m_p2pInterface->newSeq());
    message.setExtAttributes(std::move(msgExtAttr));

    P2PMessageOptions options;
    options.setGroupID(_groupID);
    options.setModuleID(_moduleID);
    options.setSrcNodeID(_srcNodeID->encode());
    options.mutableDstNodeIDs().push_back(_dstNodeID->encode());
    message.setOptions(std::move(options));

    // wait up to 10s for the peer gateway ack on each attempt (same as the previous Retry path)
    constexpr uint32_t c_gatewaySendTimeoutMs = 10000;
    for (auto const& p2pID : p2pIDs)
    {
        try
        {
            auto respMessage = co_await m_p2pInterface->sendMessageByNodeID(
                p2pID, message, _payloads, bcos::network::Options{c_gatewaySendTimeoutMs, true});
            if (!respMessage)
            {
                // No response means nothing was sent (e.g. the target session is inactive or the
                // nodeID is local). Treat it as a failed attempt and try the next gateway — the
                // previous Retry path never reported a missing response as success, and reporting
                // success here would silently drop the message.
                GATEWAY_LOG(DEBUG)
                    << LOG_BADGE("Gateway::sendMessageByNodeID")
                    << LOG_DESC("no response, try another gateway")
                    << LOG_KV("p2pid", printShortP2pID(p2pID)) << LOG_KV("moduleID", _moduleID);
                continue;
            }
            auto payload = respMessage->payload();
            int respCode = boost::lexical_cast<int>(
                std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()));
            if (respCode == bcos::protocol::CommonError::SUCCESS)
            {
                co_return nullptr;
            }
            if (respCode == bcos::protocol::CommonError::MessageDecodeFailed)
            {
                // a receiver-side decode rejection is a property of the bytes, not of the route:
                // re-sending the identical payload to another gateway cannot succeed, and
                // reporting GatewaySendMsgFailed here would misdirect the diagnosis. Return the
                // real code the destination front produced (Round-3 review finding).
                GATEWAY_LOG(DEBUG)
                    << LOG_BADGE("Gateway::sendMessageByNodeID")
                    << LOG_KV("p2pid", printShortP2pID(p2pID)) << LOG_KV("moduleID", _moduleID)
                    << LOG_KV("message",
                           "destination front failed to decode the message, "
                           "terminal");
                co_return BCOS_ERROR_PTR(bcos::protocol::CommonError::MessageDecodeFailed,
                    "the destination front failed to decode the message");
            }
            GATEWAY_LOG(DEBUG) << LOG_BADGE("Gateway::sendMessageByNodeID")
                               << LOG_KV("p2pid", printShortP2pID(p2pID))
                               << LOG_KV("moduleID", _moduleID) << LOG_KV("code", respCode)
                               << LOG_KV("message", "respCode != SUCCESS, try another gateway");
        }
        catch (bcos::network::NetworkException const& e)
        {
            if (bcos::network::errorCodeOf(e) == bcos::network::P2PExceptionType::OutBWOverflow)
            {
                co_return BCOS_ERROR_PTR(
                    bcos::protocol::CommonError::GatewayBandwidthOverFlow, e.what());
            }
            if (bcos::network::errorCodeOf(e) == bcos::network::P2PExceptionType::InQPSOverflow)
            {
                co_return BCOS_ERROR_PTR(bcos::protocol::CommonError::GatewayQPSOverFlow, e.what());
            }
            GATEWAY_LOG(DEBUG) << LOG_BADGE("Gateway::sendMessageByNodeID")
                               << LOG_DESC("network callback")
                               << LOG_KV("dstP2P", printShortP2pID(p2pID))
                               << LOG_KV("code", bcos::network::errorCodeOf(e))
                               << LOG_KV("moduleID", _moduleID) << LOG_KV("message", e.what());
            // try another gateway
        }
        catch (std::exception const& e)
        {
            GATEWAY_LOG(ERROR) << LOG_BADGE("Gateway::sendMessageByNodeID")
                               << LOG_DESC("send message exception")
                               << LOG_KV("moduleID", _moduleID)
                               << LOG_KV("message", boost::diagnostic_information(e));
            // try another gateway
        }
    }
    co_return BCOS_ERROR_PTR(
        bcos::protocol::CommonError::GatewaySendMsgFailed, "unable to send the message");
}

/**
 * @brief: receive p2p message from p2p network module
 * @param _groupID: groupID
 * @param _srcNodeID: the sender nodeID
 * @param _dstNodeID: the receiver nodeID
 * @param _payload: message content
 * @param _callback: callback
 * @return void
 */
bool Gateway::checkGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo)
{
    // check the serviceName
    auto nodeList = _groupInfo->nodeInfos();
    for (auto const& node : nodeList)
    {
        auto const& expectedGatewayService =
            node.second->serviceName(bcos::protocol::ServiceType::GATEWAY);
        if (expectedGatewayService != m_gatewayServiceName)
        {
            GATEWAY_LOG(INFO) << LOG_DESC(
                                     "unfollowed groupInfo for inconsistent gateway service name")
                              << LOG_KV("expected", expectedGatewayService)
                              << LOG_KV("selfName", m_gatewayServiceName);
            return false;
        }
    }
    return true;
}

bcos::task::Task<Error::Ptr> Gateway::notifyGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo)
{
    if (!checkGroupInfo(_groupInfo))
    {
        co_return nullptr;
    }
    m_gatewayNodeManager->updateFrontServiceInfo(_groupInfo);
    co_return nullptr;
}

task::Task<bcos::Error::Ptr> Gateway::dispatchToFrontService(
    const std::string& _groupID, NodeIDPtr _srcNodeID, NodeIDPtr _dstNodeID, Message _msg)
{
    auto frontService =
        m_gatewayNodeManager->localRouterTable()->getFrontService(_groupID, _dstNodeID);
    if (!frontService)
    {
        GATEWAY_LOG(ERROR) << LOG_DESC(
                                  "onReceiveP2PMessage unable to find front "
                                  "service to dispatch this message")
                           << LOG_KV("groupID", _groupID) << LOG_KV("srcNodeID", _srcNodeID->hex())
                           << LOG_KV("dstNodeID", _dstNodeID->hex());

        auto errorPtr = BCOS_ERROR_PTR(CommonError::NotFoundFrontServiceDispatchMsg,
            "unable to find front service dispatch message to "
            "groupID:" +
                _groupID + " ,nodeID:" + _dstNodeID->hex());
        GATEWAY_LOG(TRACE) << LOG_DESC("onReceiveP2PMessage callback")
                           << LOG_KV("groupID", _groupID) << LOG_KV("srcNodeID", _srcNodeID->hex())
                           << LOG_KV("dstNodeID", _dstNodeID->hex())
                           << LOG_KV("code", errorPtr->errorCode())
                           << LOG_KV("msg", errorPtr->errorMessage());
        co_return errorPtr;
    }

    // zero-copy: `_msg` is owned by this coroutine frame for the whole (possibly deferred)
    // dispatch, so the payload view into it stays valid until the co_await returns.
    auto error = co_await frontService->frontService()->onReceiveMessage(
        _groupID, _srcNodeID, _msg.payload());
    GATEWAY_LOG(TRACE) << LOG_DESC("onReceiveP2PMessage callback") << LOG_KV("groupID", _groupID)
                       << LOG_KV("srcNodeID", _srcNodeID->hex())
                       << LOG_KV("dstNodeID", _dstNodeID->hex())
                       << LOG_KV("code", (error ? error->errorCode() : 0))
                       << LOG_KV("msg", (error ? error->errorMessage() : ""));
    co_return error;
}

void Gateway::onReceiveP2PMessage(const std::string& _groupID, NodeIDPtr _srcNodeID,
    NodeIDPtr _dstNodeID, Message _msg, ErrorRespFunc _errorRespFunc)
{
    // Thin synchronous adapter over the coroutine form: the message is moved into the frame, so the
    // deferred dispatch keeps its own copy of the payload (the old shape moved it into a detached
    // task). See dispatchToFrontService for the awaitable entry point.
    auto self = std::static_pointer_cast<Gateway>(shared_from_this());
    task::wait(
        [](std::shared_ptr<Gateway> _self, std::string _groupID, NodeIDPtr _srcNodeID,
            NodeIDPtr _dstNodeID, Message _msg, ErrorRespFunc _errorRespFunc) -> task::Task<void> {
            auto error = co_await _self->dispatchToFrontService(
                _groupID, _srcNodeID, _dstNodeID, std::move(_msg));
            if (_errorRespFunc)
            {
                _errorRespFunc(std::move(error));
            }
        }(self, _groupID, _srcNodeID, _dstNodeID, std::move(_msg), std::move(_errorRespFunc)));
}

task::Task<void> Gateway::onReceiveP2PMessage(P2PSession::Ptr _session, Message _msg)
{
    // COROUTINE: the whole dispatch plus the reply are awaited on this frame, so the response
    // payload rides as a VIEW into _msg instead of a copy. The registered channel handler detaches
    // one frame per message via task::wait (see the constructor): this body awaits remote I/O —
    // the front-service dispatch and the response write — which the node-wide per-type pump must
    // never wait on.
    const auto& options = _msg.options();
    auto msgPayload = _msg.payload();
    auto payload = bytesConstRef(msgPayload.data(), msgPayload.size());
    // groupID
    auto groupID = options.groupID();
    // moduleID
    auto moduleID = options.moduleID();

    // Notice: moduleID not set the previous version, try to decode from front message
    if (moduleID == 0)
    {
        moduleID = front::FrontMessage::tryDecodeModuleID(payload);
    }

    if (moduleID == 0)
    {
        GATEWAY_LOG(TRACE) << LOG_BADGE("onReceiveP2PMessage")
                           << LOG_DESC("front message module id not found")
                           << LOG_KV("groupID", groupID) << LOG_KV("moduleID", moduleID)
                           << LOG_KV("seq", _msg.seq()) << LOG_KV("payload size", payload.size());
    }

    // Readonly filter
    if (m_readonlyFilter && !filter(*m_readonlyFilter, groupID, moduleID, {}))
    {
        GATEWAY_LOG(WARNING) << "P2PMessage moduleID: " << moduleID << " filter by readOnlyFilter";
        co_return;  // Drop the message
    }

    if (m_gatewayRateLimiter)
    {
        // The moduleID is not obtained,
        auto result = ((moduleID == 0) ?
                           std::nullopt :
                           m_gatewayRateLimiter->checkInComing(groupID, moduleID, _msg.length()));
        if (result.has_value())
        {
            auto errorCode = std::to_string((int)protocol::CommonError::GatewayQPSOverFlow);
            // co_await: the rejection goes out on this frame. The bytes are moved into the
            // response, so the std::string above only has to live across the argument conversion.
            co_await m_p2pInterface->sendRespMessageBySession(
                bcos::bytes(errorCode.begin(), errorCode.end()), _msg.seq(), _msg.srcP2PNodeID(),
                _session);
            co_return;
        }
    }

    auto srcNodeID = options.srcNodeID();
    const auto& dstNodeIDs = options.dstNodeIDs();
    // FIB-183: a decoded Message may legitimately carry zero dstNodeIDs (the wire
    // format allows a src-nodeID count of 0). Indexing dstNodeIDs[0] in that case is an
    // out-of-bounds read on an empty vector. Drop the message before indexing.
    if (dstNodeIDs.empty())
    {
        GATEWAY_LOG(WARNING) << LOG_BADGE("onReceiveP2PMessage")
                             << LOG_DESC("empty dstNodeIDs, drop") << LOG_KV("groupID", groupID)
                             << LOG_KV("moduleID", moduleID) << LOG_KV("seq", _msg.seq());
        co_return;
    }
    auto srcNodeIDPtr = m_gatewayNodeManager->keyFactory()->createKey(srcNodeID);
    auto dstNodeIDPtr = m_gatewayNodeManager->keyFactory()->createKey(dstNodeIDs[0]);
    // the response only needs the request's seq and srcP2PNodeID
    auto seq = _msg.seq();
    auto srcP2PNodeID = _msg.srcP2PNodeID();

    // Await the front-service dispatch, then the reply, both on this frame. `_msg` is owned here
    // for the whole dispatch, so the payload view stays valid and the response can be sent straight
    // from it (the old shape detached a task, which forced a payload copy).
    auto error =
        co_await dispatchToFrontService(groupID, srcNodeIDPtr, dstNodeIDPtr, std::move(_msg));
    if (error)
    {
        GATEWAY_LOG(TRACE) << LOG_BADGE("onReceiveP2PMessage") << "callback failed"
                           << LOG_KV("code", error->errorCode())
                           << LOG_KV("message", error->errorMessage()) << LOG_KV("group", groupID)
                           << LOG_KV("moduleID", moduleID)
                           << LOG_KV("src", srcNodeIDPtr->shortHex())
                           << LOG_KV("dst", dstNodeIDPtr->shortHex());
    }
    auto errorCode =
        std::to_string(error ? error->errorCode() : (int)protocol::CommonError::SUCCESS);
    co_await m_p2pInterface->sendRespMessageBySession(
        bcos::bytes(errorCode.begin(), errorCode.end()), seq, std::move(srcP2PNodeID), _session);
}

void Gateway::onReceiveBroadcastMessage(P2PSession::Ptr _session, Message _msg)
{
    const auto& options = _msg.options();
    auto payload = _msg.payload();

    // groupID
    auto groupID = options.groupID();
    // moduleID
    uint16_t moduleID = options.moduleID();


    // Notice: moduleID not set the previous version, try to decode from front message
    if (moduleID == 0)
    {
        moduleID =
            front::FrontMessage::tryDecodeModuleID(bytesConstRef(payload.data(), payload.size()));
    }

    if (moduleID == 0)
    {
        GATEWAY_LOG(TRACE) << LOG_BADGE("onReceiveBroadcastMessage")
                           << LOG_DESC("front message module id not found")
                           << LOG_KV("groupID", groupID) << LOG_KV("moduleID", moduleID)
                           << LOG_KV("seq", _msg.seq()) << LOG_KV("payload size", payload.size());
    }

    // Readonly filter
    if (m_readonlyFilter && !filter(*m_readonlyFilter, groupID, moduleID, bytesConstRef{}))
    {
        GATEWAY_LOG(INFO) << "BroadcastMessage moduleID: " << moduleID
                          << " filter by readOnlyFilter";
        return;
    }

    if (m_gatewayRateLimiter)
    {
        auto result = ((moduleID == 0) ?
                           std::nullopt :
                           m_gatewayRateLimiter->checkInComing(groupID, moduleID, _msg.length()));
        if (result.has_value())
        {
            auto result = m_gatewayRateLimiter->checkInComing(groupID, moduleID, _msg.length());

            if (result)
            {
                // For broadcast message, ratelimit check failed, do nothing.
                return;
            }
        }
    }

    auto srcNodeIDPtr = m_gatewayNodeManager->keyFactory()->createKey((_msg.options().srcNodeID()));

    auto type = _msg.ext();
    // broadcastMsg fans out one dispatch task per local front service, each keeping the message
    // (payload) alive — genuine shared ownership, hence the shared_ptr hand-over
    m_gatewayNodeManager->localRouterTable()->broadcastMsg(
        type, groupID, moduleID, srcNodeIDPtr, std::make_shared<Message>(std::move(_msg)));
}

void bcos::gateway::Gateway::enableReadOnlyMode()
{
    m_readonlyFilter.emplace();
}

bcos::task::Task<void> bcos::gateway::Gateway::broadcastMessage(uint16_t type,
    std::string_view groupID, int moduleID, const bcos::crypto::NodeID& srcNodeID,
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads)
{
    // zero-copy: the message (header/options only) lives in this frame; payload rides as views
    Message message;
    message.setPacketType(GatewayMessageType::BroadcastMessage);
    message.setExt(type);
    message.setSeq(m_p2pInterface->newSeq());

    P2PMessageOptions options;
    options.setGroupID(std::string(groupID));
    options.setSrcNodeID(srcNodeID.encode());
    options.setModuleID(moduleID);
    message.setOptions(std::move(options));

    if (m_gatewayRateLimiter)
    {
        GatewayMessageExtAttributes msgExtAttr;
        msgExtAttr.setGroupID(std::string(groupID));
        msgExtAttr.setModuleID(moduleID);
        message.setExtAttributes(std::move(msgExtAttr));
    }

    co_await m_gatewayNodeManager->peersRouterTable()->broadcastMessage(
        type, groupID, moduleID, message, std::move(payloads));
}

static InboundChannel::Poster makeDispatchPoster(
    Service::Ptr _service, InboundChannel::Poster _overridePoster)
{
    if (_overridePoster)
    {
        return _overridePoster;
    }
    return [service = std::move(_service)](std::function<void()> wake) {
        if (service && service->host() && service->host()->haveNetwork())
        {
            service->host()->asioInterface()->post(std::move(wake));
        }
        else
        {
            wake();
        }
    };
}

bcos::gateway::Gateway::Gateway(GatewayConfig::Ptr _gatewayConfig, Service::Ptr _p2pInterface,
    GatewayNodeManager::Ptr _gatewayNodeManager, bcos::amop::AMOPImpl::Ptr _amop,
    ratelimiter::GatewayRateLimiter::Ptr _gatewayRateLimiter, std::string _gatewayServiceName,
    std::size_t _dispatchInFlightBytes, std::size_t _dispatchInFlightCount,
    InboundChannel::Poster _dispatchPoster)
  : m_gatewayServiceName(std::move(_gatewayServiceName)),
    m_gatewayConfig(std::move(_gatewayConfig)),
    m_p2pInterface(std::move(_p2pInterface)),
    m_gatewayNodeManager(std::move(_gatewayNodeManager)),
    m_amop(std::move(_amop)),
    m_gatewayRateLimiter(std::move(_gatewayRateLimiter)),
    m_dispatchBudget(std::make_shared<DispatchBudget>(_dispatchInFlightBytes,
        _dispatchInFlightCount, makeDispatchPoster(m_p2pInterface, std::move(_dispatchPoster))))
{
    m_p2pInterface->subscribeHandler(GatewayMessageType::PeerToPeerMessage,
        [this](bcos::gateway::InboundMessage&& inbound) -> task::Task<bool> {
            // The permit, not remote I/O, is the only operation this pump awaits. While permits
            // are available each message dispatches independently; once exhausted, the pump stops
            // popping and back-pressure builds in the inbound channel.
            const auto bytes = inboundMessageBytes(inbound);
            auto budget = m_dispatchBudget;
            std::weak_ptr<Gateway> weakSelf = shared_from_this();
            auto permit = co_await budget->acquire(bytes);
            if (!permit)
            {
                if (budget->closed() || weakSelf.expired())
                {
                    co_return false;
                }
                GATEWAY_LOG(ERROR)
                    << LOG_DESC("inbound message exceeds the channel byte budget, drop")
                    << LOG_KV("seq", inbound.message.seq()) << LOG_KV("bytes", bytes);
                co_return true;
            }
            auto self = weakSelf.lock();
            if (!self)
            {
                co_return false;
            }
            task::wait([](std::shared_ptr<Gateway> _self, P2PSession::Ptr _session, Message _msg,
                           DispatchBudget::Permit _permit) -> task::Task<void> {
                try
                {
                    co_await _self->onReceiveP2PMessage(std::move(_session), std::move(_msg));
                }
                catch (std::exception const& e)
                {
                    GATEWAY_LOG(WARNING) << LOG_BADGE("onReceiveP2PMessage")
                                         << LOG_DESC("detached dispatch exception")
                                         << LOG_KV("what", boost::diagnostic_information(e));
                }
                catch (...)
                {
                    GATEWAY_LOG(WARNING)
                        << LOG_BADGE("onReceiveP2PMessage")
                        << LOG_DESC("detached dispatch exception")
                        << LOG_KV("what", boost::current_exception_diagnostic_information());
                }
            }(self, std::move(inbound.session), std::move(inbound.message), std::move(permit)));
            co_return true;
        });
    m_p2pInterface->subscribeHandler(GatewayMessageType::BroadcastMessage,
        [this](bcos::gateway::InboundMessage&& inbound) -> task::Task<bool> {
            onReceiveBroadcastMessage(std::move(inbound.session), std::move(inbound.message));
            co_return true;
        });
}
bcos::gateway::Gateway::~Gateway()
{
    stop();
}
bcos::gateway::Service::Ptr bcos::gateway::Gateway::p2pInterface() const
{
    return m_p2pInterface;
}
bcos::gateway::GatewayNodeManager::Ptr bcos::gateway::Gateway::gatewayNodeManager()
{
    return m_gatewayNodeManager;
}
bcos::task::Task<std::tuple<bcos::Error::Ptr, int16_t, bcos::bytes>>
bcos::gateway::Gateway::sendMessageByTopic(const std::string& _topic, bcos::bytesConstRef _data)
{
    if (m_amop)
    {
        co_return co_await m_amop->sendMessageByTopic(_topic, _data);
    }
    co_return std::make_tuple(
        BCOS_ERROR_PTR(-1, "AMOP is not initialized"), (int16_t)0, bcos::bytes{});
}
bcos::task::Task<void> bcos::gateway::Gateway::sendBroadcastMessageByTopic(
    const std::string& _topic, bcos::bytesConstRef _data)
{
    if (m_amop)
    {
        co_await m_amop->sendBroadcastMessageByTopic(_topic, _data);
    }
    co_return;
}
bcos::task::Task<bcos::Error::Ptr> bcos::gateway::Gateway::subscribeTopic(
    std::string const& _clientID, std::string const& _topicInfo)
{
    if (m_amop)
    {
        co_return co_await m_amop->subscribeTopic(_clientID, _topicInfo);
    }
    co_return BCOS_ERROR_PTR(-1, "AMOP is not initialized");
}
bcos::task::Task<bcos::Error::Ptr> bcos::gateway::Gateway::removeTopic(
    std::string const& _clientID, std::vector<std::string> const& _topicList)
{
    if (m_amop)
    {
        co_return co_await m_amop->removeTopic(_clientID, _topicList);
    }
    co_return BCOS_ERROR_PTR(-1, "AMOP is not initialized");
}
bcos::amop::AMOPImpl::Ptr bcos::gateway::Gateway::amop()
{
    return m_amop;
}
bool bcos::gateway::Gateway::registerNode(const std::string& _groupID,
    bcos::crypto::NodeIDPtr _nodeID, bcos::protocol::NodeType _nodeType,
    bcos::front::FrontServiceInterface::Ptr _frontService,
    bcos::protocol::ProtocolInfo::ConstPtr _protocolInfo)
{
    return m_gatewayNodeManager->registerNode(
        _groupID, _nodeID, _nodeType, _frontService, _protocolInfo);
}
bool bcos::gateway::Gateway::unregisterNode(const std::string& _groupID, std::string const& _nodeID)
{
    return m_gatewayNodeManager->unregisterNode(_groupID, _nodeID);
}
