#include "RPCClient.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include <boost/throw_exception.hpp>
#include <gsl/pointers>
#include <utility>

bcos::sdk::TarsCallback::TarsCallback(bcos::sdk::CompletionQueue* completionQueue, std::any tag,
    std::promise<tars::ReqMessagePtr> promise)
  : m_completionQueue(completionQueue), m_tag(std::move(tag)), m_promise(std::move(promise))
{}

bcos::sdk::CompletionQueue* bcos::sdk::TarsCallback::completionQueue()
{
    return m_completionQueue;
}
std::any& bcos::sdk::TarsCallback::tag()
{
    return m_tag;
}
std::promise<tars::ReqMessagePtr>& bcos::sdk::TarsCallback::promise()
{
    return m_promise;
}

void bcos::sdk::TarsCallback::callback_sendTransaction(
    bcostars::Error const& error, bcostars::TransactionReceipt const& response)
{
    if (error.errorCode != 0)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(error.errorCode, error.errorMessage));
    }
    m_response.emplace<protocol::TransactionReceipt::Ptr>(
        std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
            [m_inner = std::move(const_cast<bcostars::TransactionReceipt&>(response))]() mutable {
                return std::addressof(m_inner);
            }));
}

void bcos::sdk::TarsCallback::callback_sendTransaction_exception(tars::Int32 ret)
{
    BOOST_THROW_EXCEPTION(BCOS_ERROR(ret, "RPCClient::sendTransaction got exception"));
}

void bcos::sdk::TarsCallback::callback_blockNumber(
    bcostars::Error const& error, tars::Int64 response)
{
    if (error.errorCode != 0)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(error.errorCode, error.errorMessage));
    }
    m_response = response;
}

void bcos::sdk::TarsCallback::callback_blockNumber_exception(tars::Int32 ret)
{
    BOOST_THROW_EXCEPTION(BCOS_ERROR(ret, "RPCClient::blockNumber got exception"));
}

void bcos::sdk::RPCClient::onMessage(tars::ReqMessagePtr message)
{
    auto& callbackBase = dynamic_cast<TarsCallback&>(*message->callback.get());
    callbackBase.promise().set_value(std::move(message));

    if (callbackBase.completionQueue() != nullptr)
    {
        callbackBase.completionQueue()->notify(std::move(callbackBase.tag()));
    }
}

bcos::sdk::RPCClient::RPCClient(const std::string& connectionString)
{
    m_rpcProxy = m_communicator.stringToProxy<bcostars::RPCPrx>(connectionString);
    m_rpcProxy->tars_set_custom_callback(&RPCClient::onMessage);
}

bcos::sdk::Future<bcos::protocol::TransactionReceipt::Ptr> bcos::sdk::RPCClient::sendTransaction(
    const bcos::protocol::Transaction& transaction, CompletionQueue* completionQueue, std::any tag)
{
    auto const& tarsTransaction =
        dynamic_cast<bcostars::protocol::TransactionImpl const&>(transaction);

    std::promise<tars::ReqMessagePtr> promise;
    auto future = promise.get_future();
    auto callback =
        std::make_unique<TarsCallback>(completionQueue, std::move(tag), std::move(promise));
    m_rpcProxy->async_sendTransaction(callback.release(), tarsTransaction.inner());

    return {std::move(future)};
}

bcos::sdk::Future<long> bcos::sdk::RPCClient::blockNumber(
    CompletionQueue* completionQueue, std::any tag)
{
    std::promise<tars::ReqMessagePtr> promise;
    auto future = promise.get_future();
    auto callback =
        std::make_unique<TarsCallback>(completionQueue, std::move(tag), std::move(promise));
    m_rpcProxy->async_blockNumber(callback.release());

    return {std::move(future)};
}
