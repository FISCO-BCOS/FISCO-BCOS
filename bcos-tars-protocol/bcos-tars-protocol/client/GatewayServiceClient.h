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
 * @file GatewayServiceClient.h
 * @author: ancelmo
 * @date 2021-04-20
 */

#pragma once

#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "../Common.h"
#include "../ErrorConverter.h"
#include "../protocol/GroupNodeInfoImpl.h"
#include "bcos-tars-protocol/tars/GatewayService.h"
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <string>

#define GATEWAYCLIENT_LOG(LEVEL) BCOS_LOG(LEVEL) << "[GATEWAYCLIENT][INITIALIZER]"
#define GATEWAYCLIENT_BADGE "[GATEWAYCLIENT]"
namespace bcostars
{
class GatewayServiceClient : public bcos::gateway::GatewayInterface
{
public:
    GatewayServiceClient(bcostars::GatewayServicePrx _prx, std::string const& _serviceName,
        bcos::crypto::KeyFactory::Ptr _keyFactory)
      : GatewayServiceClient(_prx, _serviceName)
    {
        m_keyFactory = _keyFactory;
    }
    GatewayServiceClient(bcostars::GatewayServicePrx _prx, std::string const& _serviceName)
      : m_prx(_prx), m_gatewayServiceName(_serviceName)
    {}
    virtual ~GatewayServiceClient() {}

    void setKeyFactory(bcos::crypto::KeyFactory::Ptr keyFactory) { m_keyFactory = keyFactory; }

    void asyncSendMessageByNodeID(const std::string& _groupID, bcos::crypto::NodeIDPtr _srcNodeID,
        bcos::crypto::NodeIDPtr _dstNodeID, bcos::bytesConstRef _payload,
        bcos::gateway::ErrorRespFunc _errorRespFunc) override
    {
        class Callback : public bcostars::GatewayServicePrxCallback
        {
        public:
            Callback(bcos::gateway::ErrorRespFunc callback) : m_callback(callback) {}

            void callback_asyncSendMessageByNodeID(const bcostars::Error& ret) override
            {
                s_tarsTimeoutCount.store(0);
                m_callback(toBcosError(ret));
            }
            void callback_asyncSendMessageByNodeID_exception(tars::Int32 ret) override
            {
                s_tarsTimeoutCount++;
                m_callback(toBcosError(ret));
            }

        private:
            bcos::gateway::ErrorRespFunc m_callback;
        };
        auto shouldBlockCall = shouldStopCall();
        auto ret = checkConnection(
            c_moduleName, "asyncSendMessageByNodeID", m_prx,
            [_errorRespFunc](bcos::Error::Ptr _error) {
                if (_errorRespFunc)
                {
                    _errorRespFunc(_error);
                }
            },
            shouldBlockCall);
        if (!ret && shouldBlockCall)
        {
            return;
        }
        auto srcNodeID = _srcNodeID->data();
        auto destNodeID = _dstNodeID->data();
        m_prx->tars_set_timeout(c_networkTimeout)
            ->async_asyncSendMessageByNodeID(new Callback(_errorRespFunc), _groupID,
                std::vector<char>(srcNodeID.begin(), srcNodeID.end()),
                std::vector<char>(destNodeID.begin(), destNodeID.end()),
                std::vector<char>(_payload.begin(), _payload.end()));
    }

