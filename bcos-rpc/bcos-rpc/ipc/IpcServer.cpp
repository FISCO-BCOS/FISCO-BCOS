/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 *
 * @file IpcServer.cpp
 */
#include "IpcServer.h"
#include "bcos-rpc/Common.h"
#include <unistd.h>
#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>
#include <filesystem>

namespace bcos::rpc
{
IpcSession::IpcSession(boost::asio::local::stream_protocol::socket _socket, IpcHandler _handler,
    std::function<void(IpcSession::Ptr)> _onClose)
  : m_socket(std::move(_socket)),
    m_buffer(c_ipcMaxLineBytes + 1),
    m_handler(std::move(_handler)),
    m_onClose(std::move(_onClose))
{}

void IpcSession::start()
{
    readLine();
}

void IpcSession::close()
{
    // always on the socket's io thread
    if (m_closed)
    {
        return;
    }
    m_closed = true;
    boost::system::error_code ignored;
    m_socket.shutdown(boost::asio::local::stream_protocol::socket::shutdown_both, ignored);
    m_socket.close(ignored);
    if (m_onClose)
    {
        m_onClose(shared_from_this());
    }
}

void IpcSession::readLine()
{
    auto self = shared_from_this();
    boost::asio::async_read_until(
        m_socket, m_buffer, '\n', [self](boost::system::error_code const& _ec, size_t _length) {
            if (_ec)
            {
                if (_ec != boost::asio::error::eof && _ec != boost::asio::error::operation_aborted)
                {
                    // not_found == the line exceeded the buffer: refuse it by closing
                    RPC_LOG(DEBUG)
                        << LOG_BADGE("IpcSession") << LOG_DESC("read failed")
                        << LOG_KV("code", _ec.value()) << LOG_KV("message", _ec.message());
                }
                self->close();
                return;
            }
            self->onLine(_length);
        });
}

void IpcSession::onLine(size_t _length)
{
    auto begin = boost::asio::buffers_begin(m_buffer.data());
    std::string line(begin, begin + static_cast<std::ptrdiff_t>(_length));
    m_buffer.consume(_length);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
    {
        line.pop_back();
    }
    if (line.empty())
    {
        readLine();
        return;
    }
    auto self = shared_from_this();
    m_handler(line, [self](bcos::bytes _response, boost::beast::http::status) {
        // the RPC may answer from another thread; serialize onto the socket's io thread
        boost::asio::post(
            self->m_socket.get_executor(), [self, response = std::move(_response)]() mutable {
                self->write(std::move(response));
            });
    });
}

void IpcSession::write(bcos::bytes _response)
{
    if (m_closed)
    {
        return;
    }
    auto payload = std::make_shared<bcos::bytes>(std::move(_response));
    payload->push_back('\n');
    auto self = shared_from_this();
    boost::asio::async_write(m_socket, boost::asio::buffer(*payload),
        [self, payload](boost::system::error_code const& _ec, size_t) {
            if (_ec)
            {
                self->close();
                return;
            }
            self->readLine();
        });
}

IpcServer::IpcServer(boost::asio::io_context& _io, std::string _path, IpcHandler _handler)
  : m_io(_io), m_path(std::move(_path)), m_handler(std::move(_handler))
{}

IpcServer::~IpcServer()
{
    stop();
}

void IpcServer::start()
{
    if (m_running)
    {
        return;
    }
    std::error_code fsError;
    auto parent = std::filesystem::path(m_path).parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, fsError);
    }
    ::unlink(m_path.c_str());  // a stale file from a crashed node would make bind fail
    m_acceptor = std::make_shared<boost::asio::local::stream_protocol::acceptor>(
        m_io, boost::asio::local::stream_protocol::endpoint(m_path));
    m_running = true;
    accept();
    RPC_LOG(INFO) << LOG_DESC("IpcServerStarted") << LOG_KV("path", m_path);
}

void IpcServer::stop()
{
    if (!m_running.exchange(false))
    {
        return;
    }
    // the acceptor and the sessions live on m_io: close them there, never from this thread.
    // The lambdas own what they close (acceptor moved out, sessions copied), so stop() is safe
    // from the destructor too and the accept handler's re-arm cannot race the close.
    auto acceptor = std::move(m_acceptor);
    std::set<IpcSession::Ptr> sessions;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        sessions.swap(m_sessions);
    }
    boost::asio::post(m_io, [acceptor, sessions]() {
        boost::system::error_code ignored;
        if (acceptor)
        {
            acceptor->close(ignored);
        }
        for (auto const& session : sessions)
        {
            session->close();
        }
    });
    ::unlink(m_path.c_str());
    RPC_LOG(INFO) << LOG_DESC("IpcServerStopped") << LOG_KV("path", m_path);
}

void IpcServer::accept()
{
    auto self = shared_from_this();
    auto acceptor = m_acceptor;  // keeps the acceptor alive even if stop() moves it out meanwhile
    if (!acceptor)
    {
        return;
    }
    acceptor->async_accept([self, acceptor](boost::system::error_code const& _ec,
                               boost::asio::local::stream_protocol::socket _socket) {
        if (_ec)
        {
            if (self->m_running && _ec != boost::asio::error::operation_aborted)
            {
                RPC_LOG(WARNING) << LOG_BADGE("IpcServer") << LOG_DESC("accept failed")
                                 << LOG_KV("message", _ec.message());
                self->accept();
            }
            return;
        }
        if (!self->m_running)
        {
            return;  // stopped between accept and this handler: drop the socket
        }
        std::weak_ptr<IpcServer> weakServer = self;
        auto session = std::make_shared<IpcSession>(
            std::move(_socket), self->m_handler, [weakServer](IpcSession::Ptr _session) {
                if (auto server = weakServer.lock())
                {
                    server->removeSession(std::move(_session));
                }
            });
        {
            std::lock_guard<std::mutex> lock(self->m_sessionsMutex);
            if (!self->m_running)
            {
                return;  // stop() already swapped the session set out; this one closes with us
            }
            self->m_sessions.insert(session);
        }
        session->start();
        self->accept();
    });
}

void IpcServer::removeSession(IpcSession::Ptr _session)
{
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    m_sessions.erase(_session);
}
}  // namespace bcos::rpc
