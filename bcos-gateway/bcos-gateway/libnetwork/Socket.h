/** @file Socket.h
 * @ author: yujiechen
 * @ date: 2018-09-17
 * @ modification: rename RLPXSocket.h to Socket.h
 */

#pragma once

#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-utilities/BoostLog.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/filesystem.hpp>


namespace bcos::gateway
{
// Socket is templated on the stream type: the TCP-vs-SSL choice is a COMPILE-TIME parameter of
// the socket (and of Host/BasicSession via SocketT), not a runtime switch. StreamT must provide
// lowest_layer() (both ssl::stream<tcp::socket> and tcp::socket do); only the SSL flavour
// additionally exposes sslref() for handshake / verify / shutdown.
// Note: deliberately NOT enable_shared_from_this — nothing ever calls shared_from_this() on a
// socket; every async user keeps it alive with an explicit shared_ptr capture instead.
template <typename StreamT>
class BasicSocket
{
public:
    // The ssl::context is a pointer so the plain flavour can ignore it; for the SSL flavour it
    // must be non-null and must outlive this socket (the ssl::stream references it).
    BasicSocket(std::shared_ptr<ba::io_context> _ioService, ba::ssl::context* _sslContext,
        NodeIPEndpoint _nodeIPEndpoint)
      : m_nodeIPEndpoint(std::move(_nodeIPEndpoint)),
        m_ioService(std::move(_ioService)),
        m_stream(makeStream(*m_ioService, _sslContext))
    {}
    ~BasicSocket() { close(); }

    bool isConnected() const { return m_stream.lowest_layer().is_open(); }

    void close()
    {
        try
        {
            boost::system::error_code ec;
            m_stream.lowest_layer().shutdown(bi::tcp::socket::shutdown_both, ec);
            if (m_stream.lowest_layer().is_open())
            {
                m_stream.lowest_layer().close();
            }
        }
        catch (...)
        {}
    }

    bi::tcp::endpoint remoteEndpoint(boost::system::error_code ec = boost::system::error_code())
    {
        return m_stream.lowest_layer().remote_endpoint(ec);
    }

    bi::tcp::endpoint localEndpoint(boost::system::error_code ec = boost::system::error_code())
    {
        return m_stream.lowest_layer().local_endpoint(ec);
    }

    // The raw TCP socket (accept/connect target, movable into by tests).
    bi::tcp::socket& ref()
    {
        if constexpr (std::is_same_v<StreamT, ba::ssl::stream<bi::tcp::socket>>)
        {
            return m_stream.next_layer();
        }
        else
        {
            return m_stream;
        }
    }

    // The IO object reads and writes are dispatched on — the single dispatch entry point used by
    // ASIOInterface. ssl::stream and tcp::socket both provide async_read_some / async_write.
    StreamT& stream() { return m_stream; }

    // SSL-only surface (handshake / verify callback / shutdown); constrained so plain sockets
    // fail substitution and `if constexpr (requires { s->sslref(); })` selects the SSL branch.
    ba::ssl::stream<bi::tcp::socket>& sslref()
        requires std::same_as<StreamT, ba::ssl::stream<bi::tcp::socket>>
    {
        return m_stream;
    }

    const NodeIPEndpoint& nodeIPEndpoint() const { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint)
    {
        m_nodeIPEndpoint = std::move(_nodeIPEndpoint);
    }

    ba::io_context& ioService() { return *m_ioService; }

protected:
    static StreamT makeStream(ba::io_context& _ioService, ba::ssl::context* _sslContext)
    {
        if constexpr (std::is_constructible_v<StreamT, ba::io_context&, ba::ssl::context&>)
        {
            return StreamT(_ioService, *_sslContext);
        }
        else
        {
            return StreamT(_ioService);
        }
    }

    NodeIPEndpoint m_nodeIPEndpoint;
    std::shared_ptr<ba::io_context> m_ioService;
    StreamT m_stream;
};

// Production socket: TLS over TCP.
using Socket = BasicSocket<ba::ssl::stream<bi::tcp::socket>>;
// Plain TCP socket: no handshake, reads/writes go directly to the socket.
using PlainSocket = BasicSocket<bi::tcp::socket>;

}  // namespace bcos::gateway
