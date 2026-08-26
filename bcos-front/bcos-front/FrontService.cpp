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
 * @file FrontService.cpp
 * @author: octopus
 * @date 2021-04-19
 */
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-front/Common.h>
#include <bcos-front/FrontMessage.h>
#include <bcos-front/FrontService.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <chrono>
#include <future>
#include <random>
#include <range/v3/view/concat.hpp>
#include <range/v3/view/single.hpp>
#include <utility>

namespace
{
template <class Task>
void dispatchTo(bcos::IOServicePool& ioServicePool, Task&& task)
{
    ioServicePool.dispatch(std::forward<Task>(task));
}
}  // namespace

using namespace bcos;
using namespace front;
using namespace protocol;

namespace
{
// FIB-185: warn (do not drop) when the pending send queue grows pathologically, e.g. when the
// gateway send is convoyed on the session lock under TLS-churn. Diagnostic only; below the hard cap
// no consensus message is dropped.
constexpr std::size_t c_maxPendingSendQueueWarnSize = 100000;
// FIB-185 (review): hard ceiling as a last-resort OOM guard. If the gateway send stays convoyed on
// the session lock under extreme churn the queue could otherwise grow without bound and exhaust
// memory (each entry pins an encoded consensus message). Above this cap a send is shed and an error
// is logged.
//
// NOTE — intentional behavior change from the pre-Strand code: the old tbb queue shed the OLDEST
// queued send (try_pop on the head). The bcos::Strand queue is opaque and cannot be reached into,
// so here the INCOMING (newest) send is shed instead. The memory bound is identical, but under
// sustained overload the node keeps draining older (staler) messages while dropping fresh ones;
// PBFT re-broadcast / the view-change path recover from any dropped message, so dropping is
// strictly better than OOM-killing the node. Set well above the warn size so shedding only happens
// in a genuine runaway, after the warning has fired.
constexpr std::size_t c_maxPendingSendQueueHardCap = 10 * c_maxPendingSendQueueWarnSize;
}  // namespace

FrontService::FrontService()
  : m_localProtocol(g_BCOSConfig.protocolInfo(ProtocolModuleID::NodeService))
{
    FRONT_LOG(INFO) << LOG_DESC("FrontService") << LOG_KV("this", this)
                    << LOG_KV("minVersion", m_localProtocol->minVersion())
                    << LOG_KV("maxVersion", m_localProtocol->maxVersion());
}

FrontService::~FrontService() noexcept
{
    stop();
    FRONT_LOG(INFO) << LOG_DESC("~FrontService") << LOG_KV("this", this);
}

bcos::crypto::NodeIDPtr FrontService::nodeID() const
{
    return m_nodeID;
}

void FrontService::setNodeID(bcos::crypto::NodeIDPtr _nodeID)
{
    m_nodeID = std::move(_nodeID);
}

std::string FrontService::groupID() const
{
    return m_groupID;
}

void FrontService::setGroupID(const std::string& _groupID)
{
    m_groupID = _groupID;
}

std::shared_ptr<gateway::GatewayInterface> FrontService::gatewayInterface()
{
    return m_gatewayInterface;
}

bcos::gateway::GroupNodeInfo::Ptr FrontService::groupNodeInfo() const
{
    Guard guard(x_groupNodeInfo);
    return m_groupNodeInfo;
}

void FrontService::setGatewayInterface(std::shared_ptr<gateway::GatewayInterface> _gatewayInterface)
{
    m_gatewayInterface = std::move(_gatewayInterface);
}

std::shared_ptr<boost::asio::io_context> FrontService::ioService() const
{
    return m_ioService;
}

void FrontService::setIoService(std::shared_ptr<boost::asio::io_context> _ioService)
{
    m_ioService = std::move(_ioService);
}

void FrontService::setIOServicePool(bcos::IOServicePool::Ptr _ioServicePool)
{
    m_ioServicePool = std::move(_ioServicePool);
    // FIB-185: (re)create the serial send strand over the pool the factory injects. This always
    // runs before start() (FrontServiceFactory calls it in buildFrontService), and enqueueSend()
    // only fires once m_run is true, so m_sendStrand is set before the first send.
    m_sendStrand = std::make_unique<bcos::Strand>(m_ioServicePool);
}

void FrontService::registerModuleMessageDispatcher(int _moduleID,
    std::function<void(bcos::crypto::NodeIDPtr, const std::string&, bytesConstRef)> _dispatcher)
{
    m_moduleID2MessageDispatcher[_moduleID] = std::move(_dispatcher);
}

