#pragma once
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-tars-protocol/tars/RPC.h"
#include <variant>

namespace bcos::sdk
{

class Callback;

namespace detail
{

class TarsCallback : public bcostars::RPCPrxCallback
{
private:
    std::shared_ptr<Callback> m_callback;
    std::promise<tars::ReqMessagePtr> m_promise;
    int m_seq;

    std::variant<long, protocol::TransactionReceipt::Ptr> m_response;

public:
    TarsCallback(
        std::shared_ptr<Callback> callback, std::promise<tars::ReqMessagePtr> promise, int seq);
    TarsCallback(TarsCallback const&) = delete;
    TarsCallback& operator=(TarsCallback const&) = delete;
    TarsCallback(TarsCallback&&) noexcept = default;
    TarsCallback& operator=(TarsCallback&&) noexcept = default;
    ~TarsCallback() noexcept override = default;

    Callback* callback();
    std::promise<tars::ReqMessagePtr>& promise();

    template <class Response>
    Response takeResponse() &&
    {
        return std::move(std::get<Response>(m_response));
    }

    int seq() const;

    void callback_sendTransaction(
        bcostars::Error const& error, bcostars::TransactionReceipt const& response) override;
    void callback_sendTransaction_exception(tars::Int32 ret) override;

    void callback_call(
        const bcostars::Error& ret, const bcostars::TransactionReceipt& response) override;
    void callback_call_exception(tars::Int32 ret) override;

    void callback_blockNumber(bcostars::Error const& error, tars::Int64 response) override;
    void callback_blockNumber_exception(tars::Int32 ret) override;
};
}  // namespace detail

}  // namespace bcos::sdk