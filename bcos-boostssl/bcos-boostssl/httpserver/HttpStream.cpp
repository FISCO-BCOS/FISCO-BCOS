#include "HttpStream.h"
#include <bcos-utilities/BoostLog.h>
#include <type_traits>

using namespace bcos::boostssl;

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
        [](auto& _stream) -> boost::beast::tcp_stream& {
            if constexpr (std::is_same_v<std::decay_t<decltype(_stream)>,
                              boost::beast::tcp_stream>)
            {
                return _stream;
            }
            else
            {
                return _stream.next_layer();
            }
        },
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
    if (m_closed.test())
    {
        return false;
    }
    return std::visit(
        [](auto& _stream) {
            if constexpr (std::is_same_v<std::decay_t<decltype(_stream)>,
                              boost::beast::tcp_stream>)
            {
                return _stream.socket().is_open();
            }
            else
            {
                return _stream.next_layer().socket().is_open();
            }
        },
        m_stream);
}

void http::HttpStream::close()
{
    if (!m_closed.test_and_set())
    {
        HTTP_STREAM(INFO) << LOG_DESC("close the stream") << LOG_KV("this", this);
        std::visit(
            [](auto& _stream) {
                if constexpr (std::is_same_v<std::decay_t<decltype(_stream)>,
                                  boost::beast::tcp_stream>)
                {
                    ws::WsTools::close(_stream.socket());
                }
                else
                {
                    ws::WsTools::close(_stream.next_layer().socket());
                }
            },
            m_stream);
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
        auto& s = stream();
        auto localEndPoint = s.socket().local_endpoint();
        auto endPoint =
            localEndPoint.address().to_string() + ":" + std::to_string(localEndPoint.port());
        return endPoint;
    }
    catch (...)
    {}

    return {};
}

std::string http::HttpStream::remoteEndpoint()
{
    try
    {
        auto& s = stream();
        auto remoteEndpoint = s.socket().remote_endpoint();
        auto endPoint =
            remoteEndpoint.address().to_string() + ":" + std::to_string(remoteEndpoint.port());
        return endPoint;
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
