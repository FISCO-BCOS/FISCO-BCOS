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
 * @file RPCServiceClient.h
 * @author: ancelmo
 * @date 2021-04-20
 */

#pragma once

#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "bcos-tars-protocol/Common.h"
#include "bcos-tars-protocol/ErrorConverter.h"
#include "bcos-tars-protocol/protocol/TransactionSubmitResultImpl.h"
#include "bcos-tars-protocol/tars/RpcService.h"
#include <bcos-framework/interfaces/rpc/RPCInterface.h>
#include <bcos-utilities/Common.h>
#include <tarscpp/servant/Application.h>
#include <boost/core/ignore_unused.hpp>

namespace bcostars
{
class RpcServiceClient : public bcos::rpc::RPCInterface
{
public:
    RpcServiceClient(bcostars::RpcServicePrx _prx, std::string const& _rpcServiceName)
      : m_prx(_prx), m_rpcServiceName(_rpcServiceName)
    {}
    ~RpcServiceClient() override {}

    class Callback : public RpcServicePrxCallback
    {
    public:
        explicit Callback(std::function<void(bcos::Error::Ptr)> _callback)
          : RpcServicePrxCallback(), m_callback(_callback)
        {}
        ~Callback() override {}

        void callback_asyncNotifyBlockNumber(const bcostars::Error& ret) override
        {
            m_callback(toBcosError(ret));
        }
        void callback_asyncNotifyBlockNumber_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret));
        }

    private:
        std::function<void(bcos::Error::Ptr)> m_callback;
    };

    void asyncNotifyBlockNumber(std::string const& _groupID, std::string const& _nodeName,
        bcos::protocol::BlockNumber _blockNumber,
        std::function<void(bcos::Error::Ptr)> _callback) override
    {
        auto ret = checkConnection(
            c_moduleName, "asyncNotifyBlockNumber", m_prx, [_callback](bcos::Error::Ptr _error) {
                if (_callback)
                {
                    _callback(_error);
                }
            });
        if (!ret)
        {
            return;
        }
        vector<EndpointInfo> activeEndPoints;
        vector<EndpointInfo> nactiveEndPoints;
        m_prx->tars_endpointsAll(activeEndPoints, nactiveEndPoints);
        // notify block number to all rpc nodes
        for (auto const& endPoint : activeEndPoints)
        {
            auto endPointStr = endPointToString(endPoint);
            auto prx = Application::getCommunicator()->stringToProxy<RpcServicePrx>(endPointStr);
            prx->async_asyncNotifyBlockNumber(
                new Callback(_callback), _groupID, _nodeName, _blockNumber);
        }
    }

    void asyncNotifyGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo,
        std::function<void(bcos::Error::Ptr&&)> _callback) override
    {
        class Callback : public bcostars::RpcServicePrxCallback
        {
        public:
            Callback(std::function<void(bcos::Error::Ptr&&)> callback) : m_callback(callback) {}

            void callback_asyncNotifyGroupInfo(const bcostars::Error& ret) override
            {
                m_callback(toBcosError(ret));
            }
            void callback_asyncNotifyGroupInfo_exception(tars::Int32 ret) override
            {
                m_callback(toBcosError(ret));
            }

        private:
            std::function<void(bcos::Error::Ptr&&)> m_callback;
        };
        auto ret = checkConnection(
            c_moduleName, "asyncNotifyGroupInfo", m_prx, [_callback](bcos::Error::Ptr _error) {
                if (_callback)
                {
                    _callback(std::move(_error));
                }
            });
        if (!ret)
        {
            return;
        }
        vector<EndpointInfo> activeEndPoints;
        vector<EndpointInfo> nactiveEndPoints;
        if (activeEndPoints.size() == 0)
        {
            BCOS_LOG(TRACE) << LOG_DESC("RPC: asyncNotifyGroupInfo error for empty connection")
                            << bcos::group::printGroupInfo(_groupInfo);
            return;
        }
        m_prx->tars_endpointsAll(activeEndPoints, nactiveEndPoints);
        // notify groupInfo to all rpc nodes
        auto tarsGroupInfo = toTarsGroupInfo(_groupInfo);
        for (auto const& endPoint : activeEndPoints)
        {
            auto endPointStr = endPointToString(endPoint);
            auto prx = Application::getCommunicator()->stringToProxy<RpcServicePrx>(endPointStr);
            prx->async_asyncNotifyGroupInfo(new Callback(_callback), tarsGroupInfo);
        }
    }

    void asyncNotifyAMOPMessage(int16_t _type, std::string const& _topic, bcos::bytesConstRef _data,
        std::function<void(bcos::Error::Ptr&& _error, bcos::bytesPointer _responseData)> _callback)
        override
    {
        class Callback : public bcostars::RpcServicePrxCallback
        {
        public:
            Callback(std::function<void(bcos::Error::Ptr&&, bcos::bytesPointer)> callback)
              : m_callback(callback)
            {}

            void callback_asyncNotifyAMOPMessage(
                const bcostars::Error& ret, const vector<tars::Char>& _responseData) override
            {
                auto responseData =
                    std::make_shared<bcos::bytes>(_responseData.begin(), _responseData.end());
                m_callback(toBcosError(ret), responseData);
            }
            void callback_asyncNotifyAMOPMessage_exception(tars::Int32 ret) override
            {
                m_callback(toBcosError(ret), nullptr);
            }

        private:
            std::function<void(bcos::Error::Ptr&&, bcos::bytesPointer)> m_callback;
        };
        auto ret = checkConnection(
            c_moduleName, "asyncNotifyAMOPMessage", m_prx, [_callback](bcos::Error::Ptr _error) {
                if (_callback)
                {
                    _callback(std::move(_error), nullptr);
                }
            });
        if (!ret)
        {
            return;
        }
        vector<tars::Char> request(_data.begin(), _data.end());
        m_prx->tars_set_timeout(c_amopTimeout)
            ->async_asyncNotifyAMOPMessage(new Callback(_callback), _type, _topic, request);
    }

    void asyncNotifySubscribeTopic(
        std::function<void(bcos::Error::Ptr&& _error)> _callback) override
    {
        class Callback : public bcostars::RpcServicePrxCallback
        {
        public:
            Callback(std::function<void(bcos::Error::Ptr&&)> callback) : m_callback(callback) {}

            void callback_asyncNotifySubscribeTopic(const bcostars::Error& ret) override
            {
                m_callback(toBcosError(ret));
            }
            void callback_asyncNotifySubscribeTopic_exception(tars::Int32 ret) override
            {
                m_callback(toBcosError(ret));
            }

        private:
            std::function<void(bcos::Error::Ptr&&)> m_callback;
        };
        auto ret = checkConnection(
            c_moduleName, "asyncNotifySubscribeTopic", m_prx, [_callback](bcos::Error::Ptr _error) {
                if (_callback)
                {
                    _callback(std::move(_error));
                }
            });
        if (!ret)
        {
            return;
        }
        m_prx->async_asyncNotifySubscribeTopic(new Callback(_callback));
    }

    bcostars::RpcServicePrx prx() { return m_prx; }

protected:
    void start() override {}
    void stop() override {}
    std::string endPointToString(EndpointInfo _endPoint)
    {
        return m_rpcServiceName + "@tcp -h " + _endPoint.getEndpoint().getHost() + " -p " +
               boost::lexical_cast<std::string>(_endPoint.getEndpoint().getPort());
    }

private:
    bcostars::RpcServicePrx m_prx;
    std::string m_rpcServiceName;
    std::string const c_moduleName = "RpcServiceClient";
    // AMOP timeout 40s
    const int c_amopTimeout = 40000;
};

}  // namespace bcostars