std::unordered_map<int,
    std::function<void(bcos::crypto::NodeIDPtr, const std::string&, bytesConstRef)>>
FrontService::moduleID2MessageDispatcher() const
{
    return m_moduleID2MessageDispatcher;
}

std::unordered_map<int, std::function<void(bcos::gateway::GroupNodeInfo::Ptr, ReceiveMsgFunc)>>
FrontService::module2GroupNodeInfoNotifier() const
{
    return m_module2GroupNodeInfoNotifier;
}

void FrontService::registerGroupNodeInfoNotification(int _moduleID,
    std::function<void(
        bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo, ReceiveMsgFunc _receiveMsgCallback)>
        _dispatcher)
{
    Guard l(x_notifierLock);
    m_module2GroupNodeInfoNotifier[_moduleID] = _dispatcher;
}

bcos::protocol::ProtocolInfo::ConstPtr FrontService::getLocalProtocolInfo() const
{
    auto ret = std::make_shared<bcos::protocol::ProtocolInfo>(*m_localProtocol);
    ret->setVersion(m_localProtocolVersion);
    return ret;
}

std::unordered_map<std::string, FrontService::Callback::Ptr> FrontService::callback() const
{
    return m_callback;
}

FrontService::Callback::Ptr FrontService::getAndRemoveCallback(const std::string& _uuid)
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

void FrontService::addCallback(const std::string& _uuid, Callback::Ptr callback)
{
    Guard guard(x_callback);
    m_callback[_uuid] = std::move(callback);
}

// check the startup parameters, exception will be thrown if the required
// parameters are not set properly
void FrontService::checkParams()
{
    if (m_groupID.empty())
    {
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment(" FrontService groupID is uninitialized"));
    }

    if (!m_nodeID)
    {
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment(" FrontService nodeID is uninitialized"));
    }

    if (!m_gatewayInterface)
    {
        BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                  " FrontService gatewayInterface is uninitialized"));
    }

    if (!m_ioService)
    {
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment(" FrontService ioService is uninitialized"));
    }

    if (!m_ioServicePool)
    {
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment(" FrontService ioServicePool is uninitialized"));
    }
}

