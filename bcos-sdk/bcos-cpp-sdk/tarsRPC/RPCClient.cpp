#include "RPCClient.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include <gsl/pointers>
#include <utility>

std::any bcos::sdk::CompletionQueue::wait()
{
    std::any result;
    m_asyncQueue.pop(result);

    return result;
}

std::optional<std::any> bcos::sdk::CompletionQueue::tryWait()
{
    std::any result;
    if (m_asyncQueue.try_pop(result))
    {
        return std::make_optional(std::move(result));
    }

    return {};
}

bcos::sdk::RPCClient::RPCClient(const std::string& connectionString)
{
    m_rpcProxy = m_communicator.stringToProxy<bcostars::RPCPrx>(connectionString);
}

std::future<bcos::protocol::TransactionReceipt::Ptr> bcos::sdk::RPCClient::sendTransaction(
    const bcos::protocol::Transaction& transaction, CompletionQueue* completionQueue, std::any tag)
{
    class Callback : public bcostars::RPCPrxCallback
    {
    public:
        Callback(CompletionQueue* completionQueue, std::any tag)
          : m_completionQueue(completionQueue), m_tag(std::move(tag))
        {}

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

            if (m_completionQueue != nullptr)
            {
                m_completionQueue->m_asyncQueue.emplace(std::move(m_tag));
            }
        }

        void callback_sendTransaction_exception(tars::Int32 ret) override
        {
            m_promise.set_exception(std::make_exception_ptr(
                BCOS_ERROR(ret, "RPCClient::sendTransaction got exception")));

            if (m_completionQueue != nullptr)
            {
                m_completionQueue->m_asyncQueue.emplace(std::move(m_tag));
            }
        }

        CompletionQueue* m_completionQueue = nullptr;
        std::promise<bcos::protocol::TransactionReceipt::Ptr> m_promise;
        std::any m_tag;
    };

    auto const& tarsTransaction =
        dynamic_cast<bcostars::protocol::TransactionImpl const&>(transaction);

    auto* callback = gsl::owner<Callback*>(new Callback(completionQueue, std::move(tag)));
    auto future = callback->m_promise.get_future();
    m_rpcProxy->async_sendTransaction(callback, tarsTransaction.inner());

    return future;
}
