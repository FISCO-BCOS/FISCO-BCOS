#pragma once
#include "TarsCallback.h"
#include <servant/Communicator.h>
#include <future>

namespace bcos::sdk
{
class RPCClient;

class Callback
{
public:
    Callback() = default;
    Callback(const Callback&) = default;
    Callback(Callback&&) noexcept = default;
    Callback& operator=(const Callback&) = default;
    Callback& operator=(Callback&&) noexcept = default;

    virtual ~Callback() noexcept = default;
    virtual void onMessage(int seq) = 0;
};

template <class Response>
class RPCHandle
{
private:
    RPCClient& m_rpcClient;
    std::future<tars::ReqMessagePtr> m_future;
    std::shared_ptr<Callback> m_callback;
    int m_seq{};

protected:
    RPCClient& rpcClient() { return m_rpcClient; }
    void setFuture(std::future<tars::ReqMessagePtr> future) { m_future = std::move(future); }
    std::shared_ptr<Callback> callback() const { return m_callback; }

public:
    RPCHandle(RPCClient& rpcClient) : m_rpcClient(rpcClient){};
    RPCHandle(RPCHandle const&) = delete;
    RPCHandle& operator=(RPCHandle const&) = delete;
    RPCHandle(RPCHandle&&) noexcept = default;
    RPCHandle& operator=(RPCHandle&&) noexcept = default;
    ~RPCHandle() noexcept = default;

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
    void setCallback(std::shared_ptr<bcos::sdk::Callback> callback)
    {
        m_callback = std::move(callback);
    }
    void setSeq(int seq) { m_seq = seq; }
    int seq() const { return m_seq; }
};
}  // namespace bcos::sdk