void FrontService::start()
{
    if (m_run)
    {
        FRONT_LOG(INFO) << LOG_BADGE("start") << LOG_DESC("front service is running")
                        << LOG_KV("nodeID", m_nodeID->hex()) << LOG_KV("groupID", m_groupID);
        return;
    }

    checkParams();

    m_run = true;

    // try to getNodeIDs from gateway
    auto self = std::weak_ptr<FrontService>(
        std::static_pointer_cast<FrontService>(shared_from_this()));
    m_gatewayInterface->asyncGetGroupNodeInfo(m_groupID,
        [self](const Error::Ptr& _error, const bcos::gateway::GroupNodeInfo::Ptr& _groupNodeInfo) {
            if (_error)
            {
                FRONT_LOG(ERROR) << LOG_BADGE("start") << LOG_DESC("asyncGetGroupNodeInfo failed")
                                 << LOG_KV("code", _error->errorCode())
                                 << LOG_KV("message", _error->errorMessage());
                return;
            }
            FRONT_LOG(INFO) << LOG_BADGE("start") << LOG_DESC("asyncGetGroupNodeInfo callback")
                            << LOG_KV("node size",
                                   _groupNodeInfo ? _groupNodeInfo->nodeIDList().size() : 0);
            auto frontService = self.lock();
            if (frontService)
            {
                frontService->onReceiveGroupNodeInfo(
                    frontService->groupID(), _groupNodeInfo, nullptr);
            }
        });

    FRONT_LOG(INFO) << LOG_DESC("start") << LOG_KV("nodeID", m_nodeID->hex())
                    << LOG_KV("groupID", m_groupID);

    FRONT_LOG(INFO) << LOG_DESC("register module")
                    << LOG_KV("count", m_moduleID2MessageDispatcher.size());
    for (const auto& module : m_moduleID2MessageDispatcher)
    {
        FRONT_LOG(INFO) << LOG_DESC("register module") << LOG_KV("moduleID", module.first);
    }
}
void FrontService::stop()
{
    if (!m_run)
    {
        return;
    }

    m_run = false;

    try
    {
        {
            Guard guard(x_callback);
            for (auto& callback : m_callback)
            {
                FRONT_LOG(INFO) << LOG_DESC("FrontService stopped, erase the callback")
                                << LOG_KV("uuid", callback.first);
                // cancel the timer
                if (callback.second->timeoutHandler)
                {
                    callback.second->timeoutHandler->cancel();
                }
            }
            // clear the callback
            m_callback.clear();
        }

        // FIB-185 (review): flush the serial send strand before returning. m_run is already false
        // (set above), so enqueueSend accepts no new sends. Post a FIFO barrier onto the strand and
        // wait for it: a bcos::Strand executes tasks in submission order, so once the barrier runs
        // every send posted before stop() has completed — the same guarantee the old
        // drainSendQueue()/drainer-slot wait provided (which itself replaced task_group.wait()).
        //
        // This must NOT run from a strand task (posting a barrier and waiting on it would
        // self-deadlock), and it requires the shared IOServicePool to still be running. Both hold
        // on the normal shutdown path: stop() is driven by the owning thread, never from a send
        // task, and the pool is owned outside the FrontService and outlives it (services are
        // stopped before the pool is torn down).
        //
        // Bound the wait rather than blocking forever: ~FrontService() -> stop() can be driven
        // from a pool worker thread (the IOServicePool dtor comments on exactly this — FrontService
        // destroyed by a temporary shared_ptr held in a completion handler). In that case the
        // round-robin dispatch may hand the barrier to this very thread's io_context, which is
        // blocked in wait() and can never run it — an unbounded wait would self-deadlock. So wait
        // at most 5s and log on timeout (same pattern as Worker::stop / SchedulerImpl shutdown):
        // pending sends may be dropped, but a bounded ERROR is strictly better than a hang.
        if (m_sendStrand)
        {
            auto flushed = std::make_shared<std::promise<void>>();
            m_sendStrand->post([flushed]() { flushed->set_value(); });
            if (flushed->get_future().wait_for(std::chrono::seconds(5)) !=
                std::future_status::ready)
            {
                FRONT_LOG(ERROR) << LOG_BADGE("stop")
                                 << LOG_DESC(
                                        "timed out flushing the send strand; "
                                        "pending sends may be dropped");
            }
        }
    }
    catch (const std::exception& e)
    {
        FRONT_LOG(ERROR) << LOG_DESC("FrontService stop")
                         << LOG_KV("failed", boost::diagnostic_information(e));
    }

    FRONT_LOG(INFO) << LOG_DESC("FrontService stop")
                    << LOG_KV("nodeID", (m_nodeID ? m_nodeID->hex() : ""))
                    << LOG_KV("groupID", m_groupID);
}

/**
 * @brief: get nodeIDs from frontservice
 * @param _onGetGroupNodeInfo: response callback
 * @return void
 */
void FrontService::asyncGetGroupNodeInfo(GetGroupNodeInfoFunc _onGetGroupNodeInfo)
{
    bcos::gateway::GroupNodeInfo::Ptr groupNodeInfo;
    {
        Guard guard(x_groupNodeInfo);
        groupNodeInfo = m_groupNodeInfo;
    }

    FRONT_LOG(DEBUG) << LOG_DESC("asyncGetGroupNodeInfo")
                     << LOG_KV("nodeIDs.size()",
                            (groupNodeInfo ? groupNodeInfo->nodeIDList().size() : 0));
    if (_onGetGroupNodeInfo)
    {
        dispatchTo(*m_ioServicePool, [_onGetGroupNodeInfo = std::move(_onGetGroupNodeInfo),
                                         groupNodeInfo = std::move(groupNodeInfo)]() mutable {
            _onGetGroupNodeInfo(nullptr, groupNodeInfo);
        });
    }
}

/**
 * @brief: send message
 * @param _moduleID: moduleID
 * @param _nodeID: the receiver nodeID
 * @param _data: send message data
 * @param _timeout: timeout, in milliseconds.
 * @param _callbackFunc: callback
 * @return void
 */
std::string FrontService::registerCallback(
    bcos::crypto::NodeIDPtr _nodeID, uint32_t _timeout, CallbackFunc _callbackFunc)
{
    static thread_local auto uuid_gen =
        boost::uuids::basic_random_generator<std::random_device>();
    std::string uuid = boost::uuids::to_string(uuid_gen());
    if (!_callbackFunc)
    {
        return uuid;
    }

    auto callback = std::make_shared<Callback>();
    callback->callbackFunc = std::move(_callbackFunc);

    if (_timeout > 0)
    {
        // create new timer to handle timeout
        auto timeoutHandler = std::make_shared<boost::asio::steady_timer>(
            *m_ioService, std::chrono::milliseconds(_timeout));

        callback->timeoutHandler = timeoutHandler;
        auto frontServiceWeakPtr = std::weak_ptr<FrontService>(
            std::static_pointer_cast<FrontService>(shared_from_this()));
        // callback->startTime = utcSteadyTime();
        timeoutHandler->async_wait(
            [frontServiceWeakPtr, _nodeID, uuid](const boost::system::error_code& e) {
                auto frontService = frontServiceWeakPtr.lock();
                if (frontService)
                {
                    frontService->onMessageTimeout(e, _nodeID, uuid);
                }
            });
    }

    addCallback(uuid, callback);
    return uuid;
}

