#pragma once
#include "RPCClient.h"
#include <bcos-task/Coroutine.h>
#include <exception>
#include <type_traits>
#include <variant>

namespace bcos::sdk
{

template <class Response>
class Awaitable
{
private:
    std::function<Future<Response>(CO_STD::coroutine_handle<>)> m_applyCall;
    std::optional<Future<Response>> m_future;

public:
    Awaitable(decltype(m_applyCall) applyCall) : m_applyCall(std::move(applyCall)) {}

    constexpr bool await_ready() const { return false; }
    void await_suspend(CO_STD::coroutine_handle<> handle) { m_future.emplace(m_applyCall(handle)); }
    Response await_resume()
    {
        if constexpr (std::is_void_v<Response>)
        {
            m_future->get();
        }
        else
        {
            return m_future->get();
        }
    }
};

class CoRPCClient
{
private:
    RPCClient& m_rpcClient;
    class CoCompletionQueue : public bcos::sdk::CompletionQueue
    {
    public:
        void notify(std::any tag) override;
    };
    CoCompletionQueue m_coCompletionQueue;

public:
    CoRPCClient(RPCClient& rpcClient);

    Awaitable<bcos::protocol::TransactionReceipt::Ptr> sendTransaction(
        const bcos::protocol::Transaction& transaction);
    Awaitable<long> blockNumber();
};
}  // namespace bcos::sdk