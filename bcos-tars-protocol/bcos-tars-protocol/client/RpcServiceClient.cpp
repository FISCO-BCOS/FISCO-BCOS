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
 * @file RPCServiceClient.cpp
 * @author: ancelmo
 * @date 2021-04-20
 */
#include "RpcServiceClient.h"
#include "bcos-tars-protocol/Common.h"
#include "bcos-tars-protocol/ErrorConverter.h"
#include "bcos-tars-protocol/impl/TarsServantProxyCallback.h"
#include "fisco-bcos-tars-service/Common/TarsUtils.h"

std::atomic<int64_t> bcostars::RpcServiceClient::s_tarsTimeoutCount = {0};
const int64_t bcostars::RpcServiceClient::c_maxTarsTimeoutCount = 500;
bcostars::RpcServiceClient::RpcServiceClient(
    bcostars::RpcServicePrx _prx, std::string const& _rpcServiceName)
  : m_prx(_prx), m_rpcServiceName(_rpcServiceName)
{}
bcostars::RpcServiceClient::~RpcServiceClient() {}
bcostars::RpcServiceClient::Callback::Callback(std::function<void(bcos::Error::Ptr)> _callback)
  : RpcServicePrxCallback(), m_callback(_callback)
{}
bcostars::RpcServiceClient::Callback::~Callback() {}
void bcostars::RpcServiceClient::Callback::callback_asyncNotifyBlockNumber(
    const bcostars::Error& ret)
{
    s_tarsTimeoutCount.store(0);
    m_callback(toBcosError(ret));
}
void bcostars::RpcServiceClient::Callback::callback_asyncNotifyBlockNumber_exception(
    tars::Int32 ret)
{
    s_tarsTimeoutCount++;
    m_callback(toBcosError(ret));
}
void bcostars::RpcServiceClient::asyncNotifyBlockNumber(std::string const& _groupID,
    std::string const& _nodeName, bcos::protocol::BlockNumber _blockNumber,
    std::function<void(bcos::Error::Ptr)> _callback)
{
    auto shouldBlockCall = shouldStopCall();
    auto ret = checkConnection(
        c_moduleName, "asyncNotifyBlockNumber", m_prx, [_callback](bcos::Error::Ptr _error) {
            if (_callback)
            {
                _callback(_error);
            }
        });
    if (!ret && shouldBlockCall)
    {
        return;
    }

    auto activeEndPoints = tarsProxyAvailableEndPoints(m_prx);
    // notify block number to all rpc nodes
    for (auto const& endPoint : activeEndPoints)
    {
        auto prx = bcostars::createServantProxy<RpcServicePrx>(m_rpcServiceName, endPoint);
        prx->async_asyncNotifyBlockNumber(
            new Callback(_callback), _groupID, _nodeName, _blockNumber);
    }
}
void bcostars::RpcServiceClient::asyncNotifyGroupInfo(
    bcos::group::GroupInfo::Ptr _groupInfo, std::function<void(bcos::Error::Ptr&&)> _callback)
{
    class Callback : public bcostars::RpcServicePrxCallback
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

    for (auto const& endPoint : activeEndPoints)
    {
        auto prx = bcostars::createServantProxy<RpcServicePrx>(m_rpcServiceName, endPoint);
        prx->async_asyncNotifyGroupInfo(new Callback(_callback), tarsGroupInfo);
    }
}
bcos::task::Task<std::tuple<bcos::Error::Ptr, bcos::bytesPointer>>
bcostars::RpcServiceClient::notifyAMOPMessage(
    int16_t _type, std::string const& _topic, bcos::bytesConstRef _data)
{
    struct NotifyAwaitable
    {
        struct CompletionState
        {
            std::atomic<bool> completed{false};
            std::coroutine_handle<> handle;
            bcos::Error::Ptr error;
            bcos::bytesPointer data;
        };

        class Callback : public bcostars::RpcServicePrxCallback
        {
        public:
            explicit Callback(std::shared_ptr<CompletionState> state) : m_state(std::move(state)) {}

            void callback_asyncNotifyAMOPMessage(
                const bcostars::Error& ret, const std::vector<tars::Char>& _responseData) override
            {
                s_tarsTimeoutCount.store(0);
                complete(toBcosError(ret),
                    std::make_shared<bcos::bytes>(_responseData.begin(), _responseData.end()));
            }
            void callback_asyncNotifyAMOPMessage_exception(tars::Int32 ret) override
            {
                s_tarsTimeoutCount++;
                complete(toBcosError(ret), nullptr);
            }

        private:
            void complete(bcos::Error::Ptr error, bcos::bytesPointer data)
            {
                if (!m_state->completed.exchange(true))
                {
                    m_state->error = std::move(error);
                    m_state->data = std::move(data);
                    m_state->handle.resume();
                }
            }
            std::shared_ptr<CompletionState> m_state;
        };

        RpcServiceClient* m_self;
        int16_t m_type;
        std::string m_topic;
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
            auto ret = checkConnection(m_self->c_moduleName, "asyncNotifyAMOPMessage",
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
            m_self->m_prx->tars_set_timeout(m_self->c_amopTimeout)
                ->async_asyncNotifyAMOPMessage(new Callback(state), m_type, m_topic, *buffer);
            return true;
        }

        std::tuple<bcos::Error::Ptr, bcos::bytesPointer> await_resume()
        {
            return {std::move(m_state->error), std::move(m_state->data)};
        }
    };

    // copy the payload into an owned buffer before the RPC — the caller's bytesConstRef is not
    // guaranteed to outlive the request
    auto buffer = std::make_shared<std::vector<char>>(_data.begin(), _data.end());
    NotifyAwaitable awaitable{this, _type, _topic, std::move(buffer),
        std::make_shared<NotifyAwaitable::CompletionState>()};
    co_return co_await awaitable;
}
void bcostars::RpcServiceClient::asyncNotifySubscribeTopic(
    std::function<void(bcos::Error::Ptr&& _error, std::string)> _callback)
{
    class Callback : public bcostars::RpcServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr&&, std::string)> callback)
          : m_callback(callback)
        {}

        void callback_asyncNotifySubscribeTopic(
            const bcostars::Error& ret, std::string const& _topicInfo) override
        {
            s_tarsTimeoutCount.store(0);
            m_callback(toBcosError(ret), _topicInfo);
        }
        void callback_asyncNotifySubscribeTopic_exception(tars::Int32 ret) override
        {
            s_tarsTimeoutCount++;
            m_callback(toBcosError(ret), "");
        }

    private:
        std::function<void(bcos::Error::Ptr&&, std::string)> m_callback;
    };
    auto shouldBlockCall = shouldStopCall();
    auto ret = checkConnection(
        c_moduleName, "asyncNotifySubscribeTopic", m_prx,
        [_callback](bcos::Error::Ptr _error) {
            if (_callback)
            {
                _callback(std::move(_error), "");
            }
        },
        shouldBlockCall);
    if (!ret && shouldBlockCall)
    {
        return;
    }
    m_prx->async_asyncNotifySubscribeTopic(new Callback(_callback));
}
bcostars::RpcServicePrx bcostars::RpcServiceClient::prx()
{
    return m_prx;
}
void bcostars::RpcServiceClient::start() {}
void bcostars::RpcServiceClient::stop() {}
bool bcostars::RpcServiceClient::shouldStopCall()
{
    return (s_tarsTimeoutCount >= c_maxTarsTimeoutCount);
}
