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
// One completion state machine for every front RPC. The tars PrxCallback completes the
// coroutine exactly once (completed dedups the success/exception callback pair). A single
// atomic rendezvous flag decides which side continues the coroutine: a completion that lands
// before await_suspend's exchange (a synchronous tars reject/exception fired on the caller's
// stack, or a fast reply) sets rendezvous first, so await_suspend observes it and returns false
// and the coroutine continues through await_resume on this stack; a later completion finds
// rendezvous already set and resumes. Neither side can both resume and decline to suspend, so
// the coroutine is never resumed from inside await_suspend (undefined behaviour) and never runs
// on two threads at once.
template <class Result>
struct TarsAwaitable
{
    struct CompletionState
    {
        std::atomic<bool> completed{false};
        std::atomic<bool> rendezvous{false};
        std::coroutine_handle<> handle;
        Result result;
    };

    class Callback : public bcostars::FrontServicePrxCallback
    {
    public:
        using OnError = std::function<Result(const bcostars::Error&)>;
        using OnException = std::function<Result(tars::Int32)>;
        using OnGroupNodeInfo =
            std::function<Result(const bcostars::Error&, const bcostars::GroupNodeInfo&)>;
        using OnSendResult = std::function<Result(const bcostars::Error&,
            const std::vector<tars::Char>&, const std::vector<tars::Char>&, const std::string&)>;

        // each call site wires only the converters of the RPC it issues; the rest stay empty and
        // their overrides can only fire if the invoker issued a different RPC than configured
        Callback(std::shared_ptr<CompletionState> state, OnError onError, OnException onException,
            OnGroupNodeInfo onGroupNodeInfo, OnSendResult onSendResult)
          : m_state(std::move(state)),
            m_onError(std::move(onError)),
            m_onException(std::move(onException)),
            m_onGroupNodeInfo(std::move(onGroupNodeInfo)),
            m_onSendResult(std::move(onSendResult))
        {}

        void callback_onReceiveGroupNodeInfo(const bcostars::Error& ret) override
        {
            complete(m_onError(ret));
        }
        void callback_onReceiveGroupNodeInfo_exception(tars::Int32 ret) override
        {
            complete(m_onException(ret));
        }
        void callback_onReceiveMessage(const bcostars::Error& ret) override
        {
            complete(m_onError(ret));
        }
        void callback_onReceiveMessage_exception(tars::Int32 ret) override
        {
            complete(m_onException(ret));
        }
        void callback_onReceiveBroadcastMessage(const bcostars::Error& ret) override
        {
            complete(m_onError(ret));
        }
        void callback_onReceiveBroadcastMessage_exception(tars::Int32 ret) override
        {
            complete(m_onException(ret));
        }
        void callback_asyncSendResponse(const bcostars::Error& ret) override
        {
            complete(m_onError(ret));
        }
        void callback_asyncSendResponse_exception(tars::Int32 ret) override
        {
            complete(m_onException(ret));
        }
        void callback_asyncGetGroupNodeInfo(
            const bcostars::Error& ret, const bcostars::GroupNodeInfo& groupNodeInfo) override
        {
            complete(m_onGroupNodeInfo(ret, groupNodeInfo));
        }
        void callback_asyncGetGroupNodeInfo_exception(tars::Int32 ret) override
        {
            complete(m_onException(ret));
        }
        void callback_asyncSendMessageByNodeID(const bcostars::Error& ret,
            const std::vector<tars::Char>& responseNodeID,
            const std::vector<tars::Char>& responseData, const std::string& seq) override
        {
            complete(m_onSendResult(ret, responseNodeID, responseData, seq));
        }
        void callback_asyncSendMessageByNodeID_exception(tars::Int32 ret) override
        {
            complete(m_onException(ret));
        }

    private:
        void complete(Result result)
        {
            if (!m_state->completed.exchange(true, std::memory_order_acq_rel))
            {
                m_state->result = std::move(result);
                // resume only when await_suspend has already exchanged: exactly one side
                // continues the coroutine, and never from inside await_suspend
                if (m_state->rendezvous.exchange(true, std::memory_order_acq_rel))
                {
                    m_state->handle.resume();
                }
            }
        }

        std::shared_ptr<CompletionState> m_state;
        OnError m_onError;
        OnException m_onException;
        OnGroupNodeInfo m_onGroupNodeInfo;
        OnSendResult m_onSendResult;
    };

