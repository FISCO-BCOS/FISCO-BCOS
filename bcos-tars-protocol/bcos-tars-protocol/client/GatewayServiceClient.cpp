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
 * @file GatewayServiceClient.cpp
 * @author: ancelmo
 * @date 2021-04-20
 */
#include "GatewayServiceClient.h"
#include "bcos-tars-protocol/Common.h"
#include "bcos-tars-protocol/ErrorConverter.h"
#include "bcos-tars-protocol/impl/TarsServantProxyCallback.h"
#include "bcos-tars-protocol/protocol/GroupNodeInfoImpl.h"
#include "fisco-bcos-tars-service/Common/TarsUtils.h"
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/join.hpp>

std::atomic<int64_t> bcostars::GatewayServiceClient::s_tarsTimeoutCount = {0};
const int64_t bcostars::GatewayServiceClient::c_maxTarsTimeoutCount = 500;
bcostars::GatewayServiceClient::GatewayServiceClient(bcostars::GatewayServicePrx _prx,
    std::string const& _serviceName, bcos::crypto::KeyFactory::Ptr _keyFactory)
  : GatewayServiceClient(_prx, _serviceName)
{
    m_keyFactory = std::move(_keyFactory);
}
bcostars::GatewayServiceClient::GatewayServiceClient(
    bcostars::GatewayServicePrx _prx, std::string const& _serviceName)
  : m_prx(_prx), m_gatewayServiceName(_serviceName)
{}
bcostars::GatewayServiceClient::~GatewayServiceClient() = default;
void bcostars::GatewayServiceClient::setKeyFactory(bcos::crypto::KeyFactory::Ptr keyFactory)
{
    m_keyFactory = std::move(keyFactory);
}
bcos::task::Task<bcos::Error::Ptr> bcostars::GatewayServiceClient::sendMessageByNodeID(
    const std::string& _groupID, int _moduleID, bcos::crypto::NodeIDPtr _srcNodeID,
    bcos::crypto::NodeIDPtr _dstNodeID,
    ::ranges::any_view<bcos::bytesConstRef, ::ranges::category::forward> _payloads)
{
    struct SendAwaitable
    {
        struct CompletionState
        {
            std::atomic<bool> completed{false};
            std::coroutine_handle<> handle;
            bcos::Error::Ptr error;
        };

        class Callback : public bcostars::GatewayServicePrxCallback
        {
        public:
            explicit Callback(std::shared_ptr<CompletionState> state) : m_state(std::move(state)) {}

            void callback_asyncSendMessageByNodeID(const bcostars::Error& ret) override
            {
                s_tarsTimeoutCount.store(0);
                complete(toBcosError(ret));
            }
            void callback_asyncSendMessageByNodeID_exception(tars::Int32 ret) override
            {
                s_tarsTimeoutCount++;
                complete(toBcosError(ret));
            }

        private:
            void complete(bcos::Error::Ptr error)
            {
                if (!m_state->completed.exchange(true))
                {
                    m_state->error = std::move(error);
                    m_state->handle.resume();
                }
            }
            std::shared_ptr<CompletionState> m_state;
        };

        GatewayServiceClient* m_self;
        std::string m_groupID;
        int m_moduleID;
        bcos::crypto::NodeIDPtr m_srcNodeID;
        bcos::crypto::NodeIDPtr m_dstNodeID;
        std::shared_ptr<std::vector<char>> m_buffer;
        std::shared_ptr<CompletionState> m_state;

        constexpr static bool await_ready() noexcept { return false; }

        // Returns false (no suspension) on a synchronous connection-check failure so the coroutine
        // is never resumed from inside await_suspend — resuming a coroutine that is still executing
        // await_suspend is undefined behaviour. Returns true (suspend) once the RPC is in flight.
        bool await_suspend(std::coroutine_handle<> _handle)
        {
            m_state->handle = _handle;
            auto state = m_state;
            auto buffer = m_buffer;
            auto shouldBlockCall = m_self->shouldStopCall();
            auto ret = checkConnection(m_self->c_moduleName, "asyncSendMessageByNodeID",
                m_self->m_prx,
                [state, buffer](bcos::Error::Ptr _error) {
                    // connection-check failure: record the error; await_suspend returns false so
                    // the coroutine continues on this thread and await_resume delivers it
                    state->error = std::move(_error);
                    (void)buffer;
                },
                shouldBlockCall);
            if (!ret && shouldBlockCall)
            {
                return false;
            }
            auto srcNodeID = m_srcNodeID->data();
            auto destNodeID = m_dstNodeID->data();
            m_self->m_prx->tars_set_timeout(m_self->c_networkTimeout)
                ->async_asyncSendMessageByNodeID(new Callback(state), m_groupID, m_moduleID,
                    std::vector<char>(srcNodeID.begin(), srcNodeID.end()),
                    std::vector<char>(destNodeID.begin(), destNodeID.end()), *buffer);
            return true;
        }

        bcos::Error::Ptr await_resume() { return std::move(m_state->error); }
    };

    // materialise the joined payload directly as the std::vector<char> the RPC argument needs —
    // a single pass and one allocation, no second copy in await_suspend
    auto buffer = std::make_shared<std::vector<char>>();
    for (auto const& data : _payloads)
    {
        buffer->insert(buffer->end(), data.begin(), data.end());
    }
    SendAwaitable awaitable{this, _groupID, _moduleID, std::move(_srcNodeID), std::move(_dstNodeID),
        std::move(buffer), std::make_shared<SendAwaitable::CompletionState>()};
    co_return co_await awaitable;
}
void bcostars::GatewayServiceClient::asyncGetPeers(std::function<void(
        bcos::Error::Ptr, bcos::gateway::GatewayInfo::Ptr, bcos::gateway::GatewayInfosPtr)>
        _callback)
{
    class Callback : public bcostars::GatewayServicePrxCallback
    {
    public:
        Callback(std::function<void(
                bcos::Error::Ptr, bcos::gateway::GatewayInfo::Ptr, bcos::gateway::GatewayInfosPtr)>
                callback)
          : m_callback(callback)
        {}

        void callback_asyncGetPeers(const bcostars::Error& ret,
            const bcostars::GatewayInfo& _localInfo,
            const std::vector<bcostars::GatewayInfo>& _peers) override
        {
            s_tarsTimeoutCount.store(0);
            auto localGatewayInfo = fromTarsGatewayInfo(_localInfo);
            auto peersGatewayInfos = std::make_shared<bcos::gateway::GatewayInfos>();
            for (auto const& peerNodeInfo : _peers)
            {
                peersGatewayInfos->emplace_back(fromTarsGatewayInfo(peerNodeInfo));
            }
            m_callback(toBcosError(ret), localGatewayInfo, peersGatewayInfos);
        }
        void callback_asyncGetPeers_exception(tars::Int32 ret) override
        {
            s_tarsTimeoutCount++;
            m_callback(toBcosError(ret), nullptr, nullptr);
        }

    private:
        std::function<void(
            bcos::Error::Ptr, bcos::gateway::GatewayInfo::Ptr, bcos::gateway::GatewayInfosPtr)>
            m_callback;
    };
    auto shouldBlockCall = shouldStopCall();
    auto ret = checkConnection(
        c_moduleName, "asyncGetPeers", m_prx,
        [_callback](bcos::Error::Ptr _error) {
            if (_callback)
            {
                _callback(std::move(_error), nullptr, nullptr);
            }
        },
        shouldBlockCall);
    if (!ret && shouldBlockCall)
    {
        return;
    }
    m_prx->async_asyncGetPeers(new Callback(_callback));
}
bcos::task::Task<void> bcostars::GatewayServiceClient::broadcastMessage(uint16_t type,
    std::string_view groupID, int moduleID, const bcos::crypto::NodeID& srcNodeID,
    ::ranges::any_view<bcos::bytesConstRef, ::ranges::category::forward> payloads)
{
    auto shouldBlockCall = shouldStopCall();
    auto ret =
        checkConnection(c_moduleName, "asyncSendBroadcastMessage", m_prx, nullptr, shouldBlockCall);
    if (!ret && shouldBlockCall)
    {
        co_return;
    }
    bcos::bytes data = ::ranges::views::join(payloads) | ::ranges::to<bcos::bytes>();
    auto srcNodeIDBytes = srcNodeID.data();
    m_prx->async_asyncSendBroadcastMessage(nullptr, type, std::string{groupID}, moduleID,
        std::vector<char>(srcNodeIDBytes.begin(), srcNodeIDBytes.end()),
        std::vector<char>(data.begin(), data.end()));
};
void bcostars::GatewayServiceClient::asyncGetGroupNodeInfo(
    const std::string& _groupID, bcos::gateway::GetGroupNodeInfoFunc _onGetGroupNodeInfo)
{
    class Callback : public GatewayServicePrxCallback
    {
    public:
        Callback(
            bcos::gateway::GetGroupNodeInfoFunc callback, bcos::crypto::KeyFactory::Ptr keyFactory)
          : m_callback(callback), m_keyFactory(keyFactory)
        {}
        void callback_asyncGetGroupNodeInfo(
            const bcostars::Error& ret, const GroupNodeInfo& groupNodeInfo) override
        {
            s_tarsTimeoutCount.store(0);
            auto bcosGroupNodeInfo = std::make_shared<bcostars::protocol::GroupNodeInfoImpl>(
                [m_groupNodeInfo = groupNodeInfo]() mutable { return &m_groupNodeInfo; });
            m_callback(toBcosError(ret), bcosGroupNodeInfo);
        }
        void callback_asyncGetGroupNodeInfo_exception(tars::Int32 ret) override
        {
            s_tarsTimeoutCount++;
            m_callback(toBcosError(ret), nullptr);
        }

    private:
        bcos::gateway::GetGroupNodeInfoFunc m_callback;
        bcos::crypto::KeyFactory::Ptr m_keyFactory;
    };
    auto shouldBlockCall = shouldStopCall();
    auto ret = checkConnection(
        c_moduleName, "asyncGetGroupNodeInfo", m_prx,
        [_onGetGroupNodeInfo](bcos::Error::Ptr _error) {
            if (_onGetGroupNodeInfo)
            {
                _onGetGroupNodeInfo(_error, nullptr);
            }
        },
        shouldBlockCall);
    if (!ret && shouldBlockCall)
    {
        return;
    }
    m_prx->async_asyncGetGroupNodeInfo(new Callback(_onGetGroupNodeInfo, m_keyFactory), _groupID);
}
void bcostars::GatewayServiceClient::asyncNotifyGroupInfo(
    bcos::group::GroupInfo::Ptr _groupInfo, std::function<void(bcos::Error::Ptr&&)> _callback)
{
    class Callback : public bcostars::GatewayServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr&&)> callback) : m_callback(callback) {}

        void callback_asyncNotifyGroupInfo(const bcostars::Error& ret) override
        {
            s_tarsTimeoutCount.store(0);
            m_callback(toBcosError(ret));
        }
        void callback_asyncNotifyGroupInfo_exception(tars::Int32 ret) override
        {
            s_tarsTimeoutCount++;
            m_callback(toBcosError(ret));
        }

    private:
        std::function<void(bcos::Error::Ptr&&)> m_callback;
    };
    auto shouldBlockCall = shouldStopCall();
    auto ret = checkConnection(
        c_moduleName, "asyncNotifyGroupInfo", m_prx,
        [_callback](bcos::Error::Ptr _error) {
            if (_callback)
            {
                _callback(std::move(_error));
            }
        },
        shouldBlockCall);
    if (!ret && shouldBlockCall)
    {
        return;
    }

    auto activeEndPoints = tarsProxyAvailableEndPoints(m_prx);
    auto tarsGroupInfo = toTarsGroupInfo(_groupInfo);

    // notify groupInfo to all gateway nodes
    for (auto const& endPoint : activeEndPoints)
    {
        auto prx = bcostars::createServantProxy<GatewayServicePrx>(m_gatewayServiceName, endPoint);
        prx->async_asyncNotifyGroupInfo(new Callback(_callback), tarsGroupInfo);
    }
}
void bcostars::GatewayServiceClient::asyncSendMessageByTopic(const std::string& _topic,
    bcos::bytesConstRef _data,
    std::function<void(bcos::Error::Ptr&&, int16_t, bcos::bytesConstRef)> _respFunc)
{
    class Callback : public bcostars::GatewayServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr&&, int16_t, bcos::bytesConstRef)> callback)
          : m_callback(callback)
        {}
        void callback_asyncSendMessageByTopic(const bcostars::Error& ret, tars::Int32 _type,
            const std::vector<tars::Char>& _responseData) override
        {
            s_tarsTimeoutCount.store(0);
            auto data = bcos::bytesConstRef(
                reinterpret_cast<const bcos::byte*>(_responseData.data()), _responseData.size());
            m_callback(toBcosError(ret), _type, data);
        }
        void callback_asyncSendMessageByTopic_exception(tars::Int32 ret) override
        {
            s_tarsTimeoutCount++;
            m_callback(toBcosError(ret), 0, {});
        }

    private:
        std::function<void(bcos::Error::Ptr&&, int16_t, bcos::bytesConstRef)> m_callback;
    };
    auto shouldBlockCall = shouldStopCall();
    auto ret = checkConnection(
        c_moduleName, "asyncSendMessageByTopic", m_prx,
        [_respFunc](bcos::Error::Ptr _error) {
            if (_respFunc)
            {
                _respFunc(std::move(_error), 0, {});
            }
        },
        shouldBlockCall);
    if (!ret && shouldBlockCall)
    {
        return;
    }
    std::vector<tars::Char> tarsRequestData(_data.begin(), _data.end());
    m_prx->tars_set_timeout(c_amopTimeout)
        ->async_asyncSendMessageByTopic(new Callback(_respFunc), _topic, tarsRequestData);
}
void bcostars::GatewayServiceClient::asyncSendBroadcastMessageByTopic(
    const std::string& _topic, bcos::bytesConstRef _data)
{
    auto shouldBlockCall = shouldStopCall();
    auto ret = checkConnection(
        c_moduleName, "asyncSendBroadcastMessageByTopic", m_prx, nullptr, shouldBlockCall);
    if (!ret && shouldBlockCall)
    {
        return;
    }
    std::vector<tars::Char> tarsRequestData(_data.begin(), _data.end());
    m_prx->async_asyncSendBroadcastMessageByTopic(nullptr, _topic, tarsRequestData);
}
void bcostars::GatewayServiceClient::asyncSubscribeTopic(std::string const& _clientID,
    std::string const& _topicInfo, std::function<void(bcos::Error::Ptr&&)> _callback)
{
    class Callback : public bcostars::GatewayServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr&&)> callback) : m_callback(std::move(callback))
        {}
        void callback_asyncSubscribeTopic(const bcostars::Error& ret) override
        {
            s_tarsTimeoutCount.store(0);
            m_callback(toBcosError(ret));
        }
        void callback_asyncSubscribeTopic_exception(tars::Int32 ret) override
        {
            s_tarsTimeoutCount++;
            return m_callback(toBcosError(ret));
        }

    private:
        std::function<void(bcos::Error::Ptr&&)> m_callback;
    };
    auto shouldBlockCall = shouldStopCall();
    auto ret = checkConnection(
        c_moduleName, "asyncSubscribeTopic", m_prx,
        [_callback](bcos::Error::Ptr _error) {
            if (_callback)
            {
                _callback(std::move(_error));
            }
        },
        shouldBlockCall);
    if (!ret && shouldBlockCall)
    {
        return;
    }
    m_prx->async_asyncSubscribeTopic(new Callback(_callback), _clientID, _topicInfo);
}
void bcostars::GatewayServiceClient::asyncRemoveTopic(std::string const& _clientID,
    std::vector<std::string> const& _topicList, std::function<void(bcos::Error::Ptr&&)> _callback)
{
    class Callback : public bcostars::GatewayServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr&&)> callback) : m_callback(callback) {}
        void callback_asyncRemoveTopic(const bcostars::Error& ret) override
        {
            s_tarsTimeoutCount.store(0);
            m_callback(toBcosError(ret));
        }
        void callback_asyncRemoveTopic_exception(tars::Int32 ret) override
        {
            s_tarsTimeoutCount++;
            return m_callback(toBcosError(ret));
        }

    private:
        std::function<void(bcos::Error::Ptr&&)> m_callback;
    };
    auto shouldBlockCall = shouldStopCall();
    auto ret = checkConnection(
        c_moduleName, "asyncRemoveTopic", m_prx,
        [_callback](bcos::Error::Ptr _error) {
            if (_callback)
            {
                _callback(std::move(_error));
            }
        },
        shouldBlockCall);
    if (!ret && shouldBlockCall)
    {
        return;
    }
    m_prx->async_asyncRemoveTopic(new Callback(_callback), _clientID, _topicList);
}
bcostars::GatewayServicePrx bcostars::GatewayServiceClient::prx()
{
    return m_prx;
}
bool bcostars::GatewayServiceClient::shouldStopCall()
{
    return (s_tarsTimeoutCount >= c_maxTarsTimeoutCount);
}
void bcostars::GatewayServiceClient::start() {}
void bcostars::GatewayServiceClient::stop() {}
