#include "CoRPCClient.h"
#include "bcos-cpp-sdk/tarsRPC/RPCClient.h"

class CoCallback : public bcos::sdk::Callback
{
private:
    CO_STD::coroutine_handle<> m_handle;

public:
    CoCallback(CO_STD::coroutine_handle<> handle) : m_handle(handle) {}
    void onMessage([[maybe_unused]] int seq) override
    {
        // Make sure we are in the io thread of tars!
        m_handle.resume();
    }
};

bcos::sdk::async::Awaitable<bcos::sdk::SendTransaction> bcos::sdk::async::sendTransaction(
    RPCClient& rpcClient, const bcos::protocol::Transaction& transaction)
{
    return {[&](CO_STD::coroutine_handle<> handle) {
        bcos::sdk::SendTransaction sendTransaction(rpcClient);
        sendTransaction.setCallback(std::make_shared<CoCallback>(handle));
        sendTransaction.send(transaction);
        return sendTransaction;
    }};
}

bcos::sdk::async::Awaitable<bcos::sdk::Call> bcos::sdk::async::call(
    RPCClient& rpcClient, const bcos::protocol::Transaction& transaction)
{
    return {[&](CO_STD::coroutine_handle<> handle) {
        bcos::sdk::Call call(rpcClient);
        call.setCallback(std::make_shared<CoCallback>(handle));
        call.send(transaction);
        return call;
    }};
}

bcos::sdk::async::Awaitable<bcos::sdk::BlockNumber> bcos::sdk::async::blockNumber(
    RPCClient& rpcClient)
{
    return {[&](CO_STD::coroutine_handle<> handle) {
        bcos::sdk::BlockNumber blockNumber(rpcClient);
        blockNumber.setCallback(std::make_shared<CoCallback>(handle));
        blockNumber.send();
        return blockNumber;
    }};
}