/**
 * @brief: send response
 * @param _id: the request uuid
 * @param _data: message
 * @return void
 */
void FrontService::asyncSendResponse(const std::string& _id, int _moduleID,
    bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data, ReceiveMsgFunc _receiveMsgCallback)
{
    sendMessage(_moduleID, _nodeID, _id, _data, true, _receiveMsgCallback);
}

task::Task<void> FrontService::broadcastMessage(
    uint16_t type, int moduleID,
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads)
{
    FrontMessage message;
    message.setModuleID(moduleID);

    bytes header;
    message.encodeHeader(header);

    co_await m_gatewayInterface->broadcastMessage(type, m_groupID, moduleID, *m_nodeID,
        ::ranges::views::concat(
            ::ranges::views::single(bcos::ref(std::as_const(header))), std::move(payloads)));
}

void FrontService::asyncBroadcastMessageByOwnedPayload(
    uint16_t type, int moduleID, bytesPointer payload)
{
    // FIB-185: enqueue the gateway broadcast onto the serial send queue and return immediately, so
    // the caller (e.g. PBFT under m_mutex) never runs the gateway-session-lock-acquiring send on
    // its own thread. The owned payload is captured by the task -> the message body is not copied
    // (the gateway coroutine forwards it by reference from the shared_ptr).
    enqueueSend([this, type, moduleID, payload = std::move(payload)]() {
        FrontMessage message;
        message.setModuleID(moduleID);
        auto header = std::make_shared<bytes>();
        message.encodeHeader(*header);
        task::wait([](gateway::GatewayInterface::Ptr gateway, uint16_t msgType, std::string groupID,
                       int module, bcos::crypto::NodeIDPtr srcNodeID, std::shared_ptr<bytes> hdr,
                       bytesPointer body) -> task::Task<void> {
            co_await gateway->broadcastMessage(msgType, groupID, module, *srcNodeID,
                ::ranges::views::concat(::ranges::views::single(bcos::ref(*hdr)),
                    ::ranges::views::single(bcos::ref(*body))));
        }(m_gatewayInterface, type, m_groupID, moduleID, m_nodeID, header, payload));
    });
}

void FrontService::asyncSendMessageByNodeIDByOwnedPayload(
    int moduleID, bcos::crypto::NodeIDPtr nodeID, bytesPointer payload)
{
    // FIB-185: enqueue the point-to-point send onto the serial queue and return immediately, so the
    // caller (PBFT under m_mutex, via sendViewChange / sendRecoverResponse) never runs the
    // gateway-session-lock-acquiring send on its own thread. The owned payload is captured by the
    // launched coroutine -> the message body is sent as a view (zero-copy).
    enqueueSend([this, moduleID, nodeID = std::move(nodeID), payload = std::move(payload)]() {
        auto self = std::static_pointer_cast<FrontService>(shared_from_this());
        task::wait(
            [](FrontService::Ptr _self, int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
                bytesPointer _payload) -> task::Task<void> {
                // fire-and-forget owned-payload send: no module-level response is expected
                auto result = co_await _self->sendMessageByNodeID(_moduleID, _nodeID,
                    ::ranges::views::single(bcos::ref(*_payload)), 0);
                (void)result;
            }(self, moduleID, nodeID, payload));
    });
}

