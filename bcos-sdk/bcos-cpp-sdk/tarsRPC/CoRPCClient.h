#pragma once
#include "RPCClient.h"
#include <coroutine>

namespace bcos::sdk::async
{

template <class Handle>
class Awaitable
{
private:
    std::function<Handle(std::coroutine_handle<>)> m_applyCall;
    std::optional<Handle> m_handle;

public:
    Awaitable(decltype(m_applyCall) applyCall) : m_applyCall(std::move(applyCall)) {}

    constexpr bool await_ready() const { return false; }
    void await_suspend(std::coroutine_handle<> handle) { m_handle.emplace(m_applyCall(handle)); }
    Handle await_resume() { return std::move(*m_handle); }
};

Awaitable<SendTransaction> sendTransaction(
    RPCClient& rpcClient, const bcos::protocol::Transaction& transaction);
Awaitable<Call> call(RPCClient& rpcClient, const bcos::protocol::Transaction& transaction);
Awaitable<BlockNumber> blockNumber(RPCClient& rpcClient);
}  // namespace bcos::sdk::async