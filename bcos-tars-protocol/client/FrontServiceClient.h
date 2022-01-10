#pragma once

#include "bcos-tars-protocol/Common.h"
#include "bcos-tars-protocol/ErrorConverter.h"
#include "bcos-tars-protocol/tars/FrontService.h"
#include <bcos-framework/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/interfaces/front/FrontServiceInterface.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/RefDataContainer.h>

namespace bcostars
{
class FrontServiceClient : public bcos::front::FrontServiceInterface
{
public:
    void start() override {}
    void stop() override {}

    FrontServiceClient(bcostars::FrontServicePrx proxy, bcos::crypto::KeyFactory::Ptr keyFactory)
      : m_proxy(proxy), m_keyFactory(keyFactory)
    {}

    void asyncGetNodeIDs(bcos::front::GetNodeIDsFunc _getNodeIDsFunc) override
    {
        class Callback : public FrontServicePrxCallback
        {
        public:
            Callback(bcos::front::GetNodeIDsFunc callback, FrontServiceClient* self)
              : m_callback(callback), m_self(self)
            {}
            void callback_asyncGetNodeIDs(
                const bcostars::Error& ret, const vector<vector<tars::Char>>& nodeIDs) override
            {
                auto bcosNodeIDs = std::make_shared<std::vector<bcos::crypto::NodeIDPtr>>();
                bcosNodeIDs->reserve(nodeIDs.size());
                for (auto const& it : nodeIDs)
                {
                    bcosNodeIDs->push_back(m_self->m_keyFactory->createKey(
                        bcos::bytesConstRef((bcos::byte*)it.data(), it.size())));
                }

                m_callback(toBcosError(ret), bcosNodeIDs);
            }
            void callback_asyncGetNodeIDs_exception(tars::Int32 ret) override
            {
                m_callback(toBcosError(ret), nullptr);
            }

        private:
            bcos::front::GetNodeIDsFunc m_callback;
            FrontServiceClient* m_self;
        };

        m_proxy->async_asyncGetNodeIDs(new Callback(_getNodeIDsFunc, this));
    }

    void onReceiveNodeIDs(const std::string& _groupID,
        std::shared_ptr<const bcos::crypto::NodeIDs> _nodeIDs,
        bcos::front::ReceiveMsgFunc _receiveMsgCallback) override
    {
        class Callback : public FrontServicePrxCallback
        {
        public:
            Callback(bcos::front::ReceiveMsgFunc callback) : m_callback(callback) {}

            void callback_onReceivedNodeIDs(const bcostars::Error& ret) override
            {
                if (!m_callback)
                {
                    return;
                }
                m_callback(toBcosError(ret));
            }

            void callback_onReceivedNodeIDs_exception(tars::Int32 ret) override
            {
                if (!m_callback)
                {
                    return;
                }
                m_callback(toBcosError(ret));
            }

        private:
            bcos::front::ReceiveMsgFunc m_callback;
        };

        std::vector<std::vector<char>> tarsNodeIDs;
        tarsNodeIDs.reserve(_nodeIDs->size());
        for (auto const& it : *_nodeIDs)
        {
            auto nodeIDData = it->data();
            tarsNodeIDs.push_back(std::vector<char>(nodeIDData.begin(), nodeIDData.end()));
        }

        m_proxy->async_onReceivedNodeIDs(new Callback(_receiveMsgCallback), _groupID, tarsNodeIDs);
    }

    void onReceiveMessage(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::bytesConstRef _data, bcos::front::ReceiveMsgFunc _receiveMsgCallback) override
    {
        class Callback : public FrontServicePrxCallback
        {
        public:
            Callback(bcos::front::ReceiveMsgFunc callback) : m_callback(callback) {}

            void callback_onReceiveMessage(const bcostars::Error& ret) override
            {
                if (!m_callback)
                {
                    return;
                }
                m_callback(toBcosError(ret));
            }

            void callback_onReceiveMessage_exception(tars::Int32 ret) override
            {
                if (!m_callback)
                {
                    return;
                }
                m_callback(toBcosError(ret));
            }

        private:
            bcos::front::ReceiveMsgFunc m_callback;
        };
        auto ret = checkConnection(c_moduleName, "onReceiveMessage", m_proxy,
            [_receiveMsgCallback](bcos::Error::Ptr _error) {
                if (_receiveMsgCallback)
                {
                    _receiveMsgCallback(_error);
                }
            });
        if (!ret)
        {
            return;
        }
        auto nodeIDData = _nodeID->data();
        m_proxy->async_onReceiveMessage(new Callback(_receiveMsgCallback), _groupID,
            std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            std::vector<char>(_data.begin(), _data.end()));
    }