bcos::task::Task<SendResult> FrontService::sendMessageByNodeID(
    int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads, uint32_t _timeout)
{
    // keep the service alive for the whole (possibly deferred) send
    auto self = shared_from_this();
    auto state = std::make_shared<SendResponseAwaitable::State>();

    // Register the module-level response wait: the callback fires on the peer response (via the
    // front receive path, uuid-matched), on timeout, or on a gateway-level send failure. With
    // _timeout == 0 the callback is empty (registerCallback then only generates the uuid and
    // registers nothing) and the send is fire-and-forget.
    std::string uuid = registerCallback(_nodeID, _timeout,
        (_timeout > 0) ?
            CallbackFunc([state](Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
                bytesConstRef _data, const std::string& _uuid, ResponseFunc _resp) {
                SendResult result;
                result.error = std::move(_error);
                result.nodeID = std::move(_nodeID);
                result.payload.assign(_data.begin(), _data.end());
                result.uuid = _uuid;
                result.respond = std::move(_resp);
                SendResponseAwaitable::complete(state, std::move(result));
            }) :
            CallbackFunc());

    // zero-copy: only the FrontMessage header (moduleID + uuid + ext) is encoded into this frame;
    // the payload rides as views that the caller keeps alive for the duration of the co_await.
    FrontMessage message;
    message.setModuleID(_moduleID);
    message.setUuid(bytesConstRef(reinterpret_cast<const bcos::byte*>(uuid.data()), uuid.size()));
    bytes header;
    message.encodeHeader(header);

    auto nodeID = _nodeID;  // keep a copy for the gateway-error path below
    auto gatewayError = co_await m_gatewayInterface->sendMessageByNodeID(m_groupID, _moduleID,
        m_nodeID, std::move(_nodeID),
        ::ranges::views::concat(
            ::ranges::views::single(bcos::ref(std::as_const(header))), std::move(_payloads)));
    if (gatewayError && (gatewayError->errorCode() != CommonError::SUCCESS))
    {
        // complete the registered response wait with the gateway failure; this runs on the
        // coroutine's thread (self is alive in this frame), same delivery as the previous callback
        handleCallback(gatewayError, bytesConstRef(), uuid, _moduleID, nodeID);
    }

    if (_timeout == 0)
    {
        // fire-and-forget: no module-level response is expected; return once the gateway send
        // completes. Propagate the gateway failure (when any) so the TARS fire-and-forget reply
        // carries a truthful error code instead of always reporting SUCCESS. nodeID/uuid stay
        // default (the TARS server echoes the request nodeID/seq in that case).
        SendResult result;
        result.error = (gatewayError && gatewayError->errorCode() != CommonError::SUCCESS) ?
            gatewayError :
            nullptr;
        co_return result;
    }
    co_return co_await SendResponseAwaitable{std::move(state)};
}

void FrontService::enqueueSend(std::function<void()> _sendTask)
{
    // FIB-185 (review): once stopped, do not enqueue new sends. stop() flushes the strand; a task
    // enqueued after that would run behind the flush barrier and could outlive this FrontService.
    // New sends after stop are dropped on purpose (the node is shutting down). m_sendStrand is set
    // by setIOServicePool() before start(), so the guard below also covers a never-started service.
    if (!m_run || !m_sendStrand)
    {
        return;
    }

    // Backpressure / OOM guard: the bcos::Strand queue is opaque (no depth), so track the pending
    // count here. A gateway send convoyed on the session lock can otherwise grow the queue without
    // bound (each entry pins an encoded consensus message) and exhaust memory. Warn while still
    // unbounded; above the hard cap the INCOMING send is shed and an error is logged — an
    // intentional behavior change vs. the pre-Strand "shed the oldest" (the strand queue cannot be
    // reached into to shed the oldest, so the shed choice is "newest" rather than "oldest"). The
    // memory bound is identical and PBFT re-broadcast / the view-change path recover from any
    // dropped message. Dropping a message is strictly better than OOM-killing the node. Set well
    // above the warn size so shedding only happens in a genuine runaway.
    auto depth = m_pendingSendCount.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (depth > c_maxPendingSendQueueHardCap)
    {
        // undo the accounting for the dropped send; the strand never sees it
        m_pendingSendCount.fetch_sub(1, std::memory_order_acq_rel);
        FRONT_LOG(ERROR) << LOG_BADGE("enqueueSend")
                         << LOG_DESC(
                                "send queue hard cap exceeded; dropped incoming send to "
                                "avoid OOM")
                         << LOG_KV("pending", depth - 1)
                         << LOG_KV("hardCap", c_maxPendingSendQueueHardCap);
        return;
    }
    if (depth > c_maxPendingSendQueueWarnSize)
    {
        FRONT_LOG(WARNING) << LOG_BADGE("enqueueSend")
                           << LOG_DESC(
                                  "pending send queue unusually large; gateway send may be "
                                  "convoyed on the session lock")
                           << LOG_KV("pending", depth);
    }

    // Serialize the send onto the shared pool: the strand runs tasks FIFO and never concurrently,
    // on the pool's threads (the same threading model as the old single-drainer, minus the
    // hand-rolled CAS / lost-wakeup bookkeeping).
    //
    // PRECONDITION: FrontService must be owned by a shared_ptr (FrontServiceFactory is the only
    // construction site and uses make_shared); stack-allocating one would make weak_from_this()
    // empty and the send would never run.
    //
    // Hold the FrontService via weak_from_this, NOT a raw `this` and NOT shared_ptr: the shared
    // IOServicePool is owned outside and cannot be joined, so a posted task can outlive
    // ~FrontService and would touch destroyed members. Locking the weak_ptr keeps the object alive
    // for the send and no-ops once it is gone. (A shared_ptr capture would also form a cycle:
    // FrontService -> Strand -> queued task -> FrontService.)
    m_sendStrand->post([weak = weak_from_this(), task = std::move(_sendTask)]() mutable {
        if (auto self = std::static_pointer_cast<FrontService>(weak.lock()))
        {
            // this task no longer occupies queue space; decrement before running so the counter
            // reflects queued depth while a blocking gateway send is in flight
            self->m_pendingSendCount.fetch_sub(1, std::memory_order_acq_rel);
            task();
        }
        // else: FrontService is gone — its members (incl. m_pendingSendCount) are destroyed with
        // it, so there is nothing to account for.
    });
}

