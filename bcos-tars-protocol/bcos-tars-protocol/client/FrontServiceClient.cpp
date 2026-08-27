#include "FrontServiceClient.h"
#include "bcos-tars-protocol/ErrorConverter.h"
#include <range/v3/view/any_view.hpp>

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
bcos::task::Task<bcos::front::SendResult> bcostars::FrontServiceClient::sendMessageByNodeID(
    int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
    ::ranges::any_view<bcos::bytesConstRef, ::ranges::category::forward> _payloads,
    uint32_t _timeout)
{
    struct SendAwaitable
    {
        struct CompletionState
        {
            std::atomic<bool> completed{false};
            std::coroutine_handle<> handle;
            bcos::front::SendResult result;
        };

        class Callback : public FrontServicePrxCallback
        {
        public:
            Callback(std::shared_ptr<CompletionState> state, FrontServiceClient* self)
              : m_state(std::move(state)), m_self(self)
            {}

            void callback_asyncSendMessageByNodeID(const bcostars::Error& ret,
                const std::vector<tars::Char>& responseNodeID,
                const std::vector<tars::Char>& responseData, const std::string& seq) override
            {
                complete(ret, responseNodeID, responseData, seq);
            }

            void callback_asyncSendMessageByNodeID_exception(tars::Int32 ret) override
            {
                bcos::front::SendResult result;
                result.error = toBcosError(ret);
                completeResult(std::move(result));
            }

        private:
            void complete(const bcostars::Error& ret,
                const std::vector<tars::Char>& responseNodeID,
                const std::vector<tars::Char>& responseData, const std::string& seq)
            {
                bcos::front::SendResult result;
                result.error = toBcosError(ret);
                if (!responseNodeID.empty())
                {
                    result.nodeID = m_self->m_keyFactory->createKey(bcos::bytesConstRef(
                        (const bcos::byte*)responseNodeID.data(), responseNodeID.size()));
                }
                result.payload.assign(responseData.begin(), responseData.end());
                result.uuid = seq;
                completeResult(std::move(result));
            }

            void completeResult(bcos::front::SendResult result)
            {
                if (!m_state->completed.exchange(true))
                {
                    m_state->result = std::move(result);
                    m_state->handle.resume();
                }
            }

            std::shared_ptr<CompletionState> m_state;
            FrontServiceClient* m_self;
        };

        FrontServiceClient* m_self;
        int m_moduleID;
        bcos::crypto::NodeIDPtr m_nodeID;
        std::shared_ptr<std::vector<char>> m_buffer;
        uint32_t m_timeout;
        std::shared_ptr<CompletionState> m_state;

        constexpr static bool await_ready() noexcept { return false; }

        void await_suspend(std::coroutine_handle<> _handle)
        {
            m_state->handle = _handle;
            auto state = m_state;
            auto nodeIDData = m_nodeID->data();
            m_self->m_proxy->tars_set_timeout(m_self->c_frontServiceTimeout)
                ->async_asyncSendMessageByNodeID(new Callback(state, m_self), m_moduleID,
                    std::vector<char>(nodeIDData.begin(), nodeIDData.end()), *m_buffer, m_timeout,
                    (m_timeout > 0));
        }

        bcos::front::SendResult await_resume() { return std::move(m_state->result); }
    };

    // materialise the joined payload directly as the std::vector<char> the RPC argument needs —
    // a single pass and one allocation, no second copy in await_suspend
    auto buffer = std::make_shared<std::vector<char>>();
    for (auto const& data : _payloads)
    {
        buffer->insert(buffer->end(), data.begin(), data.end());
    }
    SendAwaitable awaitable{this, _moduleID, std::move(_nodeID), std::move(buffer), _timeout,
        std::make_shared<SendAwaitable::CompletionState>()};
    co_return co_await awaitable;
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
bcos::task::Task<void> bcostars::FrontServiceClient::broadcastMessage(uint16_t _type,
    int _moduleID, ::ranges::any_view<bcos::bytesConstRef, ::ranges::category::forward> payloads)
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
