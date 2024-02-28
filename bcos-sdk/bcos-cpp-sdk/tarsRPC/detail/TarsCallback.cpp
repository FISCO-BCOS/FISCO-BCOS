#include "TarsCallback.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"

bcos::sdk::detail::TarsCallback::TarsCallback(
    std::shared_ptr<Callback> callback, std::promise<tars::ReqMessagePtr> promise, int seq)
  : m_callback(std::move(callback)), m_promise(std::move(promise)), m_seq(seq)
{}

bcos::sdk::Callback* bcos::sdk::detail::TarsCallback::callback()
{
    return m_callback.get();
}

std::promise<tars::ReqMessagePtr>& bcos::sdk::detail::TarsCallback::promise()
{
    return m_promise;
}

int bcos::sdk::detail::TarsCallback::seq() const
{
    return m_seq;
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

void bcos::sdk::detail::TarsCallback::callback_call(
    const bcostars::Error& ret, const bcostars::TransactionReceipt& response)
{
    if (ret.errorCode != 0)
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(ret.errorCode, ret.errorMessage));
    }
    m_response.emplace<protocol::TransactionReceipt::Ptr>(
        std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
            [m_inner = std::move(const_cast<bcostars::TransactionReceipt&>(response))]() mutable {
                return std::addressof(m_inner);
            }));
}
void bcos::sdk::detail::TarsCallback::callback_call_exception(tars::Int32 ret) {}

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