/**
 * @brief: receive nodeIDs from gateway
 * @param _groupID: groupID
 * @param _groupNodeInfo: nodeIDs pushed by gateway
 * @param _receiveMsgCallback: response callback
 * @return void
 */
void FrontService::onReceiveGroupNodeInfo(const std::string& _groupID,
    bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo, ReceiveMsgFunc _receiveMsgCallback)
{
    {
        protocolNegotiate(_groupNodeInfo);
        Guard guard(x_groupNodeInfo);
        m_groupNodeInfo = _groupNodeInfo;
    }
    // To be considered: How to ensure orderly notifications in the pro/max mode
    FRONT_LOG(INFO) << LOG_DESC("onReceiveGroupNodeInfo") << LOG_KV("groupID", _groupID)
                    << LOG_KV("nodeIDs.size()",
                           (_groupNodeInfo ? _groupNodeInfo->nodeIDList().size() : 0));

    auto self = weak_from_this();
    dispatchTo(
        *m_ioServicePool, [self, _groupID, _groupNodeInfo = std::move(_groupNodeInfo)]() mutable {
            if (auto frontService = std::static_pointer_cast<FrontService>(self.lock()))
            {
                frontService->notifyGroupNodeInfo(_groupID, _groupNodeInfo);
            }
        });

    if (_receiveMsgCallback)
    {
        _receiveMsgCallback(nullptr);
    }
}

void FrontService::protocolNegotiate(bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo)
{
    auto const& protocolList = _groupNodeInfo->nodeProtocolList();
    auto const& nodeIDList = _groupNodeInfo->nodeIDList();
    size_t i = 0;
    for (auto const& protocol : protocolList)
    {
        auto mutableProtocol = std::const_pointer_cast<ProtocolInfo>(protocol);
        // negotiate failed: can't happen unless the code has a bug
        if (mutableProtocol->minVersion() > m_localProtocol->maxVersion() ||
            mutableProtocol->maxVersion() < m_localProtocol->minVersion()) [[unlikely]]
        {
            FRONT_LOG(ERROR) << LOG_DESC("protocolNegotiate failed")
                             << LOG_KV("nodeID", nodeIDList.at(i))
                             << LOG_KV("groupID", _groupNodeInfo->groupID())
                             << LOG_KV("minVersion", mutableProtocol->minVersion())
                             << LOG_KV("maxVersion", mutableProtocol->maxVersion())
                             << LOG_KV("supportedMinVersion", m_localProtocol->minVersion())
                             << LOG_KV("supportedMaxVersion", m_localProtocol->maxVersion());
            mutableProtocol->setVersion(ProtocolVersion::V0);
            i++;
            continue;
        }
        // set the negotiated version
        auto version = std::min(m_localProtocol->maxVersion(), mutableProtocol->maxVersion());
        mutableProtocol->setVersion((ProtocolVersion)version);
        m_localProtocolVersion = (ProtocolVersion)version;
        FRONT_LOG(INFO) << LOG_DESC("protocolNegotiate success")
                        << LOG_KV("nodeID", nodeIDList.at(i))
                        << LOG_KV("groupID", _groupNodeInfo->groupID())
                        << LOG_KV("minVersion", mutableProtocol->minVersion())
                        << LOG_KV("maxVersion", mutableProtocol->maxVersion())
                        << LOG_KV("supportedMinVersion", m_localProtocol->minVersion())
                        << LOG_KV("supportedMaxVersion", m_localProtocol->maxVersion())
                        << LOG_KV("version", version);
        i++;
    }
}

