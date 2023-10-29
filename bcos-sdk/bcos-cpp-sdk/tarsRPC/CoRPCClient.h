#pragma once
#include "RPCClient.h"
#include <bcos-task/Coroutine.h>
#include <exception>
#include <type_traits>
#include <variant>

namespace bcos::sdk
{

template <class Handle>
class Awaitable
{
private:
    std::function<Handle(CO_STD::coroutine_handle<>)> m_applyCall;
    std::optional<Handle> m_handle;

public:
    Awaitable(decltype(m_applyCall) applyCall) : m_applyCall(std::move(applyCall)) {}

    constexpr bool await_ready() const { return false; }
    void await_suspend(CO_STD::coroutine_handle<> handle) { m_handle.emplace(m_applyCall(handle)); }
    Handle await_resume() { return std::move(*m_handle); }
};

class CoRPCClient
{
private:
    RPCClient& m_rpcClient;

public:
    CoRPCClient(RPCClient& rpcClient);

    Awaitable<SendTransaction> sendTransaction(const bcos::protocol::Transaction& transaction);
    Awaitable<Call> call(const bcos::protocol::Transaction& transaction);
    Awaitable<BlockNumber> blockNumber();
};
}  // namespace bcos::sdk