    void asyncGetPeers(std::function<void(
            bcos::Error::Ptr, bcos::gateway::GatewayInfo::Ptr, bcos::gateway::GatewayInfosPtr)>
            _callback) override
    {
        class Callback : public bcostars::GatewayServicePrxCallback
        {
        public:
            Callback(std::function<void(bcos::Error::Ptr, bcos::gateway::GatewayInfo::Ptr,
                    bcos::gateway::GatewayInfosPtr)>
                    callback)
              : m_callback(callback)
            {}

            void callback_asyncGetPeers(const bcostars::Error& ret,
                const bcostars::GatewayInfo& _localInfo,
                const vector<bcostars::GatewayInfo>& _peers) override
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
                    _callback(_error, nullptr, nullptr);
                }
            },
            shouldBlockCall);
        if (!ret && shouldBlockCall)
        {
            return;
        }
        m_prx->async_asyncGetPeers(new Callback(_callback));
    }

    void asyncSendMessageByNodeIDs(const std::string& _groupID, bcos::crypto::NodeIDPtr _srcNodeID,
        const bcos::crypto::NodeIDs& _dstNodeIDs, bcos::bytesConstRef _payload) override
    {
        std::vector<std::vector<char>> tarsNodeIDs;
        for (auto const& it : _dstNodeIDs)
        {
            auto nodeID = it->data();
            tarsNodeIDs.emplace_back(nodeID.begin(), nodeID.end());
        }
        auto shouldBlockCall = shouldStopCall();
        auto ret = checkConnection(
            c_moduleName, "asyncSendMessageByNodeIDs", m_prx, nullptr, shouldBlockCall);
        if (!ret && shouldBlockCall)
        {
            return;
        }
        auto srcNodeID = _srcNodeID->data();
        m_prx->async_asyncSendMessageByNodeIDs(nullptr, _groupID,
            std::vector<char>(srcNodeID.begin(), srcNodeID.end()), tarsNodeIDs,
            std::vector<char>(_payload.begin(), _payload.end()));
    }

    void asyncSendBroadcastMessage(uint16_t _type, const std::string& _groupID,
        bcos::crypto::NodeIDPtr _srcNodeID, bcos::bytesConstRef _payload) override
    {
        auto shouldBlockCall = shouldStopCall();
        auto ret = checkConnection(
            c_moduleName, "asyncSendBroadcastMessage", m_prx, nullptr, shouldBlockCall);
        if (!ret && shouldBlockCall)
        {
            return;
        }
        auto srcNodeID = _srcNodeID->data();
        m_prx->async_asyncSendBroadcastMessage(nullptr, _type, _groupID,
            std::vector<char>(srcNodeID.begin(), srcNodeID.end()),
            std::vector<char>(_payload.begin(), _payload.end()));
    }

    void asyncGetGroupNodeInfo(const std::string& _groupID,
        bcos::gateway::GetGroupNodeInfoFunc _onGetGroupNodeInfo) override
    {
        class Callback : public GatewayServicePrxCallback
        {
        public:
            Callback(bcos::gateway::GetGroupNodeInfoFunc callback,
                bcos::crypto::KeyFactory::Ptr keyFactory)
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
        m_prx->async_asyncGetGroupNodeInfo(
            new Callback(_onGetGroupNodeInfo, m_keyFactory), _groupID);
    }

    void asyncNotifyGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo,
        std::function<void(bcos::Error::Ptr&&)> _callback) override
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
        std::vector<tars::EndpointInfo> activeEndPoints;
        vector<tars::EndpointInfo> nactiveEndPoints;
        m_prx->tars_endpointsAll(activeEndPoints, nactiveEndPoints);
        auto tarsGroupInfo = toTarsGroupInfo(_groupInfo);
        // try to call non-activate-endpoints when with zero connection
        if (activeEndPoints.size() == 0)
        {
            // notify groupInfo to all gateway nodes
            for (auto const& endPoint : nactiveEndPoints)
            {
                auto endPointStr = endPointToString(endPoint);
                auto prx = tars::Application::getCommunicator()->stringToProxy<GatewayServicePrx>(
                    endPointStr);
                prx->async_asyncNotifyGroupInfo(new Callback(_callback), tarsGroupInfo);
            }
        }
        // notify groupInfo to all gateway nodes
        for (auto const& endPoint : activeEndPoints)
        {
            auto endPointStr = endPointToString(endPoint);
            auto prx =
                tars::Application::getCommunicator()->stringToProxy<GatewayServicePrx>(endPointStr);
            prx->async_asyncNotifyGroupInfo(new Callback(_callback), tarsGroupInfo);
        }
    }

    void asyncSendMessageByTopic(const std::string& _topic, bcos::bytesConstRef _data,
        std::function<void(bcos::Error::Ptr&&, int16_t, bcos::bytesPointer)> _respFunc) override
    {
        class Callback : public bcostars::GatewayServicePrxCallback
        {
        public:
            Callback(std::function<void(bcos::Error::Ptr&&, int16_t, bcos::bytesPointer)> callback)
              : m_callback(callback)
            {}
            void callback_asyncSendMessageByTopic(const bcostars::Error& ret, tars::Int32 _type,
                const vector<tars::Char>& _responseData) override
            {
                s_tarsTimeoutCount.store(0);
                auto data =
                    std::make_shared<bcos::bytes>(_responseData.begin(), _responseData.end());
                m_callback(toBcosError(ret), _type, data);
            }
            void callback_asyncSendMessageByTopic_exception(tars::Int32 ret) override
            {
                s_tarsTimeoutCount++;
                return m_callback(toBcosError(ret), 0, nullptr);
            }

        private:
            std::function<void(bcos::Error::Ptr&&, int16_t, bcos::bytesPointer)> m_callback;
        };
        auto shouldBlockCall = shouldStopCall();
        auto ret = checkConnection(
            c_moduleName, "asyncSendMessageByTopic", m_prx,
            [_respFunc](bcos::Error::Ptr _error) {
                if (_respFunc)
                {
                    _respFunc(std::move(_error), 0, nullptr);
                }
            },
            shouldBlockCall);
        if (!ret && shouldBlockCall)
        {
            return;
        }
        vector<tars::Char> tarsRequestData(_data.begin(), _data.end());
        m_prx->tars_set_timeout(c_amopTimeout)
            ->async_asyncSendMessageByTopic(new Callback(_respFunc), _topic, tarsRequestData);
    }

    void asyncSendBroadcastMessageByTopic(
        const std::string& _topic, bcos::bytesConstRef _data) override
    {
        auto shouldBlockCall = shouldStopCall();
        auto ret = checkConnection(
            c_moduleName, "asyncSendBroadcastMessageByTopic", m_prx, nullptr, shouldBlockCall);
        if (!ret && shouldBlockCall)
        {
            return;
        }
        vector<tars::Char> tarsRequestData(_data.begin(), _data.end());
        m_prx->async_asyncSendBroadcastMessageByTopic(nullptr, _topic, tarsRequestData);
    }

    void asyncSubscribeTopic(std::string const& _clientID, std::string const& _topicInfo,
        std::function<void(bcos::Error::Ptr&&)> _callback) override
    {
        class Callback : public bcostars::GatewayServicePrxCallback
        {
        public:
            Callback(std::function<void(bcos::Error::Ptr&&)> callback) : m_callback(callback) {}
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

    void asyncRemoveTopic(std::string const& _clientID, std::vector<std::string> const& _topicList,
        std::function<void(bcos::Error::Ptr&&)> _callback) override
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

    bcostars::GatewayServicePrx prx() { return m_prx; }

protected:
    void start() override {}
    void stop() override {}
    std::string endPointToString(tars::EndpointInfo _endPoint)
    {
        return m_gatewayServiceName + "@tcp -h " + _endPoint.getEndpoint().getHost() + " -p " +
               boost::lexical_cast<std::string>(_endPoint.getEndpoint().getPort());
    }

    static bool shouldStopCall() { return (s_tarsTimeoutCount >= c_maxTarsTimeoutCount); }

private:
    bcostars::GatewayServicePrx m_prx;
    std::string m_gatewayServiceName;
    // Note: only useful for asyncGetGroupNodeInfo
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    std::string const c_moduleName = "GatewayServiceClient";
    // AMOP timeout 40s
    const int c_amopTimeout = 40000;
    const int c_networkTimeout = 40000;
    static std::atomic<int64_t> s_tarsTimeoutCount;
    static const int64_t c_maxTarsTimeoutCount;
};
}  // namespace bcostars