void FrontService::notifyGroupNodeInfo(
    const std::string& _groupID, const bcos::gateway::GroupNodeInfo::Ptr& _groupNodeInfo)
{
    Guard l(x_notifierLock);
    for (const auto& entry : m_module2GroupNodeInfoNotifier)
    {
        auto moduleID = entry.first;
        entry.second(_groupNodeInfo, [_groupID, moduleID](const Error::Ptr& _error) {
            if (_error)
            {
                FRONT_LOG(ERROR) << LOG_DESC("onReceiveGroupNodeInfo dispather failed")
                                 << LOG_KV("groupID", _groupID) << LOG_KV("moduleID", moduleID);
            }
        });
    }
}

void FrontService::handleCallback(bcos::Error::Ptr _error, bytesConstRef _payLoad,
    std::string const& _uuid, int _moduleID, bcos::crypto::NodeIDPtr _nodeID)
{
    // callback message
    auto callback = getAndRemoveCallback(_uuid);
    if (!callback)
    {
        return;
    }
    auto frontServiceWeakPtr = std::weak_ptr<FrontService>(
        std::static_pointer_cast<FrontService>(shared_from_this()));
    auto respFunc = [frontServiceWeakPtr, _moduleID, _nodeID, _uuid](bytesConstRef _data) {
        auto frontService = frontServiceWeakPtr.lock();
        if (frontService)
        {
            frontService->sendMessage(
                _moduleID, _nodeID, _uuid, _data, true, [_uuid](const Error::Ptr& _error) {
                    if (_error && (_error->errorCode() != CommonError::SUCCESS))
                    {
                        FRONT_LOG(ERROR)
                            << LOG_BADGE("onReceiveMessage sendMessage callback")
                            << LOG_KV("uuid", _uuid) << LOG_KV("code", _error->errorCode())
                            << LOG_KV("message", _error->errorMessage());
                    }
                });
        }
    };
    // cancel the timer first
    if (callback->timeoutHandler)
    {
        callback->timeoutHandler->cancel();
    }

    // Copy the payload before dispatching asynchronously.
    auto buffer = bytes(_payLoad.begin(), _payLoad.end());
    dispatchTo(*m_ioServicePool, [_uuid, _error = std::move(_error), callback = std::move(callback),
                                     buffer = std::move(buffer), _nodeID = std::move(_nodeID),
                                     respFunc = std::move(respFunc)]() mutable {
        callback->callbackFunc(
            _error, _nodeID, bytesConstRef(buffer.data(), buffer.size()), _uuid, respFunc);
    });
}
/**
 * @brief: receive message from gateway
 * @param _groupID: groupID
 * @param _nodeID: the node send the message
 * @param _data: received message data
 * @param _receiveMsgCallback: response callback
 * @return void
 */
void FrontService::onReceiveMessage(const std::string& _groupID,
    const bcos::crypto::NodeIDPtr& _nodeID, bytesConstRef _data, ReceiveMsgFunc _receiveMsgCallback)
{
    try
    {
        FrontMessage message;
        auto ret = message.decode(_data);
        if (MessageDecodeStatus::MESSAGE_COMPLETE != ret)
        {
            FRONT_LOG(ERROR) << LOG_DESC("onReceiveMessage") << LOG_DESC("illegal message")
                             << LOG_KV("length", _data.size()) << LOG_KV("nodeID", m_nodeID->hex());
            BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment("illegal message"));
        }

        int moduleID = message.moduleID();
        int ext = message.ext();
        std::string uuid = std::string(message.uuid().begin(), message.uuid().end());

        FRONT_LOG(TRACE) << LOG_BADGE("onReceiveMessage") << LOG_KV("moduleID", moduleID)
                         << LOG_KV("uuid", uuid) << LOG_KV("ext", ext)
                         << LOG_KV("groupID", _groupID) << LOG_KV("nodeID", _nodeID->hex())
                         << LOG_KV("length", _data.size());

        if (message.isResponse())
        {
            handleCallback(nullptr, message.payload(), uuid, moduleID, _nodeID);
        }
        else
        {
            if (auto it = m_moduleID2MessageDispatcher.find(moduleID);
                it != m_moduleID2MessageDispatcher.end())
            {
                auto callback = it->second;
                // Copy the payload before dispatching asynchronously.
                bytes buffer(message.payload().begin(), message.payload().end());

                // dispatch to io service pool
                dispatchTo(*m_ioServicePool, [uuid, callback = std::move(callback),
                                                 buffer = std::move(buffer), _nodeID]() mutable {
                    callback(_nodeID, uuid, bytesConstRef(buffer.data(), buffer.size()));
                });
            }
            else
            {
                FRONT_LOG(WARNING) << LOG_DESC("unable find the register module message dispather")
                                   << LOG_KV("moduleID", moduleID) << LOG_KV("uuid", uuid);
            }
        }
    }
    catch (const std::exception& e)
    {
        FRONT_LOG(ERROR) << "onReceiveMessage"
                         << LOG_KV("failed", boost::diagnostic_information(e));
    }

    if (_receiveMsgCallback)
    {
        dispatchTo(
            *m_ioServicePool, [_receiveMsgCallback = std::move(_receiveMsgCallback)]() mutable {
                _receiveMsgCallback(nullptr);
            });
    }
}

