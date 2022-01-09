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

#include "bcos-tars-protocol/Common.h"
#include "bcos-tars-protocol/ErrorConverter.h"
#include "bcos-tars-protocol/tars/GatewayService.h"
#include <bcos-framework/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/interfaces/gateway/GatewayInterface.h>
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
    GatewayServiceClient(bcostars::GatewayServicePrx _proxy, std::string const& _serviceName)
      : m_proxy(_proxy), m_gatewayServiceName(_serviceName)
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
                m_callback(toBcosError(ret));
            }
            void callback_asyncSendMessageByNodeID_exception(tars::Int32 ret) override
            {
                m_callback(toBcosError(ret));
            }

        private:
            bcos::gateway::ErrorRespFunc m_callback;
        };
        auto ret = checkConnection(c_moduleName, "asyncSendMessageByNodeID", m_proxy,
            [_errorRespFunc](bcos::Error::Ptr _error) {
                if (_errorRespFunc)
                {
                    _errorRespFunc(_error);
                }
            });
        if (!ret)
        {
            return;
        }
        auto srcNodeID = _srcNodeID->data();
        auto destNodeID = _dstNodeID->data();
        m_proxy->tars_set_timeout(c_networkTimeout)
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
                m_callback(toBcosError(ret), nullptr, nullptr);
            }

        private:
            std::function<void(
                bcos::Error::Ptr, bcos::gateway::GatewayInfo::Ptr, bcos::gateway::GatewayInfosPtr)>
                m_callback;
        };
        auto ret = checkConnection(
            c_moduleName, "asyncGetPeers", m_proxy, [_callback](bcos::Error::Ptr _error) {
                if (_callback)
                {
                    _callback(_error, nullptr, nullptr);
                }
            });
        if (!ret)
        {
            return;
        }
        m_proxy->async_asyncGetPeers(new Callback(_callback));
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
        auto ret = checkConnection(c_moduleName, "asyncSendMessageByNodeIDs", m_proxy, nullptr);
        if (!ret)
        {
            return;
        }
        auto srcNodeID = _srcNodeID->data();
        m_proxy->async_asyncSendMessageByNodeIDs(nullptr, _groupID,
            std::vector<char>(srcNodeID.begin(), srcNodeID.end()), tarsNodeIDs,
            std::vector<char>(_payload.begin(), _payload.end()));
    }

    void asyncSendBroadcastMessage(bcos::protocol::NodeType _type, const std::string& _groupID,
        bcos::crypto::NodeIDPtr _srcNodeID, bcos::bytesConstRef _payload) override
    {
        auto ret = checkConnection(c_moduleName, "asyncSendBroadcastMessage", m_proxy, nullptr);
        if (!ret)
        {
            return;
        }
        auto srcNodeID = _srcNodeID->data();
        m_proxy->async_asyncSendBroadcastMessage(nullptr, _type, _groupID,
            std::vector<char>(srcNodeID.begin(), srcNodeID.end()),
            std::vector<char>(_payload.begin(), _payload.end()));
    }

    void asyncGetNodeIDs(
        const std::string& _groupID, bcos::gateway::GetNodeIDsFunc _getNodeIDsFunc) override
    {
        class Callback : public GatewayServicePrxCallback
        {
        public:
            Callback(
                bcos::gateway::GetNodeIDsFunc callback, bcos::crypto::KeyFactory::Ptr keyFactory)
              : m_callback(callback), m_keyFactory(keyFactory)
            {}
            void callback_asyncGetNodeIDs(
                const bcostars::Error& ret, const vector<vector<tars::Char>>& nodeIDs) override
            {
                auto bcosNodeIDs = std::make_shared<std::vector<bcos::crypto::NodeIDPtr>>();
                bcosNodeIDs->reserve(nodeIDs.size());
                for (auto const& it : nodeIDs)
                {
                    bcosNodeIDs->push_back(m_keyFactory->createKey(
                        bcos::bytesConstRef((bcos::byte*)it.data(), it.size())));
                }
                m_callback(toBcosError(ret), bcosNodeIDs);
            }
            void callback_asyncGetNodeIDs_exception(tars::Int32 ret) override
            {
                m_callback(toBcosError(ret), nullptr);
            }

        private:
            bcos::gateway::GetNodeIDsFunc m_callback;
            bcos::crypto::KeyFactory::Ptr m_keyFactory;
        };
        auto ret = checkConnection(
            c_moduleName, "asyncGetNodeIDs", m_proxy, [_getNodeIDsFunc](bcos::Error::Ptr _error) {
                if (_getNodeIDsFunc)
                {
                    _getNodeIDsFunc(_error, nullptr);
                }
            });
        if (!ret)
        {
            return;
        }
        m_proxy->async_asyncGetNodeIDs(new Callback(_getNodeIDsFunc, m_keyFactory), _groupID);
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
            c_moduleName, "asyncNotifyGroupInfo", m_proxy, [_callback](bcos::Error::Ptr _error) {
                if (_callback)
                {
                    _callback(std::move(_error));
                }
            });
        if (!ret)
        {
            return;
        }
        auto tarsGroupInfo = toTarsGroupInfo(_groupInfo);
        m_proxy->async_asyncNotifyGroupInfo(new Callback(_callback), tarsGroupInfo);
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
                auto data =
                    std::make_shared<bcos::bytes>(_responseData.begin(), _responseData.end());
                m_callback(toBcosError(ret), _type, data);
            }
            void callback_asyncSendMessageByTopic_exception(tars::Int32 ret) override
            {
                return m_callback(toBcosError(ret), 0, nullptr);
            }

        private:
            std::function<void(bcos::Error::Ptr&&, int16_t, bcos::bytesPointer)> m_callback;
        };
        auto ret = checkConnection(
            c_moduleName, "asyncSendMessageByTopic", m_proxy, [_respFunc](bcos::Error::Ptr _error) {
                if (_respFunc)
                {
                    _respFunc(std::move(_error), 0, nullptr);
                }
            });
        if (!ret)
        {
            return;
        }
        vector<tars::Char> tarsRequestData(_data.begin(), _data.end());
        m_proxy->tars_set_timeout(c_amopTimeout)
            ->async_asyncSendMessageByTopic(new Callback(_respFunc), _topic, tarsRequestData);
    }

    void asyncSendBroadbastMessageByTopic(
        const std::string& _topic, bcos::bytesConstRef _data) override
    {
        auto ret =
            checkConnection(c_moduleName, "asyncSendBroadbastMessageByTopic", m_proxy, nullptr);
        if (!ret)
        {
            return;
        }
        vector<tars::Char> tarsRequestData(_data.begin(), _data.end());
        m_proxy->async_asyncSendBroadbastMessageByTopic(nullptr, _topic, tarsRequestData);
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
                m_callback(toBcosError(ret));
            }
            void callback_asyncSubscribeTopic_exception(tars::Int32 ret) override
            {
                return m_callback(toBcosError(ret));
            }

        private:
            std::function<void(bcos::Error::Ptr&&)> m_callback;
        };
        auto ret = checkConnection(
            c_moduleName, "asyncSubscribeTopic", m_proxy, [_callback](bcos::Error::Ptr _error) {
                if (_callback)
                {
                    _callback(std::move(_error));
                }
            });
        if (!ret)
        {
            return;
        }
        m_proxy->async_asyncSubscribeTopic(new Callback(_callback), _clientID, _topicInfo);
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
                m_callback(toBcosError(ret));
            }
            void callback_asyncRemoveTopic_exception(tars::Int32 ret) override
            {
                return m_callback(toBcosError(ret));
            }

        private:
            std::function<void(bcos::Error::Ptr&&)> m_callback;
        };
        auto ret = checkConnection(
            c_moduleName, "asyncRemoveTopic", m_proxy, [_callback](bcos::Error::Ptr _error) {
                if (_callback)
                {
                    _callback(std::move(_error));
                }
            });
        if (!ret)
        {
            return;
        }
        m_proxy->async_asyncRemoveTopic(new Callback(_callback), _clientID, _topicList);
    }

    bcostars::GatewayServicePrx prx() { return m_proxy; }

protected:
    void start() override {}
    void stop() override {}

private:
    bcostars::GatewayServicePrx m_proxy;
    std::string m_gatewayServiceName;
    // Note: only useful for asyncGetNodeIDs
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    std::string const c_moduleName = "GatewayServiceClient";
    // AMOP timeout 40s
    const int c_amopTimeout = 40000;
    const int c_networkTimeout = 40000;
};
}  // namespace bcostars
