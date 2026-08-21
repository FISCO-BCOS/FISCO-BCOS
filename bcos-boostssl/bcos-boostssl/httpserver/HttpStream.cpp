#include "HttpStream.h"
#include <bcos-utilities/BoostLog.h>
#include <type_traits>

using namespace bcos::boostssl;

namespace
{
// access the lowest layer tcp stream of either a plain tcp stream or an ssl stream
template <typename Stream>
boost::beast::tcp_stream& lowestTcpStream(Stream& _stream)
{
    if constexpr (std::is_same_v<std::decay_t<Stream>, boost::beast::tcp_stream>)
    {
        return _stream;
    }
    else
    {
        return _stream.next_layer();
    }
}

std::string endpointToString(const boost::asio::ip::tcp::endpoint& _endpoint)
{
    return _endpoint.address().to_string() + ":" + std::to_string(_endpoint.port());
}
}  // namespace

http::HttpStream::HttpStream(boost::beast::tcp_stream _stream) : m_stream(std::move(_stream))
{
    HTTP_STREAM(DEBUG) << LOG_KV("[NEWOBJ][HttpStream]", this);
}

http::HttpStream::HttpStream(boost::beast::ssl_stream<boost::beast::tcp_stream> _stream)
  : m_stream(std::move(_stream))
{
    HTTP_STREAM(DEBUG) << LOG_KV("[NEWOBJ][HttpStream][SSL]", this);
}

http::HttpStream::~HttpStream()
{
    HTTP_STREAM(DEBUG) << LOG_KV("[DELOBJ][HttpStream]", this);
    close();
}

boost::beast::tcp_stream& http::HttpStream::stream()
{
    return std::visit(
        [](auto& _stream) -> boost::beast::tcp_stream& { return lowestTcpStream(_stream); },
        m_stream);
}

ws::WsStreamDelegate::Ptr http::HttpStream::wsStream()
{
    m_closed.test_and_set();
    ws::WsStreamDelegateBuilder builder;
    return std::visit(
        [&builder](auto& _stream) { return builder.build(std::move(_stream)); }, m_stream);
}

bool http::HttpStream::open()
{
    return !m_closed.test() &&
           std::visit(
               [](auto& _stream) { return lowestTcpStream(_stream).socket().is_open(); }, m_stream);
}

void http::HttpStream::close()
{
    if (!m_closed.test_and_set())
    {
        HTTP_STREAM(INFO) << LOG_DESC("close the stream") << LOG_KV("this", this);
        std::visit(
            [](auto& _stream) { ws::WsTools::close(lowestTcpStream(_stream).socket()); }, m_stream);
    }
}

void http::HttpStream::asyncRead(boost::beast::flat_buffer& _buffer,
    boost::beast::http::request_parser<boost::beast::http::string_body>& _parser,
    HttpStreamRWHandler _handler)
{
    if (!m_closed.test())
    {
        std::visit(
            [&](auto& _stream) {
                boost::beast::http::async_read(_stream, _buffer, _parser, std::move(_handler));
            },
            m_stream);
    }
}

void http::HttpStream::asyncWrite(const HttpResponse& _httpResp, HttpStreamRWHandler _handler)
{
    if (!m_closed.test())
    {
        std::visit(
            [&](auto& _stream) {
                boost::beast::http::async_write(_stream, _httpResp, std::move(_handler));
            },
            m_stream);
    }
}

std::string http::HttpStream::localEndpoint()
{
    try
    {
        return endpointToString(stream().socket().local_endpoint());
    }
    catch (...)
    {}

    return {};
}

std::string http::HttpStream::remoteEndpoint()
{
    try
    {
        return endpointToString(stream().socket().remote_endpoint());
    }
    catch (...)
    {}

    return {};
}

http::HttpStream::Ptr http::HttpStreamFactory::buildHttpStream(
    std::shared_ptr<boost::beast::tcp_stream> _stream)
{
    return std::make_shared<HttpStream>(std::move(*_stream));
}

http::HttpStream::Ptr http::HttpStreamFactory::buildHttpStream(
    std::shared_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>> _stream)
{
    return std::make_shared<HttpStream>(std::move(*_stream));
}