/**
 * @brief: receive broadcast message from gateway
 * @param _groupID: groupID
 * @param _nodeID: the node send the message
 * @param _data: received message data
 * @param _receiveMsgCallback: response callback
 * @return void
 */
void FrontService::onReceiveBroadcastMessage(const std::string& _groupID,
    bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data, ReceiveMsgFunc _receiveMsgCallback)
{
    onReceiveMessage(_groupID, _nodeID, _data, _receiveMsgCallback);
}

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
void FrontService::sendMessage(int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
    const std::string& _uuid, bytesConstRef _data, bool isResponse,
    const ReceiveMsgFunc& _receiveMsgCallback)
{
    FrontMessage message;
    message.setModuleID(_moduleID);
    message.setUuid(bytesConstRef(reinterpret_cast<const bcos::byte*>(_uuid.data()), _uuid.size()));
    if (isResponse)
    {
        message.setResponse();
    }

    // header + payload must both outlive the non-blocking task::wait: copy them into owned
    // buffers held by the coroutine frame (sendMessage is the bridge from borrowed-callback APIs,
    // so it cannot preserve zero-copy)
    bytes header;
    message.encodeHeader(header);

    auto gateway = m_gatewayInterface;
    auto hdr = std::make_shared<bytes>(std::move(header));
    auto payload = std::make_shared<bytes>(_data.begin(), _data.end());
    task::wait([](gateway::GatewayInterface::Ptr _gateway, std::string _groupID, int _moduleID,
                   bcos::crypto::NodeIDPtr _srcNodeID, bcos::crypto::NodeIDPtr _nodeID,
                   std::shared_ptr<bytes> _hdr, std::shared_ptr<bytes> _payload,
                   ReceiveMsgFunc _receiveMsgCallback) -> task::Task<void> {
        auto error = co_await _gateway->sendMessageByNodeID(_groupID, _moduleID, _srcNodeID,
            std::move(_nodeID),
            ::ranges::views::concat(::ranges::views::single(bcos::ref(std::as_const(*_hdr))),
                ::ranges::views::single(
                    bytesConstRef(_payload->data(), _payload->size()))));
        if (_receiveMsgCallback)
        {
            _receiveMsgCallback(std::move(error));
        }
    }(gateway, m_groupID, _moduleID, m_nodeID, std::move(_nodeID), hdr, payload,
        _receiveMsgCallback));
}

/**
 * @brief: handle message timeout
 * @param _error: boost error code
 * @param _uuid: message uuid
 * @return void
 */
void FrontService::onMessageTimeout(const boost::system::error_code& _error,
    bcos::crypto::NodeIDPtr _nodeID, const std::string& _uuid)
{
    if (_error)
    {
        return;
    }

    try
    {
        Callback::Ptr callback = getAndRemoveCallback(_uuid);
        if (callback)
        {
            auto errorPtr = BCOS_ERROR_PTR(CommonError::TIMEOUT, "timeout");
            dispatchTo(*m_ioServicePool,
                [_uuid, _nodeID = std::move(_nodeID), callback = std::move(callback),
                    errorPtr = std::move(errorPtr)]() mutable {
                    callback->callbackFunc(errorPtr, _nodeID, {}, _uuid, {});
                });
        }

        FRONT_LOG(WARNING) << LOG_BADGE("onMessageTimeout") << LOG_KV("uuid", _uuid);
    }
    catch (std::exception& e)
    {
        FRONT_LOG(ERROR) << "onMessageTimeout" << LOG_KV("uuid", _uuid)
                         << LOG_KV("failed", boost::diagnostic_information(e));
    }
}
