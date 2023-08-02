#include "RPCClient.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <boost/throw_exception.hpp>

void bcos::sdk::RPCClient::onMessage(tars::ReqMessagePtr message)
{
    auto& callbackBase = dynamic_cast<detail::TarsCallback&>(*message->callback);
    callbackBase.promise().set_value(std::move(message));

    if (callbackBase.callback() != nullptr)
    {
        callbackBase.callback()->onMessage();
    }
}

bcos::sdk::RPCClient::RPCClient(const std::string& connectionString)
{
    constexpr static int timeout = 60000;

    m_rpcProxy = m_communicator.stringToProxy<bcostars::RPCPrx>(connectionString);
    m_rpcProxy->tars_set_custom_callback(&RPCClient::onMessage);
    m_rpcProxy->tars_async_timeout(timeout);
}

bcos::sdk::SendTransaction::SendTransaction(RPCClient& rpcClient) : Handle(rpcClient){};
bcos::sdk::SendTransaction& bcos::sdk::SendTransaction::send(
    const bcos::protocol::Transaction& transaction)
{
    auto const& tarsTransaction =
        dynamic_cast<bcostars::protocol::TransactionImpl const&>(transaction);

    std::promise<tars::ReqMessagePtr> promise;
    setFuture(promise.get_future());

    auto tarsCallback = std::make_unique<detail::TarsCallback>(callback(), std::move(promise));
    rpcClient().rpcProxy()->async_sendTransaction(tarsCallback.release(), tarsTransaction.inner());

    return *this;
}

bcos::sdk::BlockNumber::BlockNumber(RPCClient& rpcClient) : Handle(rpcClient){};
bcos::sdk::BlockNumber& bcos::sdk::BlockNumber::send()
{
    std::promise<tars::ReqMessagePtr> promise;
    setFuture(promise.get_future());

    auto tarsCallback = std::make_unique<detail::TarsCallback>(callback(), std::move(promise));
    rpcClient().rpcProxy()->async_blockNumber(tarsCallback.release());

    return *this;
}