    // issues the RPC; adopts the callback at the tars call (release()), so a throw before the
    // call cannot leak it
    std::function<void(std::unique_ptr<bcostars::FrontServicePrxCallback>)> m_invoker;
    std::shared_ptr<CompletionState> m_state;
    typename Callback::OnError m_onError;
    typename Callback::OnException m_onException;
    typename Callback::OnGroupNodeInfo m_onGroupNodeInfo;
    typename Callback::OnSendResult m_onSendResult;

    constexpr static bool await_ready() noexcept { return false; }

    // returns false (no suspension) when the RPC completed synchronously inside await_suspend:
    // the coroutine then continues on this stack through await_resume instead of being resumed
    // re-entrantly from within await_suspend (undefined behaviour)
    bool await_suspend(std::coroutine_handle<> _handle)
    {
        m_state->handle = _handle;
        try
        {
            m_invoker(std::make_unique<Callback>(
                m_state, m_onError, m_onException, m_onGroupNodeInfo, m_onSendResult));
        }
        catch (...)
        {
            // the RPC never left this stack and no callback can fire: swallow any late
            // completion and let the exception resume the coroutine here
            m_state->completed.store(true, std::memory_order_relaxed);
            throw;
        }
        // a completion that already landed set rendezvous first: do not suspend, continue
        // through await_resume on this stack
        return !m_state->rendezvous.exchange(true, std::memory_order_acq_rel);
    }

    Result await_resume() { return std::move(m_state->result); }
};

// the Error-returning RPCs (onReceive* and sendResponse): the tars error converts directly
using ErrorAwaitable = TarsAwaitable<bcos::Error::Ptr>;
using GetGroupNodeInfoAwaitable =
    TarsAwaitable<std::tuple<bcos::Error::Ptr, bcos::gateway::GroupNodeInfo::Ptr>>;
using SendAwaitable = TarsAwaitable<bcos::front::SendResult>;
}  // namespace

