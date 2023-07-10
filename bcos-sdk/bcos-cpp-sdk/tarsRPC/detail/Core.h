#pragma once
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-tars-protocol/tars/RPC.h"
#include <any>
#include <variant>

namespace bcos::sdk
{

class CompletionQueue;

namespace detail
{

class TarsCallback : public bcostars::RPCPrxCallback
{
private:
    CompletionQueue* m_completionQueue = nullptr;
    std::any m_tag;
    std::promise<tars::ReqMessagePtr> m_promise;
    std::variant<long, protocol::TransactionReceipt::Ptr> m_response;

public:
    TarsCallback(
        CompletionQueue* completionQueue, std::any tag, std::promise<tars::ReqMessagePtr> promise);
    TarsCallback(TarsCallback const&) = delete;
    TarsCallback& operator=(TarsCallback const&) = delete;
    TarsCallback(TarsCallback&&) = default;
    TarsCallback& operator=(TarsCallback&&) = default;
    ~TarsCallback() noexcept override = default;

    CompletionQueue* completionQueue();
    std::any& tag();
    std::promise<tars::ReqMessagePtr>& promise();

    template <class Response>
    Response getResponse() &&
    {
        return std::move(std::get<Response>(m_response));
    }

    void callback_sendTransaction(
        bcostars::Error const& error, bcostars::TransactionReceipt const& response) override;
    void callback_sendTransaction_exception(tars::Int32 ret) override;

    void callback_blockNumber(bcostars::Error const& error, tars::Int64 response) override;
    void callback_blockNumber_exception(tars::Int32 ret) override;
};
}  // namespace detail

}  // namespace bcos::sdk