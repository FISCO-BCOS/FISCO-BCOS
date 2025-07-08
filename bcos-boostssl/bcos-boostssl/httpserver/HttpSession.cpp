#include "HttpSession.h"

bcos::boostssl::http::HttpSession::HttpSession()
{
    HTTP_SESSION(DEBUG) << LOG_KV("[NEWOBJ][HTTPSESSION]", this);

    m_buffer = std::make_shared<boost::beast::flat_buffer>();
}
bcos::boostssl::http::HttpSession::~HttpSession()
{
    doClose();
    HTTP_SESSION(DEBUG) << LOG_KV("[DELOBJ][HTTPSESSION]", this);
}
void bcos::boostssl::http::HttpSession::run()
{
    boost::asio::dispatch(m_httpStream->stream().get_executor(),
        boost::beast::bind_front_handler(&HttpSession::doRead, shared_from_this()));
}
void bcos::boostssl::http::HttpSession::doRead()
{
    m_parser.emplace();
    // set limit to http request size, 100m
    m_parser->body_limit(PARSER_BODY_LIMITATION);

    auto buffer = m_buffer;
    auto session = shared_from_this();
    m_httpStream->asyncRead(*m_buffer, m_parser,
        [session, buffer](boost::system::error_code _ec, std::size_t bytes_transferred) {
            session->onRead(_ec, bytes_transferred);
        });
}
void bcos::boostssl::http::HttpSession::onRead(
    boost::beast::error_code ec, std::size_t bytes_transferred)
{
    try
    {
        // the peer client closed the connection
        if (ec == boost::beast::http::error::end_of_stream)
        {
            HTTP_SESSION(TRACE) << LOG_BADGE("onRead") << LOG_DESC("end of stream");
            doClose();
            return;
        }

        if (ec)
        {
            HTTP_SESSION(WARNING) << LOG_BADGE("onRead") << LOG_DESC("close the connection")
                                  << LOG_KV("failed", ec);
            doClose();
            return;
        }

        auto self = std::weak_ptr<HttpSession>(shared_from_this());
        if (boost::beast::websocket::is_upgrade(m_parser->get()))
        {
            HTTP_SESSION(INFO) << LOG_BADGE("onRead") << LOG_DESC("websocket upgrade");
            if (m_wsUpgradeHandler)
            {
                auto httpSession = self.lock();
                if (!httpSession)
                {
                    return;
                }
                m_wsUpgradeHandler(m_httpStream, m_parser->release(), httpSession->nodeId());
            }
            else
            {
                HTTP_SESSION(WARNING) << LOG_BADGE("onRead")
                                      << LOG_DESC(
                                             "the session will be closed for "
                                             "unsupported websocket upgrade");
                doClose();
                return;
            }
            return;
        }

        HTTP_SESSION(TRACE) << LOG_BADGE("onRead") << LOG_DESC("receive http request");
        handleRequest(m_parser->release());
    }
    catch (...)
    {
        HTTP_SESSION(WARNING) << LOG_DESC("onRead exception")
                              << LOG_KV("bytesSize", bytes_transferred)
                              << LOG_KV(
                                     "failed", boost::current_exception_diagnostic_information());
    }

    if (!m_queue->isFull())
    {
        doRead();
    }
}
void bcos::boostssl::http::HttpSession::onWrite(
    bool close, boost::beast::error_code ec, [[maybe_unused]] std::size_t bytes_transferred)
{
    if (ec)
    {
        HTTP_SESSION(WARNING) << LOG_BADGE("onWrite") << LOG_DESC("close the connection")
                              << LOG_KV("failed", ec);
        doClose();
        return;
    }

    if (close)
    {
        // we should close the connection, usually because
        // the response indicated the "Connection: close" semantic.
        doClose();
        return;
    }

    if (m_queue->onWrite())
    {
        // read the next request
        doRead();
    }
}
void bcos::boostssl::http::HttpSession::doClose()
{
    if (m_httpStream)
    {
        m_httpStream->close();
    }
}
void bcos::boostssl::http::HttpSession::handleRequest(HttpRequest&& _httpRequest)
{
    HTTP_SESSION(DEBUG) << LOG_BADGE("handleRequest") << LOG_DESC("request")
                        << LOG_KV("method", _httpRequest.method_string())
                        << LOG_KV("target", _httpRequest.target())
                        << LOG_KV("body", _httpRequest.body())
                        << LOG_KV("keep_alive", _httpRequest.keep_alive())
                        << LOG_KV("need_eof", _httpRequest.need_eof());

    auto startT = utcTime();
    unsigned version = _httpRequest.version();
    auto self = std::weak_ptr<HttpSession>(shared_from_this());
    if (m_httpReqHandler)
    {
        const std::string& request = _httpRequest.body();
        m_httpReqHandler(request, [self, version, startT](bcos::bytes _content) {
            auto session = self.lock();
            if (!session)
            {
                return;
            }
            auto resp = session->buildHttpResp(
                boost::beast::http::status::ok, version, std::move(_content));
            // put the response into the queue and waiting to be send
            BCOS_LOG(TRACE) << LOG_BADGE("handleRequest") << LOG_DESC("response")
                            << LOG_KV("body", std::string_view((const char*)resp->body().data(),
                                                  resp->body().size()))
                            << LOG_KV("keep_alive", resp->keep_alive())
                            << LOG_KV("timecost", (utcTime() - startT));
            session->queue()->enqueue(std::move(resp));
        });
    }
    else
    {
        // unsupported http service
        auto resp =
            buildHttpResp(boost::beast::http::status::http_version_not_supported, version, {});
        auto session = self.lock();
        if (!session)
        {
            return;
        }
        // put the response into the queue and waiting to be send
        HTTP_SESSION(WARNING) << LOG_BADGE("handleRequest") << LOG_DESC("unsupported http service")
                              << LOG_KV("body", std::string_view((const char*)resp->body().data(),
                                                    resp->body().size()));
        session->queue()->enqueue(std::move(resp));
    }
}
bcos::boostssl::http::HttpResponsePtr bcos::boostssl::http::HttpSession::buildHttpResp(
    boost::beast::http::status status, unsigned version, bcos::bytes content)
{
    auto msg = std::make_shared<HttpResponse>(status, version);
    msg->set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
    msg->set(boost::beast::http::field::content_type, "application/json");
    msg->keep_alive(true);  // default , keep alive
    msg->body() = std::move(content);
    msg->prepare_payload();
    return msg;
}
bcos::boostssl::http::HttpReqHandler bcos::boostssl::http::HttpSession::httpReqHandler() const
{
    return m_httpReqHandler;
}
void bcos::boostssl::http::HttpSession::setRequestHandler(HttpReqHandler _httpReqHandler)
{
    m_httpReqHandler = std::move(_httpReqHandler);
}
bcos::boostssl::http::WsUpgradeHandler bcos::boostssl::http::HttpSession::wsUpgradeHandler() const
{
    return m_wsUpgradeHandler;
}
void bcos::boostssl::http::HttpSession::setWsUpgradeHandler(WsUpgradeHandler _wsUpgradeHandler)
{
    m_wsUpgradeHandler = std::move(_wsUpgradeHandler);
}
std::shared_ptr<bcos::boostssl::http::Queue> bcos::boostssl::http::HttpSession::queue()
{
    return m_queue;
}
void bcos::boostssl::http::HttpSession::setQueue(std::shared_ptr<Queue> _queue)
{
    m_queue = std::move(_queue);
}
bcos::boostssl::http::HttpStream::Ptr bcos::boostssl::http::HttpSession::httpStream()
{
    return m_httpStream;
}
void bcos::boostssl::http::HttpSession::setHttpStream(HttpStream::Ptr _httpStream)
{
    m_httpStream = std::move(_httpStream);
}
std::shared_ptr<std::string> bcos::boostssl::http::HttpSession::nodeId()
{
    return m_nodeId;
}
void bcos::boostssl::http::HttpSession::setNodeId(std::shared_ptr<std::string> _nodeId)
{
    m_nodeId = std::move(_nodeId);
}
