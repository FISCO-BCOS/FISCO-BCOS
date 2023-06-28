#include "CoRPCClient.h"

void bcos::sdk::CoRPCClient::CoCompletionQueue::notify(std::any tag)
{
    auto handle = std::any_cast<CO_STD::coroutine_handle<>>(tag);
    handle.resume();
}

bcos::sdk::CoRPCClient::CoRPCClient(RPCClient& rpcClient) : m_rpcClient(rpcClient) {}

bcos::sdk::Awaitable<bcos::protocol::TransactionReceipt::Ptr>
bcos::sdk::CoRPCClient::sendTransaction(const bcos::protocol::Transaction& transaction)
{
    return {[this, &transaction](CO_STD::coroutine_handle<> handle) {
        return m_rpcClient.sendTransaction(
            transaction, std::addressof(m_coCompletionQueue), handle);
    }};
}

bcos::sdk::Awaitable<long> bcos::sdk::CoRPCClient::blockNumber()
{
    return {[this](CO_STD::coroutine_handle<> handle) {
        return m_rpcClient.blockNumber(std::addressof(m_coCompletionQueue), handle);
    }};
}
