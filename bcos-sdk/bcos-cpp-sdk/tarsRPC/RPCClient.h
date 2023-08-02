#pragma once

#include "bcos-framework/protocol/Transaction.h"
#include "bcos-tars-protocol/tars/RPC.h"
#include "detail/Core.h"
#include <servant/Communicator.h>

namespace bcos::sdk
{

class RPCClient
{
private:
    tars::Communicator m_communicator;
    bcostars::RPCPrx m_rpcProxy;

    static void onMessage(tars::ReqMessagePtr message);

public:
    RPCClient(const std::string& connectionString);
    bcostars::RPCPrx& rpcProxy() { return m_rpcProxy; }
};

class Callback
{
public:
    Callback() = default;
    Callback(const Callback&) = default;
    Callback(Callback&&) noexcept = default;
    Callback& operator=(const Callback&) = default;
    Callback& operator=(Callback&&) noexcept = default;

    virtual ~Callback() noexcept = default;
    virtual void onMessage() = 0;
};

template <class Response>
class Handle
{
private:
    RPCClient& m_rpcClient;
    std::future<tars::ReqMessagePtr> m_future;
    std::shared_ptr<Callback> m_callback;

protected:
    RPCClient& rpcClient() { return m_rpcClient; }
    void setFuture(std::future<tars::ReqMessagePtr> future) { m_future = std::move(future); }
    std::shared_ptr<Callback> callback() { return m_callback; }

public:
    Handle(RPCClient& rpcClient) : m_rpcClient(rpcClient){};
    Handle(Handle const&) = default;
    Handle& operator=(Handle const&) = default;
    Handle(Handle&&) noexcept = default;
    Handle& operator=(Handle&&) noexcept = default;
    ~Handle() noexcept = default;

    Response get()
    {
        auto message = m_future.get();
        message->callback->dispatch(message);

        auto& tarsCallback = dynamic_cast<detail::TarsCallback&>(*message->callback);
        return std::move(tarsCallback).takeResponse<Response>();
    }
    void waitFor() { m_future.wait(); }
    template <typename Rep, typename Period>
    std::future_status waitFor(const std::chrono::duration<Rep, Period>& rel)
    {
        return m_future.wait_for(rel);
    }
    template <typename Clock, typename Duration>
    std::future_status waitUntil(const std::chrono::time_point<Clock, Duration>& abs)
    {
        return m_future.wait_until(abs);
    }
    void setCallback(std::shared_ptr<Callback> callback) { m_callback = std::move(callback); }
};

class SendTransaction : public Handle<bcos::protocol::TransactionReceipt::Ptr>
{
public:
    SendTransaction(RPCClient& rpcClient) : Handle(rpcClient){};
    SendTransaction& send(const bcos::protocol::Transaction& transaction);
};

class BlockNumber : public Handle<long>
{
public:
    BlockNumber(RPCClient& rpcClient) : Handle(rpcClient){};
    BlockNumber& send();
};

}  // namespace bcos::sdk