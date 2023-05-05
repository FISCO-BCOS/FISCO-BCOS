#include "RPCClient.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include <gsl/pointers>

bcos::sdk::RPCClient::RPCClient() {}

std::future<bcos::protocol::TransactionReceipt::Ptr> bcos::sdk::RPCClient::sendTransaction(
    const bcos::protocol::Transaction& transaction, AsyncID asyncID)
{
    class Callback : public bcostars::RPCPrxCallback
    {
    public:
        void callback_sendTransaction(
            bcostars::Error const& error, bcostars::TransactionReceipt const& response) override
        {
            if (error.errorCode != 0)
            {
                m_promise.set_exception(
                    std::make_exception_ptr(BCOS_ERROR(error.errorCode, error.errorMessage)));
            }
            else
            {
                auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
                    [m_inner = std::move(const_cast<bcostars::TransactionReceipt&>(
                         response))]() mutable { return std::addressof(m_inner); });
                m_promise.set_value(std::move(receipt));
            }
        }

        void callback_sendTransaction_exception(tars::Int32 ret) override
        {
            m_promise.set_exception(std::make_exception_ptr(
                BCOS_ERROR(ret, "RPCClient::sendTransaction got exception")));
        }

        std::promise<bcos::protocol::TransactionReceipt::Ptr> m_promise;
        AsyncID m_asyncID = 0;
    };

    auto const& tarsTransaction =
        dynamic_cast<bcostars::protocol::TransactionImpl const&>(transaction);

    auto* callback = gsl::owner<Callback*>(new Callback());
    auto future = callback->m_promise.get_future();
    m_rpcProxy->async_sendTransaction(callback, tarsTransaction.inner());

    return future;
}
