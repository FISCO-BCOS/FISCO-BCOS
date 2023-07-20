#pragma once

#include "bcos-framework/protocol/Transaction.h"
#include "bcos-tars-protocol/tars/RPC.h"
#include "detail/Core.h"
#include <servant/Communicator.h>

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

template <class Response>
class Future
{
private:
    std::shared_future<tars::ReqMessagePtr> m_future;

public:
    Future() = default;
    Future(std::shared_future<tars::ReqMessagePtr> future) : m_future(std::move(future)) {}
    Future(Future const&) = default;
    Future& operator=(Future const&) = default;
    Future(Future&&) noexcept = default;
    Future& operator=(Future&&) noexcept = default;
    ~Future() noexcept = default;

    Response get()
    {
        auto message = m_future.get();
        message->callback->dispatch(message);

        auto& tarsCallback = dynamic_cast<detail::TarsCallback&>(*message->callback.get());
        return std::move(tarsCallback).getResponse<Response>();
    }
    void waitFor() { m_future.wait(); }
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
    RPCClient(const std::string& connectionString);

    Future<bcos::protocol::TransactionReceipt::Ptr> sendTransaction(
        const bcos::protocol::Transaction& transaction, CompletionQueue* completionQueue = nullptr,
        std::any tag = {});
    Future<bcos::protocol::TransactionReceipt::Ptr> sendEncodedTransaction(
        const std::vector<char>& encodedBuffer, CompletionQueue* completionQueue = nullptr,
        std::any tag = {});
    Future<long> blockNumber(CompletionQueue* completionQueue = nullptr, std::any tag = {});
};
}  // namespace bcos::sdk