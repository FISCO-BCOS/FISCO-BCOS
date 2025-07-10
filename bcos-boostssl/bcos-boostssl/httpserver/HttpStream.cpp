#include "HttpStream.h"

std::string bcos::boostssl::http::HttpStream::localEndpoint()
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
std::string bcos::boostssl::http::HttpStream::remoteEndpoint()
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
bcos::boostssl::http::HttpStreamImpl::HttpStreamImpl(
    std::shared_ptr<boost::beast::tcp_stream> _stream)
  : m_stream(std::move(_stream))
{
    HTTP_STREAM(DEBUG) << LOG_KV("[NEWOBJ][HttpStreamImpl]", this);
}
bcos::boostssl::http::HttpStreamImpl::~HttpStreamImpl()
{
    HTTP_STREAM(DEBUG) << LOG_KV("[DELOBJ][HttpStreamImpl]", this);
    close();
}
boost::beast::tcp_stream& bcos::boostssl::http::HttpStreamImpl::stream()
{
    return *m_stream;
}
bcos::boostssl::ws::WsStreamDelegate::Ptr bcos::boostssl::http::HttpStreamImpl::wsStream()
{
    m_closed.test_and_set();
    auto builder = std::make_shared<ws::WsStreamDelegateBuilder>();
    return builder->build(m_stream);
}
bool bcos::boostssl::http::HttpStreamImpl::open()
{
    return !m_closed.test() && m_stream && m_stream->socket().is_open();
}
void bcos::boostssl::http::HttpStreamImpl::close()
{
    if (!m_closed.test_and_set())
    {
        HTTP_STREAM(INFO) << LOG_DESC("close the stream") << LOG_KV("this", this);
        ws::WsTools::close(m_stream->socket());
    }
}
void bcos::boostssl::http::HttpStreamImpl::asyncRead(boost::beast::flat_buffer& _buffer,
    boost::optional<boost::beast::http::request_parser<boost::beast::http::string_body>>& _parser,
    HttpStreamRWHandler _handler)
{
    if (!m_closed.test())
    {
        boost::beast::http::async_read(*m_stream, _buffer, *_parser, _handler);
    }
}
void bcos::boostssl::http::HttpStreamImpl::asyncWrite(
    const HttpResponse& _httpResp, HttpStreamRWHandler _handler)
{
    if (!m_closed.test())
    {
        boost::beast::http::async_write(*m_stream, _httpResp, _handler);
    }
}
bcos::boostssl::http::HttpStreamSslImpl::HttpStreamSslImpl(
    std::shared_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>> _stream)
  : m_stream(std::move(_stream))
{
    HTTP_STREAM(DEBUG) << LOG_KV("[NEWOBJ][HttpStreamSslImpl]", this);
}
bcos::boostssl::http::HttpStreamSslImpl::~HttpStreamSslImpl()
{
    HTTP_STREAM(DEBUG) << LOG_KV("[DELOBJ][HttpStreamSslImpl]", this);
    close();
}
boost::beast::tcp_stream& bcos::boostssl::http::HttpStreamSslImpl::stream()
{
    return m_stream->next_layer();
}
bcos::boostssl::ws::WsStreamDelegate::Ptr bcos::boostssl::http::HttpStreamSslImpl::wsStream()
{
    m_closed.test_and_set();
    auto builder = std::make_shared<ws::WsStreamDelegateBuilder>();
    return builder->build(m_stream);
}
bool bcos::boostssl::http::HttpStreamSslImpl::open()
{
    return !m_closed.test() && m_stream && m_stream->next_layer().socket().is_open();
}
void bcos::boostssl::http::HttpStreamSslImpl::close()
{
    if (!m_closed.test_and_set())
    {
        HTTP_STREAM(INFO) << LOG_DESC("close the ssl stream") << LOG_KV("this", this);
        ws::WsTools::close(m_stream->next_layer().socket());
    }
}
void bcos::boostssl::http::HttpStreamSslImpl::asyncRead(boost::beast::flat_buffer& _buffer,
    boost::optional<boost::beast::http::request_parser<boost::beast::http::string_body>>& _parser,
    HttpStreamRWHandler _handler)
{
    boost::beast::http::async_read(*m_stream, _buffer, *_parser, std::move(_handler));
}
void bcos::boostssl::http::HttpStreamSslImpl::asyncWrite(
    const HttpResponse& _httpResp, HttpStreamRWHandler _handler)
{
    boost::beast::http::async_write(*m_stream, _httpResp, std::move(_handler));
}
bcos::boostssl::http::HttpStream::Ptr bcos::boostssl::http::HttpStreamFactory::buildHttpStream(
    std::shared_ptr<boost::beast::tcp_stream> _stream)
{
    return std::make_shared<HttpStreamImpl>(std::move(_stream));
}
bcos::boostssl::http::HttpStream::Ptr bcos::boostssl::http::HttpStreamFactory::buildHttpStream(
    std::shared_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>> _stream)
{
    return std::make_shared<HttpStreamSslImpl>(std::move(_stream));
}
