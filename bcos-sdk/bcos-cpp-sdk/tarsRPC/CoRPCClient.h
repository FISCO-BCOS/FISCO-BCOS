#pragma once
#include "RPCClient.h"
#include <bcos-task/Coroutine.h>
#include <exception>
#include <type_traits>
#include <variant>

namespace bcos::sdk
{

template <class Future>
class Awaitable
{
private:
    std::function<Future(CO_STD::coroutine_handle<>)> m_applyCall;
    Future m_future;

public:
    Awaitable(decltype(m_applyCall) applyCall) : m_applyCall(std::move(applyCall)) {}

    constexpr bool await_ready() const { return false; }
    void await_suspend(CO_STD::coroutine_handle<> handle) { m_future = m_applyCall(handle); }
    Future await_resume() { return std::move(m_future); }
};

class CoRPCClient
{
private:
    RPCClient& m_rpcClient;
    struct CoCompletionQueue : public bcos::sdk::CompletionQueue
    {
        void notify(std::any tag) override;
    };
    CoCompletionQueue m_coCompletionQueue;

public:
    CoRPCClient(RPCClient& rpcClient);

    Awaitable<Future<bcos::protocol::TransactionReceipt::Ptr>> sendTransaction(
        const bcos::protocol::Transaction& transaction);
    Awaitable<Future<long>> blockNumber();
};
}  // namespace bcos::sdk