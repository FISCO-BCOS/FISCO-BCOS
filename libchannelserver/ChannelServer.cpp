/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/**
 * @file: ChannelServer.cpp
 * @author: monan
 *
 * @date: 2018
 */

#include "ChannelServer.h"

#include <libdevcore/easylog.h>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <iostream>


using namespace dev::channel;

void dev::channel::ChannelServer::run()
{
    _threadPool = std::make_shared<ThreadPool>("ChannelWorker", 8);
    if (!_listenHost.empty() && _listenPort > 0)
    {
        _acceptor = std::make_shared<boost::asio::ip::tcp::acceptor>(
            *_ioService, boost::asio::ip::tcp::endpoint(
                             boost::asio::ip::address::from_string(_listenHost), _listenPort));

        boost::asio::socket_base::reuse_address optionReuseAddress(true);
        _acceptor->set_option(optionReuseAddress);
    }

    _serverThread = std::make_shared<std::thread>([=]() {
        pthread_setThreadName("ChannelServer");

        while (_acceptor && _acceptor->is_open())
        {
            try
            {
                startAccept();
                _ioService->run();
            }
            catch (std::exception& e)
            {
                CHANNEL_LOG(ERROR) << LOG_DESC("IO thread error")
                                   << LOG_KV("what", boost::diagnostic_information(e));
            }


            sleep(1);

            if (_acceptor->is_open() && _ioService->stopped())
            {
                CHANNEL_LOG(WARNING) << LOG_DESC("io_service reset");
                _ioService->reset();
            }
        }
    });
}


void dev::channel::ChannelServer::onAccept(
    const boost::system::error_code& error, ChannelSession::Ptr session)
{
    if (!error)
    {
        auto remoteEndpoint = session->sslSocket()->lowest_layer().remote_endpoint();
        CHANNEL_LOG(TRACE) << LOG_DESC("Receive new connection")
                           << LOG_KV("from", remoteEndpoint.address().to_string()) << ":"
                           << remoteEndpoint.port();

        session->setHost(remoteEndpoint.address().to_string());
        session->setPort(remoteEndpoint.port());

        if (_enableSSL)
        {
            CHANNEL_LOG(TRACE) << LOG_DESC("Start SSL handshake");
            session->sslSocket()->async_handshake(boost::asio::ssl::stream_base::server,
                boost::bind(&ChannelServer::onHandshake, shared_from_this(),
                    boost::asio::placeholders::error, session));
        }
        else
        {
            CHANNEL_LOG(TRACE) << LOG_DESC("Call connectionHandler");

            if (_connectionHandler)
            {
                _connectionHandler(ChannelException(), session);
            }
        }
    }
    else
    {
        CHANNEL_LOG(WARNING) << LOG_DESC("Accept failed") << LOG_KV("message", error.message());

        try
        {
            session->sslSocket()->lowest_layer().close();
        }
        catch (std::exception& e)
        {
            CHANNEL_LOG(WARNING) << LOG_DESC("Close error")
                                 << LOG_KV("what", boost::diagnostic_information(e));
        }
    }

    startAccept();
}

void dev::channel::ChannelServer::startAccept()
{
    try
    {
        ChannelSession::Ptr session = std::make_shared<ChannelSession>();
        session->setThreadPool(_threadPool);
        session->setIOService(_ioService);
        session->setEnableSSL(_enableSSL);
        session->setMessageFactory(_messageFactory);

        session->setSSLSocket(
            std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket> >(
                *_ioService, *_sslContext));

        _acceptor->async_accept(session->sslSocket()->lowest_layer(),
            boost::bind(&ChannelServer::onAccept, shared_from_this(),
                boost::asio::placeholders::error, session));
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << LOG_DESC("startAccept error")
                           << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void dev::channel::ChannelServer::stop()
{
    try
    {
        CHANNEL_LOG(DEBUG) << LOG_DESC("Close acceptor");

        if (_acceptor->is_open())
        {
            _acceptor->cancel();
            _acceptor->close();
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << LOG_DESC("Close acceptor error")
                           << LOG_KV("what", boost::diagnostic_information(e));
    }

    try
    {
        CHANNEL_LOG(DEBUG) << LOG_DESC("Close ioService");
        _ioService->stop();
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << LOG_DESC("Close ioService")
                           << LOG_KV("what", boost::diagnostic_information(e));
        ;
    }
    _serverThread->join();
}

void dev::channel::ChannelServer::onHandshake(
    const boost::system::error_code& error, ChannelSession::Ptr session)
{
    try
    {
        if (!error)
        {
            CHANNEL_LOG(TRACE) << LOG_DESC("SSL handshake success");
            if (_connectionHandler)
            {
                _connectionHandler(ChannelException(), session);
            }
            else
            {
                CHANNEL_LOG(ERROR) << LOG_DESC("connectionHandler empty");
            }
        }
        else
        {
            CHANNEL_LOG(WARNING) << LOG_DESC("SSL handshake error")
                                 << LOG_KV("message", error.message());

            try
            {
                session->sslSocket()->lowest_layer().close();
            }
            catch (std::exception& e)
            {
                CHANNEL_LOG(WARNING)
                    << "Close error" << LOG_KV("what", boost::diagnostic_information(e));
            }
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << LOG_KV("what", boost::diagnostic_information(e));
    }
}
