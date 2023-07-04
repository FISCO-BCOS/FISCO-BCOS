#pragma once

#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-rpc/jsonrpc/Common.h"
#include "bcos-tars-protocol/tars/RPC.h"
#include <servant/Communicator.h>
#include <any>

namespace bcos::sdk
{

class CompletionQueue
{
public:
    CompletionQueue() = default;
    CompletionQueue(const CompletionQueue&) = default;
    CompletionQueue& operator=(const CompletionQueue&) = default;
    CompletionQueue(CompletionQueue&&) = default;
    CompletionQueue& operator=(CompletionQueue&&) = default;

    virtual ~CompletionQueue() noexcept = default;
    virtual void notify(std::any tag) = 0;
};

class TarsCallback : public bcostars::RPCPrxCallback
{
private:
    CompletionQueue* m_completionQueue = nullptr;
    std::any m_tag;
    std::promise<tars::ReqMessagePtr> m_promise;
    std::variant<long, protocol::TransactionReceipt::Ptr> m_response;

public:
    TarsCallback(
        CompletionQueue* completionQueue, std::any tag, std::promise<tars::ReqMessagePtr> promise);
    TarsCallback(TarsCallback const&) = delete;
    TarsCallback& operator=(TarsCallback const&) = delete;
    TarsCallback(TarsCallback&&) = default;
    TarsCallback& operator=(TarsCallback&&) = default;
    ~TarsCallback() noexcept override = default;

    CompletionQueue* completionQueue();
    std::any& tag();
    std::promise<tars::ReqMessagePtr>& promise();

    template <class Response>
    Response getResponse() &&
    {
        return std::move(std::get<Response>(m_response));
    }

    void callback_sendTransaction(
        bcostars::Error const& error, bcostars::TransactionReceipt const& response) override;
    void callback_sendTransaction_exception(tars::Int32 ret) override;

    void callback_blockNumber(bcostars::Error const& error, tars::Int64 response) override;
    void callback_blockNumber_exception(tars::Int32 ret) override;
};

template <class Response>
class Future
{
private:
    std::future<tars::ReqMessagePtr> m_future;

public:
    Future() = default;
    Future(std::future<tars::ReqMessagePtr> future) : m_future(std::move(future)) {}
    Future(Future const&) = delete;
    Future& operator=(Future const&) = delete;
    Future(Future&&) noexcept = default;
    Future& operator=(Future&&) noexcept = default;
    ~Future() noexcept = default;

    Response get()
    {
        auto message = m_future.get();
        message->callback->dispatch(message);

        auto& tarsCallback = dynamic_cast<TarsCallback&>(*message->callback.get());
        return std::move(tarsCallback).getResponse<Response>();
    }
    void wait() { m_future.wait(); }
    template <typename Rep, typename Period>
    std::future_status waitFor(const std::chrono::duration<Rep, Period>& rel)
    {
        return m_future.wait_for(rel);
    }
    template <typename Clock, typename Duration>
    std::future_status waitUntil(const std::chrono::time_point<Clock, Duration>& abs)
    {
        return m_future.wait_until(abs);
    }
};

class RPCClient
{
private:
    tars::Communicator m_communicator;
    bcostars::RPCPrx m_rpcProxy;

    static void onMessage(tars::ReqMessagePtr message);

public:
    RPCClient(std::vector<std::string> endpoints, std::string locator = "") {}
    RPCClient(const std::string& connectionString);

    Future<bcos::protocol::TransactionReceipt::Ptr> sendTransaction(
        const bcos::protocol::Transaction& transaction, CompletionQueue* completionQueue = nullptr,
        std::any tag = {});
    Future<long> blockNumber(CompletionQueue* completionQueue = nullptr, std::any tag = {});
};
}  // namespace bcos::sdk