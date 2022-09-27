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

#include <libnetwork/Common.h>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <iostream>


using namespace std;
using namespace dev::channel;

void dev::channel::ChannelServer::run()
{
    auto sslCtx = m_sslContext->native_handle();
    auto cert = SSL_CTX_get0_certificate(sslCtx);
    /// get issuer name
    const char* issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
    std::string issuerName(issuer);
    m_certIssuerName = issuerName;
    OPENSSL_free((void*)issuer);

    // ChannelReq process more request, should be larger
    m_requestThreadPool =
        std::make_shared<ThreadPool>("ChannelReq", m_threadPoolSize > 0 ? m_threadPoolSize : 16);
    m_responseThreadPool =
        std::make_shared<ThreadPool>("ChannelResp", m_threadPoolSize > 0 ? m_threadPoolSize : 8);
    if (!m_listenHost.empty() && m_listenPort > 0)
    {
        m_acceptor = std::make_shared<boost::asio::ip::tcp::acceptor>(
            *m_ioService, boost::asio::ip::tcp::endpoint(
                              boost::asio::ip::make_address(m_listenHost), m_listenPort));

        boost::asio::socket_base::reuse_address optionReuseAddress(true);
        m_acceptor->set_option(optionReuseAddress);
    }

    CHANNEL_LOG(INFO) << LOG_BADGE("ChannelServer::run")
                      << LOG_KV("threadPoolSize", m_threadPoolSize);

    m_serverThread = std::make_shared<std::thread>([=]() {
        pthread_setThreadName("ChannelServer");

        while (m_acceptor && m_acceptor->is_open())
        {
            try
            {
                startAccept();
                m_ioService->run();
            }
            catch (std::exception& e)
            {
                CHANNEL_LOG(ERROR) << LOG_DESC("IO thread error")
                                   << LOG_KV("what", boost::diagnostic_information(e));
            }


            this_thread::sleep_for(chrono::milliseconds(1000));

            if (m_acceptor->is_open() && m_ioService->stopped())
            {
                CHANNEL_LOG(WARNING) << LOG_DESC("io_service reset");
                m_ioService->reset();
            }
        }
    });
}


void dev::channel::ChannelServer::onAccept(
    const boost::system::error_code& error, ChannelSession::Ptr session)
{
    if (!error)
    {
        boost::system::error_code ec;
        auto remoteEndpoint = session->sslSocket()->lowest_layer().remote_endpoint(ec);
        CHANNEL_LOG(TRACE) << LOG_DESC("Receive new connection")
                           << LOG_KV("from", remoteEndpoint.address().to_string()) << ":"
                           << remoteEndpoint.port();

        session->setHost(remoteEndpoint.address().to_string());
        session->setPort(remoteEndpoint.port());

        if (m_enableSSL)
        {
            CHANNEL_LOG(TRACE) << LOG_DESC("Start SSL handshake");
            std::shared_ptr<std::string> sdkPublicKey = std::make_shared<std::string>();
            session->sslSocket()->set_verify_callback(newVerifyCallback(sdkPublicKey));
            session->sslSocket()->async_handshake(boost::asio::ssl::stream_base::server,
                boost::bind(&ChannelServer::onHandshake, shared_from_this(),
                    boost::asio::placeholders::error, sdkPublicKey, session));
        }
        else
        {
            CHANNEL_LOG(TRACE) << LOG_DESC("Call connectionHandler");

            if (m_connectionHandler)
            {
                m_connectionHandler(ChannelException(), session);
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


std::function<bool(bool, boost::asio::ssl::verify_context&)>
dev::channel::ChannelServer::newVerifyCallback(std::shared_ptr<std::string> _sdkPublicKey)
{
    auto server = shared_from_this();
    return [server, _sdkPublicKey](bool preverified, boost::asio::ssl::verify_context& ctx) {
        try
        {
            /// return early when the certificate is invalid
            if (!preverified)
            {
                return false;
            }
            /// get the object points to certificate
            X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
            if (!cert)
            {
                CHANNEL_LOG(ERROR) << LOG_DESC("Get cert failed");
                return preverified;
            }

            int crit = 0;
            BASIC_CONSTRAINTS* basic =
                (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, &crit, NULL);
            if (!basic)
            {
                CHANNEL_LOG(ERROR) << LOG_DESC("Get ca basic failed");
                return preverified;
            }
            /// ignore ca
            if (basic->ca)
            {
                // ca or agency certificate
                CHANNEL_LOG(TRACE) << LOG_DESC("Ignore CA certificate");
                BASIC_CONSTRAINTS_free(basic);
                return preverified;
            }

            BASIC_CONSTRAINTS_free(basic);

            /// get issuer name
            if (server->m_checkCertIssuer)
            {
                const char* issuerName = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
                std::string issuer(issuerName);
                OPENSSL_free((void*)issuerName);

                if (issuer != server->m_certIssuerName)
                {
                    CHANNEL_LOG(ERROR)
                        << LOG_DESC("The issuer of the two certificates are inconsistent.")
                        << LOG_KV("sdk certificate issuer", issuer)
                        << LOG_KV("node certificate issuer", server->m_certIssuerName);
                    return false;
                }
            }

            dev::network::getPublicKeyFromCert(_sdkPublicKey, cert);

            return preverified;
        }
        catch (std::exception& e)
        {
            CHANNEL_LOG(ERROR) << LOG_DESC("Cert verify failed")
                               << boost::diagnostic_information(e);
            return preverified;
        }
    };
}

void dev::channel::ChannelServer::startAccept()
{
    try
    {
        ChannelSession::Ptr session = std::make_shared<ChannelSession>();
        session->setThreadPool(m_requestThreadPool, m_responseThreadPool);
        session->setIOService(m_ioService);
        session->setEnableSSL(m_enableSSL);
        session->setMessageFactory(m_messageFactory);

        session->setSSLSocket(
            std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket> >(
                *m_ioService, *m_sslContext));

        m_acceptor->async_accept(session->sslSocket()->lowest_layer(),
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

        if (m_acceptor->is_open())
        {
            m_acceptor->cancel();
            m_acceptor->close();
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
        m_ioService->stop();
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << LOG_DESC("Close ioService")
                           << LOG_KV("what", boost::diagnostic_information(e));
    }
    m_serverThread->join();
}

void dev::channel::ChannelServer::onHandshake(const boost::system::error_code& error,
    std::shared_ptr<std::string> _sdkPublicKey, ChannelSession::Ptr session)
{
    try
    {
        if (!error)
        {
            if (m_connectionHandler)
            {
                m_connectionHandler(ChannelException(), session);
            }
            else
            {
                CHANNEL_LOG(ERROR) << LOG_DESC("connectionHandler empty");
            }

            if (!_sdkPublicKey->empty())
            {
                session->setRemotePublicKey(dev::h512(*_sdkPublicKey));
            }

            CHANNEL_LOG(DEBUG) << LOG_DESC("Channel Server SSL handshake success")
                               << LOG_KV("pubKey", *_sdkPublicKey);
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
