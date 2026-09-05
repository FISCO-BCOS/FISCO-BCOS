#include "FrontServiceClient.h"
#include "bcos-tars-protocol/ErrorConverter.h"
#include <range/v3/view/any_view.hpp>

void bcostars::FrontServiceClient::start() {}
void bcostars::FrontServiceClient::stop() {}
bcostars::FrontServiceClient::FrontServiceClient(
    bcostars::FrontServicePrx proxy, bcos::crypto::KeyFactory::Ptr keyFactory)
  : m_proxy(proxy), m_keyFactory(keyFactory)
{}
namespace
{
// shared awaitable for the Error-returning RPCs (onReceive* and sendResponse): the Callback
// completes the coroutine exactly once (via exchange) with the tars error converted to a bcos error
struct ErrorAwaitable
{
    struct CompletionState
    {
        std::atomic<bool> completed{false};
        // true only while await_suspend is still executing: complete() must never resume the
        // coroutine from inside await_suspend — resuming a coroutine that is still executing
        // await_suspend is undefined behaviour. A synchronous completion (tars local reject /
        // exception fired on the caller's stack before the request leaves it) only records the
        // result here; await_suspend then observes the completed state and returns false, so the
        // coroutine continues through await_resume on this stack instead of being resumed
        // re-entrantly.
        std::atomic<bool> inAwaitSuspend{false};
        std::coroutine_handle<> handle;
        bcos::Error::Ptr error;
    };

    class Callback : public bcostars::FrontServicePrxCallback
    {
    public:
        explicit Callback(std::shared_ptr<CompletionState> state) : m_state(std::move(state)) {}

        void callback_onReceiveGroupNodeInfo(const bcostars::Error& ret) override
        {
            complete(bcostars::toBcosError(ret));
        }
        void callback_onReceiveGroupNodeInfo_exception(tars::Int32 ret) override
        {
            complete(bcostars::toBcosError(ret));
        }
        void callback_onReceiveMessage(const bcostars::Error& ret) override
        {
            complete(bcostars::toBcosError(ret));
        }
        void callback_onReceiveMessage_exception(tars::Int32 ret) override
        {
            complete(bcostars::toBcosError(ret));
        }
        void callback_onReceiveBroadcastMessage(const bcostars::Error& ret) override
        {
            complete(bcostars::toBcosError(ret));
        }
        void callback_onReceiveBroadcastMessage_exception(tars::Int32 ret) override
        {
            complete(bcostars::toBcosError(ret));
        }
        void callback_asyncSendResponse(const bcostars::Error& ret) override
        {
            complete(bcostars::toBcosError(ret));
        }
        void callback_asyncSendResponse_exception(tars::Int32 ret) override
        {
            complete(bcostars::toBcosError(ret));
        }

    private:
        void complete(bcos::Error::Ptr error)
        {
            if (!m_state->completed.exchange(true))
            {
                m_state->error = std::move(error);
                // a completion arriving while await_suspend is still on this stack must not resume
                // the coroutine re-entrantly (see CompletionState::inAwaitSuspend): await_suspend
                // observes the completed state and returns false instead
                if (!m_state->inAwaitSuspend.load(std::memory_order_acquire))
                {
                    m_state->handle.resume();
                }
            }
        }
        std::shared_ptr<CompletionState> m_state;
    };

    std::shared_ptr<CompletionState> m_state;
    // issues the RPC with the given callback (owns the marshaled arguments)
    std::function<void(bcostars::FrontServicePrxCallback*)> m_invoker;

    constexpr static bool await_ready() noexcept { return false; }

    // returns false (no suspension) if the RPC completed synchronously inside await_suspend, so
    // the coroutine is never resumed from within await_suspend (undefined behaviour) — it then
    // continues on this stack through await_resume. Returns true once the RPC is in flight and
    // the completion callback will fire on the tars network thread.
    bool await_suspend(std::coroutine_handle<> _handle)
    {
        m_state->handle = _handle;
        m_state->inAwaitSuspend.store(true, std::memory_order_release);
        try
        {
            m_invoker(new Callback(m_state));
        }
        catch (...)
        {
            // the RPC was never issued: clear the guard before the exception resumes the coroutine
            m_state->inAwaitSuspend.store(false, std::memory_order_release);
            throw;
        }
        m_state->inAwaitSuspend.store(false, std::memory_order_release);
        return !m_state->completed.load(std::memory_order_acquire);
    }

    bcos::Error::Ptr await_resume() { return std::move(m_state->error); }
};
}  // namespace

