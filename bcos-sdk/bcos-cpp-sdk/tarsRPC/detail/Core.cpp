#include "Core.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"

bcos::sdk::detail::TarsCallback::TarsCallback(bcos::sdk::CompletionQueue* completionQueue,
    std::any tag, std::promise<tars::ReqMessagePtr> promise)
  : m_completionQueue(completionQueue), m_tag(std::move(tag)), m_promise(std::move(promise))
{}

bcos::sdk::CompletionQueue* bcos::sdk::detail::TarsCallback::completionQueue()
{
    return m_completionQueue;
}
std::any& bcos::sdk::detail::TarsCallback::tag()
{
    return m_tag;
}
std::promise<tars::ReqMessagePtr>& bcos::sdk::detail::TarsCallback::promise()
{
    return m_promise;
}

void bcos::sdk::detail::TarsCallback::callback_sendTransaction(
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

void bcos::sdk::detail::TarsCallback::callback_sendTransaction_exception(tars::Int32 ret)
{
    BOOST_THROW_EXCEPTION(BCOS_ERROR(ret, "RPCClient::sendTransaction got exception"));
}

void bcos::sdk::detail::TarsCallback::callback_blockNumber(
    bcostars::Error const& error, tars::Int64 response)
{
    if (error.errorCode != 0)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(error.errorCode, error.errorMessage));
    }
    m_response = response;
}

void bcos::sdk::detail::TarsCallback::callback_blockNumber_exception(tars::Int32 ret)
{
    BOOST_THROW_EXCEPTION(BCOS_ERROR(ret, "RPCClient::blockNumber got exception"));
}