bcos::task::Task<std::tuple<bcos::Error::Ptr, bcos::gateway::GroupNodeInfo::Ptr>>
bcostars::FrontServiceClient::getGroupNodeInfo()
{
    co_return co_await GetGroupNodeInfoAwaitable{
        [proxy = m_proxy, timeout = c_frontServiceTimeout](
            std::unique_ptr<FrontServicePrxCallback> callback) mutable {
            proxy->tars_set_timeout(timeout)->async_asyncGetGroupNodeInfo(callback.release());
        },
        std::make_shared<GetGroupNodeInfoAwaitable::CompletionState>(), {},
        [](tars::Int32 ret) -> std::tuple<bcos::Error::Ptr, bcos::gateway::GroupNodeInfo::Ptr> {
            return {bcostars::toBcosError(ret), nullptr};
        },
        [](const bcostars::Error& ret, const bcostars::GroupNodeInfo& groupNodeInfo) {
            auto bcosGroupNodeInfo = std::make_shared<bcostars::protocol::GroupNodeInfoImpl>(
                [m_groupNodeInfo = groupNodeInfo]() mutable { return &m_groupNodeInfo; });
            return std::make_tuple(bcostars::toBcosError(ret), std::move(bcosGroupNodeInfo));
        },
        {}};
}
bcos::task::Task<bcos::Error::Ptr> bcostars::FrontServiceClient::onReceiveGroupNodeInfo(
    std::string _groupID, bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo)
{
    auto groupNodeInfoImpl =
        std::dynamic_pointer_cast<bcostars::protocol::GroupNodeInfoImpl>(_groupNodeInfo);
    auto tarsGroupNodeInfo = groupNodeInfoImpl->inner();
    co_return co_await ErrorAwaitable{
        [proxy = m_proxy, timeout = c_frontServiceTimeout, groupID = std::move(_groupID),
            tarsGroupNodeInfo](std::unique_ptr<FrontServicePrxCallback> callback) mutable {
            proxy->tars_set_timeout(timeout)
                ->async_onReceiveGroupNodeInfo(callback.release(), groupID, tarsGroupNodeInfo);
        },
        std::make_shared<ErrorAwaitable::CompletionState>(),
        [](const bcostars::Error& ret) { return bcostars::toBcosError(ret); },
        [](tars::Int32 ret) { return bcostars::toBcosError(ret); }, {}, {}};
}
bcos::task::Task<bcos::Error::Ptr> bcostars::FrontServiceClient::onReceiveMessage(
    std::string _groupID, bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data)
{
    auto nodeIDData = _nodeID->data();
    co_return co_await ErrorAwaitable{
        [proxy = m_proxy, timeout = c_frontServiceTimeout, groupID = std::move(_groupID),
            nodeID = std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            data = std::vector<char>(_data.begin(), _data.end())](
            std::unique_ptr<FrontServicePrxCallback> callback) mutable {
            proxy->tars_set_timeout(timeout)->async_onReceiveMessage(
                callback.release(), groupID, nodeID, data);
        },
        std::make_shared<ErrorAwaitable::CompletionState>(),
        [](const bcostars::Error& ret) { return bcostars::toBcosError(ret); },
        [](tars::Int32 ret) { return bcostars::toBcosError(ret); }, {}, {}};
}
bcos::task::Task<bcos::Error::Ptr> bcostars::FrontServiceClient::onReceiveBroadcastMessage(
    std::string _groupID, bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data)
{
    auto nodeIDData = _nodeID->data();
    co_return co_await ErrorAwaitable{
        [proxy = m_proxy, timeout = c_frontServiceTimeout, groupID = std::move(_groupID),
            nodeID = std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            data = std::vector<char>(_data.begin(), _data.end())](
            std::unique_ptr<FrontServicePrxCallback> callback) mutable {
            proxy->tars_set_timeout(timeout)
                ->async_onReceiveBroadcastMessage(callback.release(), groupID, nodeID, data);
        },
        std::make_shared<ErrorAwaitable::CompletionState>(),
        [](const bcostars::Error& ret) { return bcostars::toBcosError(ret); },
        [](tars::Int32 ret) { return bcostars::toBcosError(ret); }, {}, {}};
}
bcos::task::Task<bcos::front::SendResult> bcostars::FrontServiceClient::sendMessageByNodeID(
    int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
    ::ranges::any_view<bcos::bytesConstRef, ::ranges::category::forward> _payloads,
    uint32_t _timeout)
{
    // materialise the joined payload directly as the std::vector<char> the RPC argument needs —
    // a single pass and one allocation
    std::vector<char> buffer;
    for (auto const& data : _payloads)
    {
        buffer.insert(buffer.end(), data.begin(), data.end());
    }
    auto nodeIDData = _nodeID->data();
    co_return co_await SendAwaitable{
        [proxy = m_proxy, timeout = c_frontServiceTimeout, _moduleID,
            nodeID = std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            buffer = std::move(buffer), _timeout](
            std::unique_ptr<FrontServicePrxCallback> callback) mutable {
            proxy->tars_set_timeout(timeout)->async_asyncSendMessageByNodeID(callback.release(),
                _moduleID, nodeID, buffer, _timeout, (_timeout > 0));
        },
        std::make_shared<SendAwaitable::CompletionState>(), {},
        [](tars::Int32 ret) {
            bcos::front::SendResult result;
            result.error = bcostars::toBcosError(ret);
            return result;
        },
        {},
        // owns the keyFactory across the RPC: the callback may fire on the tars network thread
        // after the client would otherwise be gone
        [keyFactory = m_keyFactory](const bcostars::Error& ret,
            const std::vector<tars::Char>& responseNodeID,
            const std::vector<tars::Char>& responseData, const std::string& seq) {
            bcos::front::SendResult result;
            result.error = bcostars::toBcosError(ret);
            if (!responseNodeID.empty())
            {
                result.nodeID = keyFactory->createKey(bcos::bytesConstRef(
                    (const bcos::byte*)responseNodeID.data(), responseNodeID.size()));
            }
            result.payload.assign(responseData.begin(), responseData.end());
            result.uuid = seq;
            return result;
        }};
}
bcos::task::Task<bcos::Error::Ptr> bcostars::FrontServiceClient::sendResponse(
    std::string _id, int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
    bcos::bytesConstRef _data)
{
    auto nodeIDData = _nodeID->data();
    co_return co_await ErrorAwaitable{
        [proxy = m_proxy, timeout = c_frontServiceTimeout, id = std::move(_id), _moduleID,
            nodeID = std::vector<char>(nodeIDData.begin(), nodeIDData.end()),
            data = std::vector<char>(_data.begin(), _data.end())](
            std::unique_ptr<FrontServicePrxCallback> callback) mutable {
            proxy->tars_set_timeout(timeout)->async_asyncSendResponse(
                callback.release(), id, _moduleID, nodeID, data);
        },
        std::make_shared<ErrorAwaitable::CompletionState>(),
        [](const bcostars::Error& ret) { return bcostars::toBcosError(ret); },
        [](tars::Int32 ret) { return bcostars::toBcosError(ret); }, {}, {}};
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