bcos::task::Task<std::tuple<bcos::Error::Ptr, bcos::gateway::GroupNodeInfo::Ptr>>
bcostars::FrontServiceClient::getGroupNodeInfo()
{
    struct GetGroupNodeInfoAwaitable
    {
        struct CompletionState
        {
            std::atomic<bool> completed{false};
            // see ErrorAwaitable::CompletionState::inAwaitSuspend: complete() must not resume the
            // coroutine from inside await_suspend
            std::atomic<bool> inAwaitSuspend{false};
            std::coroutine_handle<> handle;
            bcos::Error::Ptr error;
            bcos::gateway::GroupNodeInfo::Ptr groupNodeInfo;
        };

        class Callback : public FrontServicePrxCallback
        {
        public:
            explicit Callback(std::shared_ptr<CompletionState> state) : m_state(std::move(state))
            {}

            void callback_asyncGetGroupNodeInfo(
                const bcostars::Error& ret, const GroupNodeInfo& groupNodeInfo) override
            {
                auto bcosGroupNodeInfo = std::make_shared<bcostars::protocol::GroupNodeInfoImpl>(
                    [m_groupNodeInfo = groupNodeInfo]() mutable { return &m_groupNodeInfo; });
                complete(toBcosError(ret), std::move(bcosGroupNodeInfo));
            }
            void callback_asyncGetGroupNodeInfo_exception(tars::Int32 ret) override
            {
                complete(toBcosError(ret), nullptr);
            }

        private:
            void complete(bcos::Error::Ptr error, bcos::gateway::GroupNodeInfo::Ptr groupNodeInfo)
            {
                if (!m_state->completed.exchange(true))
                {
                    m_state->error = std::move(error);
                    m_state->groupNodeInfo = std::move(groupNodeInfo);
                    // see CompletionState::inAwaitSuspend: no resume from inside await_suspend
                    if (!m_state->inAwaitSuspend.load(std::memory_order_acquire))
                    {
                        m_state->handle.resume();
                    }
                }
            }
            std::shared_ptr<CompletionState> m_state;
        };

        FrontServiceClient* m_self;
        std::shared_ptr<CompletionState> m_state;

        constexpr static bool await_ready() noexcept { return false; }

        // see ErrorAwaitable::await_suspend for why this returns false on a synchronous
        // completion instead of resuming from inside await_suspend
        bool await_suspend(std::coroutine_handle<> _handle)
        {
            m_state->handle = _handle;
            m_state->inAwaitSuspend.store(true, std::memory_order_release);
            try
            {
                m_self->m_proxy->tars_set_timeout(m_self->c_frontServiceTimeout)
                    ->async_asyncGetGroupNodeInfo(new Callback(m_state));
            }
            catch (...)
            {
                m_state->inAwaitSuspend.store(false, std::memory_order_release);
                throw;
            }
            m_state->inAwaitSuspend.store(false, std::memory_order_release);
            return !m_state->completed.load(std::memory_order_acquire);
        }

        std::tuple<bcos::Error::Ptr, bcos::gateway::GroupNodeInfo::Ptr> await_resume()
        {
            return std::make_tuple(std::move(m_state->error), std::move(m_state->groupNodeInfo));
        }
    };

    GetGroupNodeInfoAwaitable awaitable{
        this, std::make_shared<GetGroupNodeInfoAwaitable::CompletionState>()};
    co_return co_await awaitable;
}
bcos::task::Task<bcos::Error::Ptr> bcostars::FrontServiceClient::onReceiveGroupNodeInfo(
    std::string _groupID, bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo)
{
    auto groupNodeInfoImpl =
        std::dynamic_pointer_cast<bcostars::protocol::GroupNodeInfoImpl>(_groupNodeInfo);
    auto tarsGroupNodeInfo = groupNodeInfoImpl->inner();
    co_return co_await ErrorAwaitable{std::make_shared<ErrorAwaitable::CompletionState>(),
        [proxy = m_proxy, timeout = c_frontServiceTimeout, groupID = std::move(_groupID),
            tarsGroupNodeInfo](FrontServicePrxCallback* callback) mutable {
            proxy->tars_set_timeout(timeout)
                ->async_onReceiveGroupNodeInfo(callback, groupID, tarsGroupNodeInfo);
        }};
}
bcos::task::Task<bcos::Error::Ptr> bcostars::FrontServiceClient::onReceiveMessage(
    std::string _groupID, bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data)
{
    auto nodeIDData = _nodeID->data();
    co_return co_await ErrorAwaitable{std::make_shared<ErrorAwaitable::CompletionState>(),
        [proxy = m_proxy, timeout = c_frontServiceTimeout, groupID = std::move(_groupID),
            nodeID = std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            data = std::vector<char>(_data.begin(), _data.end())](
            FrontServicePrxCallback* callback) mutable {
            proxy->tars_set_timeout(timeout)->async_onReceiveMessage(callback, groupID, nodeID,
                data);
        }};
}
bcos::task::Task<bcos::Error::Ptr> bcostars::FrontServiceClient::onReceiveBroadcastMessage(
    std::string _groupID, bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data)
{
    auto nodeIDData = _nodeID->data();
    co_return co_await ErrorAwaitable{std::make_shared<ErrorAwaitable::CompletionState>(),
        [proxy = m_proxy, timeout = c_frontServiceTimeout, groupID = std::move(_groupID),
            nodeID = std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            data = std::vector<char>(_data.begin(), _data.end())](
            FrontServicePrxCallback* callback) mutable {
            proxy->tars_set_timeout(timeout)
                ->async_onReceiveBroadcastMessage(callback, groupID, nodeID, data);
        }};
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
            // see ErrorAwaitable::CompletionState::inAwaitSuspend: complete() must not resume the
            // coroutine from inside await_suspend
            std::atomic<bool> inAwaitSuspend{false};
            std::coroutine_handle<> handle;
            bcos::front::SendResult result;
        };

        class Callback : public FrontServicePrxCallback
        {
        public:
            // owns the keyFactory across the RPC (the callback may fire on the tars network
            // thread after the client would otherwise be gone): no raw FrontServiceClient* is held
            Callback(std::shared_ptr<CompletionState> state,
                bcos::crypto::KeyFactory::Ptr keyFactory)
              : m_state(std::move(state)), m_keyFactory(std::move(keyFactory))
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
                    result.nodeID = m_keyFactory->createKey(bcos::bytesConstRef(
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
                    // see CompletionState::inAwaitSuspend: no resume from inside await_suspend
                    if (!m_state->inAwaitSuspend.load(std::memory_order_acquire))
                    {
                        m_state->handle.resume();
                    }
                }
            }

            std::shared_ptr<CompletionState> m_state;
            bcos::crypto::KeyFactory::Ptr m_keyFactory;
        };

        FrontServiceClient* m_self;
        int m_moduleID;
        bcos::crypto::NodeIDPtr m_nodeID;
        std::shared_ptr<std::vector<char>> m_buffer;
        uint32_t m_timeout;
        std::shared_ptr<CompletionState> m_state;

        constexpr static bool await_ready() noexcept { return false; }

        // see ErrorAwaitable::await_suspend for why this returns false on a synchronous
        // completion instead of resuming from inside await_suspend
        bool await_suspend(std::coroutine_handle<> _handle)
        {
            m_state->handle = _handle;
            auto state = m_state;
            auto nodeIDData = m_nodeID->data();
            m_state->inAwaitSuspend.store(true, std::memory_order_release);
            try
            {
                m_self->m_proxy->tars_set_timeout(m_self->c_frontServiceTimeout)
                    ->async_asyncSendMessageByNodeID(
                        new Callback(state, m_self->m_keyFactory), m_moduleID,
                        std::vector<char>(nodeIDData.begin(), nodeIDData.end()), *m_buffer,
                        m_timeout, (m_timeout > 0));
            }
            catch (...)
            {
                m_state->inAwaitSuspend.store(false, std::memory_order_release);
                throw;
            }
            m_state->inAwaitSuspend.store(false, std::memory_order_release);
            return !m_state->completed.load(std::memory_order_acquire);
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
bcos::task::Task<bcos::Error::Ptr> bcostars::FrontServiceClient::sendResponse(
    std::string _id, int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
    bcos::bytesConstRef _data)
{
    auto nodeIDData = _nodeID->data();
    co_return co_await ErrorAwaitable{std::make_shared<ErrorAwaitable::CompletionState>(),
        [proxy = m_proxy, timeout = c_frontServiceTimeout, id = std::move(_id), _moduleID,
            nodeID = std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            data = std::vector<char>(_data.begin(), _data.end())](
            FrontServicePrxCallback* callback) mutable {
            proxy->tars_set_timeout(timeout)->async_asyncSendResponse(callback, id, _moduleID,
                nodeID, data);
        }};
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