    // Note: the _receiveMsgCallback maybe null in some cases
    void onReceiveBroadcastMessage(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::bytesConstRef _data, bcos::front::ReceiveMsgFunc _receiveMsgCallback) override
    {
        class Callback : public FrontServicePrxCallback
        {
        public:
            Callback(bcos::front::ReceiveMsgFunc callback) : m_callback(callback) {}

            void callback_onReceiveBroadcastMessage(const bcostars::Error& ret) override
            {
                if (!m_callback)
                {
                    return;
                }
                m_callback(toBcosError(ret));
            }

            void callback_onReceiveBroadcastMessage_exception(tars::Int32 ret) override
            {
                if (!m_callback)
                {
                    return;
                }
                m_callback(toBcosError(ret));
            }

        private:
            bcos::front::ReceiveMsgFunc m_callback;
        };
        auto ret = checkConnection(c_moduleName, "onReceiveBroadcastMessage", m_proxy,
            [_receiveMsgCallback](bcos::Error::Ptr _error) {
                if (_receiveMsgCallback)
                {
                    _receiveMsgCallback(_error);
                }
            });
        if (!ret)
        {
            return;
        }
        auto nodeIDData = _nodeID->data();
        m_proxy->async_onReceiveBroadcastMessage(new Callback(_receiveMsgCallback), _groupID,
            std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            std::vector<char>(_data.begin(), _data.end()));
    }

    // Note: the _callback maybe null in some cases
    void asyncSendMessageByNodeID(int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::bytesConstRef _data, uint32_t _timeout, bcos::front::CallbackFunc _callback) override
    {
        class Callback : public FrontServicePrxCallback
        {
        public:
            Callback(bcos::front::CallbackFunc callback, FrontServiceClient* self)
              : m_callback(callback), m_self(self)
            {}

            void callback_asyncSendMessageByNodeID(const bcostars::Error& ret,
                const vector<tars::Char>& responseNodeID, const vector<tars::Char>& responseData,
                const std::string& seq) override
            {
                if (!m_callback)
                {
                    return;
                }
                auto bcosNodeID = m_self->m_keyFactory->createKey(
                    bcos::bytesConstRef((bcos::byte*)responseNodeID.data(), responseNodeID.size()));
                m_callback(toBcosError(ret), bcosNodeID,
                    bcos::bytesConstRef((bcos::byte*)responseData.data(), responseData.size()), seq,
                    bcos::front::ResponseFunc());
            }

            void callback_asyncSendMessageByNodeID_exception(tars::Int32 ret) override
            {
                if (!m_callback)
                {
                    return;
                }
                m_callback(toBcosError(ret), nullptr, bcos::bytesConstRef(), "",
                    bcos::front::ResponseFunc());
            }

        private:
            bcos::front::CallbackFunc m_callback;
            FrontServiceClient* m_self;
        };

        auto nodeIDData = _nodeID->data();
        m_proxy->async_asyncSendMessageByNodeID(new Callback(_callback, this), _moduleID,
            std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            std::vector<char>(_data.begin(), _data.end()), _timeout, (_callback ? true : false));
    }

    void asyncSendResponse(const std::string& _id, int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::bytesConstRef _data, bcos::front::ReceiveMsgFunc _receiveMsgCallback) override
    {
        auto ret = checkConnection(c_moduleName, "asyncSendResponse", m_proxy,
            [_receiveMsgCallback](bcos::Error::Ptr _error) {
                if (_receiveMsgCallback)
                {
                    _receiveMsgCallback(_error);
                }
            });
        if (!ret)
        {
            return;
        }
        auto nodeIDData = _nodeID->data();
        m_proxy->asyncSendResponse(_id, _moduleID,
            std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            std::vector<char>(_data.begin(), _data.end()));
    }

    void asyncSendMessageByNodeIDs(int _moduleID,
        const std::vector<bcos::crypto::NodeIDPtr>& _nodeIDs, bcos::bytesConstRef _data) override
    {
        std::vector<std::vector<char>> tarsNodeIDs;
        tarsNodeIDs.reserve(_nodeIDs.size());
        for (auto const& it : _nodeIDs)
        {
            auto nodeIDData = it->data();
            tarsNodeIDs.emplace_back(nodeIDData.begin(), nodeIDData.end());
        }
        m_proxy->async_asyncSendMessageByNodeIDs(
            nullptr, _moduleID, tarsNodeIDs, std::vector<char>(_data.begin(), _data.end()));
    }

    void asyncSendBroadcastMessage(
        uint16_t _type, int _moduleID, bcos::bytesConstRef _data) override
    {
        auto data = _data.toBytes();
        m_proxy->async_asyncSendBroadcastMessage(
            nullptr, _type, _moduleID, std::vector<char>(data.begin(), data.end()));
    }

private:
    bcostars::FrontServicePrx m_proxy;
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    std::string const c_moduleName = "FrontServiceClient";
};
}  // namespace bcostars