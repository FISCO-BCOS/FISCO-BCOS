/**
 * @brief: inteface for boost::asio(for unittest)
 *
 * @file AsioInterface.h
 * @author: yujiechen
 * @date 2018-09-13
 */
#pragma once
#include "bcos-gateway/libnetwork/AsioAwaitable.h"
#include "bcos-gateway/libnetwork/SocketFace.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/IOServicePool.h"
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

namespace ba = boost::asio;
namespace bi = ba::ip;

namespace bcos::gateway
{
class ASIOInterface
{
public:
    enum ASIO_TYPE
    {
        TCP_ONLY = 0,
        SSL = 1
    };

    /// verify callback
    using VerifyCallback = std::function<bool(bool, boost::asio::ssl::verify_context&)>;

    ASIOInterface(IOServicePool::Ptr _ioServicePool, std::string listenHost, uint16_t listenPort);
    virtual ~ASIOInterface();
    virtual void setType(int type);

    virtual ba::ssl::context* srvContext();

    virtual void setSrvContext(ba::ssl::context _srvContext);
    virtual void setClientContext(ba::ssl::context _clientContext);

    virtual boost::asio::steady_timer newTimer(uint32_t timeout);
    // Unlike newTimer (round-robin pool context), this timer is bound to the acceptor's own
    // executor, so awaiting it resumes the caller on the acceptor's single io_context thread.
    // The accept retry loop relies on that thread to serialize its m_run re-check and its
    // async_accept re-arm against the cancelAcceptor() that Host::stop() posts there.
    virtual boost::asio::steady_timer newAcceptorTimer(uint32_t timeout);

    virtual std::shared_ptr<SocketFace> newSocket(
        bool _server, NodeIPEndpoint nodeIPEndpoint = NodeIPEndpoint());

    virtual bi::tcp::acceptor* acceptor();

    virtual void setVerifyCallback(
        const std::shared_ptr<SocketFace>& socket, VerifyCallback callback, bool /*unused*/ = true);

    // ----- coroutine-facing interface -----------------------------------------
    // Awaitable network operations, for use inside task::Task coroutines (see AsioAwaitable.h
    // for the threading / exception / lifetime contract). Each operation guarantees total
    // completion: the awaiting coroutine is resumed exactly once, on success and on failure
    // alike — a silently dropped completion would pin the suspended coroutine (and everything
    // its frame holds) forever. The awaitables live in the calling coroutine's own frame — no
    // bridge frame, and no per-operation allocation — and are obtained from the non-virtual
    // awaitable* helpers below.
    //
    // The read path is a COMPILE-TIME policy (the template parameter of awaitableReadSome):
    // production uses DefaultReadPolicy, whose invoke() directly dispatches async_read_some on
    // the socket (TCP vs SSL, see DefaultReadPolicy below) — no std::function, no virtual call,
    // fully inlined. Read-loop test fakes substitute their own policy type to park / control
    // read completions deterministically. CONTRACT (for custom policies and for every initiate
    // call in this header): the completion must be handed to a deferred executor (asio, or a
    // post to some io_context) — it must be neither invoked nor dropped synchronously. A
    // synchronous invocation / drop is neutralized by the arm/cancel handshake in AsioAwaitable
    // (see AsioAwaitable.h) rather than corrupting the running coroutine, but the awaitable's
    // total-completion guarantee is clearest when every initiation defers.
    using ReadSomeHandler = detail::AsioCompletion<boost::system::error_code, std::size_t>;

    // Production read-initiation policy: directly dispatches async_read_some on the socket
    // (TCP vs SSL, with the unexpected-type default completing via operation_not_supported).
    // Nested in ASIOInterface so it can read the private m_type; tests provide their own policy
    // with the same invoke() signature.
    struct DefaultReadPolicy
    {
        static void invoke(ASIOInterface* self, const std::shared_ptr<SocketFace>& socket,
            boost::asio::mutable_buffer buffers, ReadSomeHandler completion)
        {
            switch (self->m_type)
            {
            case TCP_ONLY:
                socket->ref().async_read_some(buffers, std::move(completion));
                break;
            case SSL:
                socket->sslref().async_read_some(buffers, std::move(completion));
                break;
            default:
                // total completion: an unexpected type must still answer the read, or the
                // awaiting read-loop coroutine pins forever. POST the completion rather than
                // invoking it inline — this runs on the initiator's stack inside await_suspend,
                // and an inline invocation would resume the coroutine from within its own
                // await_suspend.
                boost::asio::post(socket->ioService(),
                    [completion = std::move(completion)]() mutable {
                        completion(boost::asio::error::operation_not_supported, std::size_t{0});
                    });
                break;
            }
        }
    };

    template <typename ReadPolicy = DefaultReadPolicy>
    auto awaitableReadSome(
        const std::shared_ptr<SocketFace>& socket, boost::asio::mutable_buffer buffers)
    {
        return makeAsioAwaitable<boost::system::error_code, std::size_t>(
            [this, socket, buffers](auto handler) {
                ReadPolicy::invoke(this, socket, buffers, std::move(handler));
            });
    }

