#include "FrontServiceClient.h"
#include "bcos-tars-protocol/ErrorConverter.h"

void bcostars::FrontServiceClient::start() {}
void bcostars::FrontServiceClient::stop() {}
bcostars::FrontServiceClient::FrontServiceClient(
    bcostars::FrontServicePrx proxy, bcos::crypto::KeyFactory::Ptr keyFactory)
  : m_proxy(proxy), m_keyFactory(keyFactory)
{}
void bcostars::FrontServiceClient::asyncGetGroupNodeInfo(
    bcos::front::GetGroupNodeInfoFunc _onGetGroupNodeInfo)
{
    class Callback : public FrontServicePrxCallback
    {
    public:
        Callback(bcos::front::GetGroupNodeInfoFunc callback, FrontServiceClient* self)
          : m_callback(callback)
        {}
        void callback_asyncGetGroupNodeInfo(
            const bcostars::Error& ret, const GroupNodeInfo& groupNodeInfo) override
        {
            auto bcosGroupNodeInfo = std::make_shared<bcostars::protocol::GroupNodeInfoImpl>(
                [m_groupNodeInfo = groupNodeInfo]() mutable { return &m_groupNodeInfo; });
            m_callback(toBcosError(ret), bcosGroupNodeInfo);
        }
        void callback_asyncGetGroupNodeInfo_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), nullptr);
        }

    private:
        bcos::front::GetGroupNodeInfoFunc m_callback;
    };

    m_proxy->tars_set_timeout(c_frontServiceTimeout)
        ->async_asyncGetGroupNodeInfo(new Callback(_onGetGroupNodeInfo, this));
}
void bcostars::FrontServiceClient::onReceiveGroupNodeInfo(const std::string& _groupID,
    bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo,
    bcos::front::ReceiveMsgFunc _receiveMsgCallback)
{
    class Callback : public FrontServicePrxCallback
    {
    public:
        Callback(bcos::front::ReceiveMsgFunc callback) : m_callback(callback) {}

        void callback_onReceiveGroupNodeInfo(const bcostars::Error& ret) override
        {
            if (!m_callback)
            {
                return;
            }
            m_callback(toBcosError(ret));
        }

        void callback_onReceiveGroupNodeInfo_exception(tars::Int32 ret) override
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
    auto groupNodeInfoImpl =
        std::dynamic_pointer_cast<bcostars::protocol::GroupNodeInfoImpl>(_groupNodeInfo);
    auto tarsGroupNodeInfo = groupNodeInfoImpl->inner();
    m_proxy->tars_set_timeout(c_frontServiceTimeout)
        ->async_onReceiveGroupNodeInfo(
            new Callback(_receiveMsgCallback), _groupID, tarsGroupNodeInfo);
}
void bcostars::FrontServiceClient::onReceiveMessage(const std::string& _groupID,
    const bcos::crypto::NodeIDPtr& _nodeID, bcos::bytesConstRef _data,
    bcos::front::ReceiveMsgFunc _receiveMsgCallback)
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
    auto nodeIDData = _nodeID->data();
    m_proxy->tars_set_timeout(c_frontServiceTimeout)
        ->async_onReceiveMessage(new Callback(_receiveMsgCallback), _groupID,
            std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            std::vector<char>(_data.begin(), _data.end()));
}
void bcostars::FrontServiceClient::onReceiveBroadcastMessage(const std::string& _groupID,
    bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data,
    bcos::front::ReceiveMsgFunc _receiveMsgCallback)
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
    auto nodeIDData = _nodeID->data();
    m_proxy->tars_set_timeout(c_frontServiceTimeout)
        ->async_onReceiveBroadcastMessage(new Callback(_receiveMsgCallback), _groupID,
            std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            std::vector<char>(_data.begin(), _data.end()));
}
void bcostars::FrontServiceClient::asyncSendMessageByNodeID(int _moduleID,
    bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data, uint32_t _timeout,
    bcos::front::CallbackFunc _callback)
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
            m_callback(
                toBcosError(ret), nullptr, bcos::bytesConstRef(), "", bcos::front::ResponseFunc());
        }

    private:
        bcos::front::CallbackFunc m_callback;
        FrontServiceClient* m_self;
    };

    auto nodeIDData = _nodeID->data();
    m_proxy->tars_set_timeout(c_frontServiceTimeout)
        ->async_asyncSendMessageByNodeID(new Callback(_callback, this), _moduleID,
            std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            std::vector<char>(_data.begin(), _data.end()), _timeout, (_callback ? true : false));
}
void bcostars::FrontServiceClient::asyncSendResponse(const std::string& _id, int _moduleID,
    bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data,
    bcos::front::ReceiveMsgFunc _receiveMsgCallback)
{
    auto nodeIDData = _nodeID->data();
    m_proxy->tars_set_timeout(c_frontServiceTimeout)
        ->asyncSendResponse(_id, _moduleID, std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            std::vector<char>(_data.begin(), _data.end()));
}
void bcostars::FrontServiceClient::asyncSendMessageByNodeIDs(
    int _moduleID, const std::vector<bcos::crypto::NodeIDPtr>& _nodeIDs, bcos::bytesConstRef _data)
{
    std::vector<std::vector<char>> tarsNodeIDs;
    tarsNodeIDs.reserve(_nodeIDs.size());
    for (auto const& it : _nodeIDs)
    {
        auto nodeIDData = it->data();
        tarsNodeIDs.emplace_back(nodeIDData.begin(), nodeIDData.end());
    }
    m_proxy->tars_set_timeout(c_frontServiceTimeout)
        ->async_asyncSendMessageByNodeIDs(
            nullptr, _moduleID, tarsNodeIDs, std::vector<char>(_data.begin(), _data.end()));
}
void bcostars::FrontServiceClient::asyncSendBroadcastMessage(
    uint16_t _type, int _moduleID, bcos::bytesConstRef _data)
{
    auto data = _data.toBytes();
    m_proxy->tars_set_timeout(c_frontServiceTimeout)
        ->async_asyncSendBroadcastMessage(
            nullptr, _type, _moduleID, std::vector<char>(data.begin(), data.end()));
}
bcos::task::Task<void> bcostars::FrontServiceClient::broadcastMessage(
    uint16_t _type, int _moduleID, ::ranges::any_view<bcos::bytesConstRef> payloads)
{
    std::vector<char> data;
    for (auto payload : payloads)
    {
        data.insert(data.end(), payload.begin(), payload.end());
    }
    m_proxy->tars_set_timeout(c_frontServiceTimeout)
        ->async_asyncSendBroadcastMessage(nullptr, _type, _moduleID, data);
    co_return;
}
