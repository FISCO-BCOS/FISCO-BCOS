#include "RPCClient.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <boost/throw_exception.hpp>

void bcos::sdk::RPCClient::onMessage(tars::ReqMessagePtr message)
{
    auto& callbackBase = dynamic_cast<detail::TarsCallback&>(*message->callback.get());
    callbackBase.promise().set_value(std::move(message));

    if (callbackBase.completionQueue() != nullptr)
    {
        callbackBase.completionQueue()->notify(std::move(callbackBase.tag()));
    }
}

bcos::sdk::RPCClient::RPCClient(const std::string& connectionString)
{
    constexpr static int timeout = 60000;

    m_rpcProxy = m_communicator.stringToProxy<bcostars::RPCPrx>(connectionString);
    m_rpcProxy->tars_set_custom_callback(&RPCClient::onMessage);
    m_rpcProxy->tars_async_timeout(timeout);
}

bcos::sdk::Future<bcos::protocol::TransactionReceipt::Ptr> bcos::sdk::RPCClient::sendTransaction(
    const bcos::protocol::Transaction& transaction, CompletionQueue* completionQueue, std::any tag)
{
    auto const& tarsTransaction =
        dynamic_cast<bcostars::protocol::TransactionImpl const&>(transaction);

    std::promise<tars::ReqMessagePtr> promise;
    auto future = promise.get_future();
    auto callback =
        std::make_unique<detail::TarsCallback>(completionQueue, std::move(tag), std::move(promise));
    m_rpcProxy->async_sendTransaction(callback.release(), tarsTransaction.inner());

    return {std::move(future)};
}

bcos::sdk::Future<long> bcos::sdk::RPCClient::blockNumber(
    CompletionQueue* completionQueue, std::any tag)
{
    std::promise<tars::ReqMessagePtr> promise;
    auto future = promise.get_future();
    auto callback =
        std::make_unique<detail::TarsCallback>(completionQueue, std::move(tag), std::move(promise));
    m_rpcProxy->async_blockNumber(callback.release());

    return {std::move(future)};
}
