#include "CoRPCClient.h"
#include "bcos-cpp-sdk/tarsRPC/RPCClient.h"

class CoCallback : public bcos::sdk::Callback
{
private:
    CO_STD::coroutine_handle<> m_handle;

public:
    CoCallback(CO_STD::coroutine_handle<> handle) : m_handle(handle) {}
    void onMessage([[maybe_unused]] int seq) override { m_handle.resume(); }
};

bcos::sdk::CoRPCClient::CoRPCClient(RPCClient& rpcClient) : m_rpcClient(rpcClient) {}

bcos::sdk::Awaitable<bcos::sdk::SendTransaction> bcos::sdk::CoRPCClient::sendTransaction(
    const bcos::protocol::Transaction& transaction)
{
    return {[this, &transaction](CO_STD::coroutine_handle<> handle) {
        bcos::sdk::SendTransaction sendTransaction(m_rpcClient);
        sendTransaction.setCallback(std::make_shared<CoCallback>(handle));
        sendTransaction.send(transaction);
        return sendTransaction;
    }};
}

bcos::sdk::Awaitable<bcos::sdk::Call> bcos::sdk::CoRPCClient::call(
    const bcos::protocol::Transaction& transaction)
{
    return {[this, &transaction](CO_STD::coroutine_handle<> handle) {
        bcos::sdk::Call call(m_rpcClient);
        call.setCallback(std::make_shared<CoCallback>(handle));
        call.send(transaction);
        return call;
    }};
}

bcos::sdk::Awaitable<bcos::sdk::BlockNumber> bcos::sdk::CoRPCClient::blockNumber()
{
    return {[this](CO_STD::coroutine_handle<> handle) {
        bcos::sdk::BlockNumber blockNumber(m_rpcClient);
        blockNumber.setCallback(std::make_shared<CoCallback>(handle));
        blockNumber.send();
        return blockNumber;
    }};
}
