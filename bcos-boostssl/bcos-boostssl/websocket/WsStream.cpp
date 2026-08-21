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

WsStreamDelegate::WsStreamDelegate(RawWsStream::Ptr _rawStream) : m_stream(std::move(_rawStream)) {}

WsStreamDelegate::WsStreamDelegate(SslWsStream::Ptr _sslStream) : m_stream(std::move(_sslStream)) {}

void WsStreamDelegate::setMaxReadMsgSize(uint32_t _maxValue)
{
    std::visit([_maxValue](const auto& _s) { _s->setMaxReadMsgSize(_maxValue); }, m_stream);
}

bool WsStreamDelegate::open()
{
    return std::visit([](const auto& _stream) { return _stream->open(); }, m_stream);
}

void WsStreamDelegate::close()
{
    std::visit([](const auto& _stream) { _stream->close(); }, m_stream);
}

std::string WsStreamDelegate::localEndpoint()
{
    return std::visit([](const auto& _stream) { return _stream->localEndpoint(); }, m_stream);
}

std::string WsStreamDelegate::remoteEndpoint()
{
    return std::visit([](const auto& _stream) { return _stream->remoteEndpoint(); }, m_stream);
}

void WsStreamDelegate::asyncWrite(const bcos::bytes& _buffer, WsStreamRWHandler _handler)
{
    std::visit([&](const auto& _s) { _s->asyncWrite(_buffer, std::move(_handler)); }, m_stream);
}

void WsStreamDelegate::asyncRead(boost::beast::flat_buffer& _buffer, WsStreamRWHandler _handler)
{
    std::visit([&](const auto& _s) { _s->asyncRead(_buffer, std::move(_handler)); }, m_stream);
}

void WsStreamDelegate::asyncWsHandshake(const std::string& _host, const std::string& _target,
    std::function<void(boost::beast::error_code)> _handler)
{
    std::visit(
        [&](const auto& _s) { _s->asyncHandshake(_host, _target, std::move(_handler)); }, m_stream);
}

void WsStreamDelegate::asyncAccept(
    bcos::boostssl::http::HttpRequest _httpRequest, WsStreamHandshakeHandler _handler)
{
    std::visit(
        [&](const auto& _s) { _s->asyncAccept(std::move(_httpRequest), std::move(_handler)); },
        m_stream);
}

void WsStreamDelegate::asyncHandshake(std::function<void(boost::beast::error_code)> _handler)
{
    if (auto* sslStream = std::get_if<SslWsStream::Ptr>(&m_stream))
    {
        auto& nextLayer = (*sslStream)->stream().next_layer();
        nextLayer.async_handshake(boost::asio::ssl::stream_base::client, std::move(_handler));
        return;
    }
    _handler(make_error_code(boost::system::errc::success));
}

boost::beast::tcp_stream& WsStreamDelegate::tcpStream()
{
    return std::visit(
        [](const auto& _s) -> boost::beast::tcp_stream& { return _s->tcpStream(); }, m_stream);
}

void WsStreamDelegate::setVerifyCallback(bool _disableSsl, VerifyCallback callback, bool)
{
    if (!_disableSsl)
    {
        if (auto* sslStream = std::get_if<SslWsStream::Ptr>(&m_stream))
        {
            (*sslStream)->stream().next_layer().set_verify_callback(std::move(callback));
        }
    }
}

WsStreamDelegate::Ptr WsStreamDelegateBuilder::build(boost::beast::tcp_stream _tcpStream)
{
    _tcpStream.socket().set_option(boost::asio::ip::tcp::no_delay(true));
    auto rawWsStream = std::make_shared<RawWsStream>(
        boost::beast::websocket::stream<boost::beast::tcp_stream>(std::move(_tcpStream)));
    return std::make_shared<WsStreamDelegate>(std::move(rawWsStream));
}

WsStreamDelegate::Ptr WsStreamDelegateBuilder::build(
    boost::beast::ssl_stream<boost::beast::tcp_stream> _sslStream)
{
    _sslStream.next_layer().socket().set_option(boost::asio::ip::tcp::no_delay(true));
    auto sslWsStream = std::make_shared<SslWsStream>(
        boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>(
            std::move(_sslStream)));
    return std::make_shared<WsStreamDelegate>(std::move(sslWsStream));
}

WsStreamDelegate::Ptr WsStreamDelegateBuilder::build(bool _disableSsl,
    std::shared_ptr<boost::asio::ssl::context> _ctx,
    std::shared_ptr<boost::beast::tcp_stream> _tcpStream)
{
    if (_disableSsl)
    {
        return build(std::move(*_tcpStream));
    }

    auto sslStream = std::make_shared<boost::beast::ssl_stream<boost::beast::tcp_stream>>(
        std::move(*_tcpStream), *_ctx);
    return build(std::move(*sslStream));
}