    auto awaitableAccept(const std::shared_ptr<SocketFace>& socket)
    {
        return makeAsioAwaitable<boost::system::error_code>(
            [this, socket](auto handler) {
                m_acceptor.async_accept(socket->ref(), std::move(handler));
            });
    }

    auto awaitableResolveConnect(const std::shared_ptr<SocketFace>& socket)
    {
        return makeAsioAwaitable<boost::system::error_code>(
            [this, socket](auto handler) { resolveConnect(socket, std::move(handler)); });
    }

    static auto awaitableHandshake(const std::shared_ptr<SocketFace>& socket,
        ba::ssl::stream_base::handshake_type type)
    {
        return makeAsioAwaitable<boost::system::error_code>(
            [socket, type](auto handler) {
                socket->sslref().async_handshake(type, std::move(handler));
            });
    }

    auto awaitableWrite(const std::shared_ptr<SocketFace>& socket, auto buffers)
    {
        return makeAsioAwaitable<boost::system::error_code, std::size_t>(
            [this, socket, buffers = std::move(buffers)](auto handler) mutable {
                auto type = m_type;
                auto& ioService = socket->ioService();
                if (socket->isConnected())
                {
                    boost::asio::post(ioService, [type, socket, buffers = std::move(buffers),
                                                     handler = std::move(handler)]() mutable {
                        switch (type)
                        {
                        case TCP_ONLY:
                        {
                            ba::async_write(socket->ref(), buffers, std::move(handler));
                            break;
                        }
                        case SSL:
                        {
                            ba::async_write(socket->sslref(), buffers, std::move(handler));
                            break;
                        }
                        default:
                            // total completion: an unexpected type must still answer the
                            // awaiting coroutine — dropping the handler would pin its frame
                            // forever. (The completion-or-cancel guard in AsioAwaitable would
                            // eventually release the frame, but failing loudly here keeps the
                            // error explicit.)
                            handler(boost::asio::error::operation_not_supported, 0);
                            break;
                        }
                    });
                }
                else
                {
                    // total completion, see the class comment above
                    boost::asio::post(ioService, [handler = std::move(handler)]() mutable {
                        handler(boost::asio::error::not_connected, 0);
                    });
                }
            });
    }

    // Cancel any pending async_accept so the accept loop (Host::acceptLoop) completes with
    // operation_aborted and can exit, releasing the strong Host reference its coroutine frame
    // holds. Must run on the acceptor's io_context thread — post it there, e.g. from Host::stop().
    void cancelAcceptor()
    {
        boost::system::error_code ec;
        // NOLINTNEXTLINE(bugprone-unused-return-value) error is delivered via the ec out-param
        m_acceptor.cancel(ec);
    }

    template <class Task>
    void post(Task&& task)
    {
        m_ioServicePool->post(std::forward<Task>(task));
    }

private:
    // resolve + connect helper backing awaitableResolveConnect (total completion: the handler
    // fires with an error when resolution fails — see the coroutine-interface comment above).
    // Templated because the handler is the move-only AsioCompletion.
    template <typename Handler>
    void resolveConnect(const std::shared_ptr<SocketFace>& socket, Handler handler)
    {
        auto protocol = socket->nodeIPEndpoint().isIPv6() ? bi::tcp::tcp::v6() : bi::tcp::tcp::v4();
        m_resolver.async_resolve(protocol, socket->nodeIPEndpoint().address(),
            std::to_string(socket->nodeIPEndpoint().port()),
            [socket, handler = std::move(handler)](const boost::system::error_code& ec,
                const bi::tcp::resolver::results_type& results) mutable {
                if (ec || results.empty())
                {
                    ASIO_LOG(WARNING) << LOG_DESC("asyncResolve failed")
                                      << LOG_KV("host", socket->nodeIPEndpoint().address())
                                      << LOG_KV("port", socket->nodeIPEndpoint().port());
                    // Total completion: the client coroutine co_awaits this operation, so the
                    // handler must fire on resolve failure too — the old callback version
                    // silently dropped it, which would pin the awaiting coroutine forever.
                    handler(ec ? ec : boost::asio::error::host_not_found);
                    return;
                }

                // results is a iterator, but only use first endpoint.
                auto it = results.begin();
                socket->ref().async_connect(it->endpoint(), std::move(handler));
                ASIO_LOG(INFO) << LOG_DESC("asyncResolveConnect")
                               << LOG_KV("endpoint", it->endpoint());
            });
    }

    IOServicePool::Ptr m_ioServicePool;
    bi::tcp::acceptor m_acceptor;
    bi::tcp::resolver m_resolver;

    std::optional<ba::ssl::context> m_srvContext;
    std::optional<ba::ssl::context> m_clientContext;
    int m_type = 0;
};
}  // namespace bcos::gateway
