/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <bcos-boostssl/websocket/WsStream.h>

using namespace bcos::boostssl::ws;

WsStreamDelegate::WsStreamDelegate(RawWsStream::Ptr _rawStream)
  : m_isSsl(false), m_rawStream(std::move(_rawStream))
{}

WsStreamDelegate::WsStreamDelegate(SslWsStream::Ptr _sslStream)
  : m_isSsl(true), m_sslStream(std::move(_sslStream))
{}

void WsStreamDelegate::setMaxReadMsgSize(uint32_t _maxValue)
{
    if (m_isSsl)
    {
        m_sslStream->setMaxReadMsgSize(_maxValue);
        return;
    }
    m_rawStream->setMaxReadMsgSize(_maxValue);
}

bool WsStreamDelegate::open()
{
    return m_isSsl ? m_sslStream->open() : m_rawStream->open();
}

void WsStreamDelegate::close()
{
    if (m_isSsl)
    {
        m_sslStream->close();
        return;
    }
    m_rawStream->close();
}

std::string WsStreamDelegate::localEndpoint()
{
    return m_isSsl ? m_sslStream->localEndpoint() : m_rawStream->localEndpoint();
}

std::string WsStreamDelegate::remoteEndpoint()
{
    return m_isSsl ? m_sslStream->remoteEndpoint() : m_rawStream->remoteEndpoint();
}

void WsStreamDelegate::asyncWrite(const bcos::bytes& _buffer, WsStreamRWHandler _handler)
{
    if (m_isSsl)
    {
        m_sslStream->asyncWrite(_buffer, std::move(_handler));
        return;
    }
    m_rawStream->asyncWrite(_buffer, std::move(_handler));
}

void WsStreamDelegate::asyncRead(boost::beast::flat_buffer& _buffer, WsStreamRWHandler _handler)
{
    if (m_isSsl)
    {
        m_sslStream->asyncRead(_buffer, std::move(_handler));
        return;
    }
    m_rawStream->asyncRead(_buffer, std::move(_handler));
}

void WsStreamDelegate::asyncWsHandshake(const std::string& _host, const std::string& _target,
    std::function<void(boost::beast::error_code)> _handler)
{
    if (m_isSsl)
    {
        m_sslStream->asyncHandshake(_host, _target, std::move(_handler));
        return;
    }
    m_rawStream->asyncHandshake(_host, _target, std::move(_handler));
}

void WsStreamDelegate::asyncAccept(
    bcos::boostssl::http::HttpRequest _httpRequest, WsStreamHandshakeHandler _handler)
{
    if (m_isSsl)
    {
        m_sslStream->asyncAccept(std::move(_httpRequest), std::move(_handler));
        return;
    }
    m_rawStream->asyncAccept(std::move(_httpRequest), std::move(_handler));
}

void WsStreamDelegate::asyncHandshake(std::function<void(boost::beast::error_code)> _handler)
{
    if (m_isSsl)
    {
        m_sslStream->stream()->next_layer().async_handshake(
            boost::asio::ssl::stream_base::client, std::move(_handler));
        return;
    }
    _handler(make_error_code(boost::system::errc::success));
}

boost::beast::tcp_stream& WsStreamDelegate::tcpStream()
{
    return m_isSsl ? m_sslStream->tcpStream() : m_rawStream->tcpStream();
}

void WsStreamDelegate::setVerifyCallback(bool _disableSsl, VerifyCallback callback, bool)
{
    if (!_disableSsl)
    {
        m_sslStream->stream()->next_layer().set_verify_callback(std::move(callback));
    }
}

WsStreamDelegate::Ptr WsStreamDelegateBuilder::build(
    std::shared_ptr<boost::beast::tcp_stream> _tcpStream)
{
    _tcpStream->socket().set_option(boost::asio::ip::tcp::no_delay(true));
    auto wsStream = std::make_shared<boost::beast::websocket::stream<boost::beast::tcp_stream>>(
        std::move(*_tcpStream));
    auto rawWsStream =
        std::make_shared<bcos::boostssl::ws::WsStream<boost::beast::tcp_stream>>(wsStream);
    return std::make_shared<WsStreamDelegate>(rawWsStream);
}

WsStreamDelegate::Ptr WsStreamDelegateBuilder::build(
    std::shared_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>> _sslStream)
{
    _sslStream->next_layer().socket().set_option(boost::asio::ip::tcp::no_delay(true));
    auto wsStream = std::make_shared<
        boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>>(
        std::move(*_sslStream));
    auto sslWsStream =
        std::make_shared<bcos::boostssl::ws::WsStream<boost::beast::ssl_stream<boost::beast::tcp_stream>>>(
            wsStream);
    return std::make_shared<WsStreamDelegate>(sslWsStream);
}

WsStreamDelegate::Ptr WsStreamDelegateBuilder::build(bool _disableSsl,
    std::shared_ptr<boost::asio::ssl::context> _ctx,
    std::shared_ptr<boost::beast::tcp_stream> _tcpStream)
{
    if (_disableSsl)
    {
        return build(std::move(_tcpStream));
    }

    auto sslStream = std::make_shared<boost::beast::ssl_stream<boost::beast::tcp_stream>>(
        std::move(*_tcpStream), *_ctx);
    return build(std::move(sslStream));
}