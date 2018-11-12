/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Host.cpp
 * @author Alex Leverington <nessence@gmail.com>
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 * @author toxotguo
 * @date 2018
 *
 * @ author: yujiechen
 * @ date: 2018-09-19
 * @ modifications:
 *  1. modify io_service value from 1 to 2
 * (construction of io_service is io_service(std::size_t concurrency_hint);)
 * (currenncy_hint means that "A suggestion to the implementation on how many threads it should
 * allow to run simultaneously.") (since ethereum use 2, we modify io_service from 1 to 2) 2.
 *
 * @ author: chaychen
 * @ date: 2018-09-27
 * @ modifications:
 * Add send topicSeq
 */
#include "Host.h"

#include <libdevcore/Assertions.h>
#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/Exceptions.h>
#include <libdevcore/FileSystem.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Rsa.h>
#include <libethcore/CommonJS.h>
#include <boost/algorithm/string.hpp>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <functional>

#include "Common.h"
#include "Session.h"

using namespace std;
using namespace dev;
using namespace dev::p2p;
using namespace dev::eth;
using namespace dev::crypto;
namespace dev
{
namespace p2p
{
Host::~Host()
{
    stop();
}

/**
 * @brief: accept connection requests, maily include procedures:
 *         1. async_accept: accept connection requests
 *         2. ssl handshake: obtain node id from the certificate during ssl handshake
 *         3. if ssl handshake success, call 'handshakeServer' to init client socket
 *            and get caps, version of the connecting client, and startPeerSession
 *            (mainly init the caps and session, and update peer related information)
 * @attention: this function is called repeatedly
 */
void Host::startAccept(boost::system::error_code boost_error)
{
    /// accept the connection
    if (m_run)
    {
        LOG(INFO) << "Listening on local port " << m_listenPort << " (public: " << m_listenHost
                  << "), P2P Start Accept";
        auto socket = m_asioInterface->newSocket(NodeIPEndpoint());
        // get and set the accepted endpoint to socket(client endpoint)
        /// define callback after accept connections
        m_asioInterface->asyncAccept(socket, [=](boost::system::error_code ec) {
                /// get the endpoint information of remote client after accept the connections
                auto remoteEndpoint = socket->remote_endpoint();
                LOG(INFO) << "P2P Recv Connect: " << remoteEndpoint.address().to_string() << ":"
                          << remoteEndpoint.port();
                /// network acception failed
                if (ec || !m_run)
                {
                    socket->close();
                    return;
                }

                /// if the connected peer over the limitation, drop socket
                auto endpoint = socket->remoteEndpoint();
                socket->setNodeIPEndpoint(
                    NodeIPEndpoint(endpoint.address(), endpoint.port(), endpoint.port()));
                LOG(DEBUG) << "client port:" << endpoint.port()
                           << "|ip:" << endpoint.address().to_string();
                LOG(DEBUG) << "server port:" << m_listenPort
                           << "|ip:" << m_listenHost;
                /// register ssl callback to get the NodeID of peers
                std::shared_ptr<std::string> endpointPublicKey = std::make_shared<std::string>();
                m_asioInterface->setVerifyCallback(socket, newVerifyCallback(endpointPublicKey));
                m_asioInterface->asyncHandshake(socket, ba::ssl::stream_base::server,
                    boost::bind(&Host::handshakeServer, shared_from_this(), ba::placeholders::error,
                        endpointPublicKey, socket));
            },
            boost_error);
    }
}

/**
 * @brief : functions called after openssl handshake,
 *          maily to get node id and verify whether the certificate has been expired
 * @param nodeIDOut : also return value, pointer points to the node id string
 * @return std::function<bool(bool, boost::asio::ssl::verify_context&)>:
 *  return true: verify success
 *  return false: verify failed
 */
std::function<bool(bool, boost::asio::ssl::verify_context&)> Host::newVerifyCallback(
    std::shared_ptr<std::string> nodeIDOut)
{
    auto host = shared_from_this();
    return [host, nodeIDOut](bool preverified, boost::asio::ssl::verify_context& ctx) {
        try
        {
            /// get the object points to certificate
            X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
            if (!cert)
            {
                LOG(ERROR) << "Get cert failed";
                return preverified;
            }

            int crit = 0;
            BASIC_CONSTRAINTS* basic =
                (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, &crit, NULL);
            if (!basic)
            {
                LOG(ERROR) << "Get ca casic failed";
                return preverified;
            }
            /// ignore ca
            if (basic->ca)
            {
                // ca or agency certificate
                LOG(TRACE) << "Ignore CA certificate";
                return preverified;
            }
            EVP_PKEY* evpPublicKey = X509_get_pubkey(cert);
            if (!evpPublicKey)
            {
                LOG(ERROR) << "Get evpPublicKey failed";
                return preverified;
            }

            ec_key_st* ecPublicKey = EVP_PKEY_get1_EC_KEY(evpPublicKey);
            if (!ecPublicKey)
            {
                LOG(ERROR) << "Get ecPublicKey failed";
                return preverified;
            }
            /// get public key of the certificate
            const EC_POINT* ecPoint = EC_KEY_get0_public_key(ecPublicKey);
            if (!ecPoint)
            {
                LOG(ERROR) << "Get ecPoint failed";
                return preverified;
            }

            std::shared_ptr<char> hex =
                std::shared_ptr<char>(EC_POINT_point2hex(EC_KEY_get0_group(ecPublicKey), ecPoint,
                                          EC_KEY_get_conv_form(ecPublicKey), NULL),
                    [](char* p) { OPENSSL_free(p); });

            if (hex)
            {
                nodeIDOut->assign(hex.get());
                if (nodeIDOut->find("04") == 0)
                {
                    /// remove 04
                    nodeIDOut->erase(0, 2);
                }
                LOG(DEBUG) << "Get endpoint publickey:" << *nodeIDOut;
            }
            return preverified;
        }
        catch (std::exception& e)
        {
            LOG(ERROR) << "Cert verify failed: " << boost::diagnostic_information(e);
            return preverified;
        }
    };
}

/**
 * @brief: server calls handshakeServer to after handshake
 *         mainly calls RLPxHandshake to obtain informations(client version, caps, etc),
 *         start peer session and start accepting procedure repeatedly
 * @param error: error information triggered in the procedure of ssl handshake
 * @param endpointPublicKey: public key obtained from certificate during handshake
 * @param socket: socket related to the endpoint of the connected client
 */
void Host::handshakeServer(const boost::system::error_code& error,
    std::shared_ptr<std::string>& endpointPublicKey, std::shared_ptr<SocketFace> socket)
{
    if(m_run) {
        std::string node_id_str(*endpointPublicKey);
        NodeID nodeID = NodeID(node_id_str);
        /// handshake failed
        if (error)
        {
            LOG(ERROR) << "Host::async_handshake err:" << error.message();
            socket->close();
            return;
        }

        startPeerSession(nodeID, socket, m_connectionHandler);

        startAccept();
    }
}

/**
 * @brief: start peer sessions after handshake succeed(called by RLPxHandshake),
 *         mainly include four functions:
 *         1. disconnect connecting host with invalid capability
 *         2. modify m_peers && disconnect already-connected session
 *         3. modify m_sessions and m_staticNodes
 *         4. start new session (session->start())
 * @param _pub: node id of the connecting client
 * @param _rlp: informations obtained from the client-peer during handshake
 *              now include protocolVersion, clientVersion, caps and listenPort
 * @param _s : connected socket(used to init session object)
 */
void Host::startPeerSession(NodeID nodeID, std::shared_ptr<SocketFace> const& socket, std::function<void(NetworkException, NodeID, std::shared_ptr<SessionFace>)> handler)
{
    auto weakHost = std::weak_ptr<Host>(shared_from_this());
    std::shared_ptr<SessionFace> ps = m_sessionFactory->create_session(weakHost, socket, m_messageFactory);

    auto connectionHandler = m_connectionHandler;
    m_threadPool->enqueue([ps, connectionHandler, nodeID]() {
        if(connectionHandler) {
            connectionHandler(NetworkException(0, ""), nodeID, ps);
        }
        else {
            LOG(WARNING) << "No connectionHandler, new connection may lost";
        }
    });

    LOG(INFO) << "start a new peer: " << nodeID;
}

/**
 * @brief: remove expired timer
 *         modify alived peers to m_peers
 *         reconnect all nodes recorded in m_staticNodes periodically
 */
void Host::start()
{
    /// if the p2p network has been stoped, then stop related service
    if (!m_run)
    {
        m_run = true;
        m_asioInterface->init(m_listenHost, m_listenPort);

        auto self = std::weak_ptr<Host>(shared_from_this());
        m_hostThread = std::make_shared<std::thread>([self]
        {
            auto host = self.lock();
            while(host && host->haveNetwork())
            {
                try
                {
                    if(host->asioInterface()->acceptor()) {
                        host->startAccept();
                    }

                    host->asioInterface()->run();
                }
                catch (std::exception &e)
                {
                    LOG(WARNING) << "Exception in Network Thread:" << boost::diagnostic_information(e);
                }

                host->asioInterface()->reset();
            }

            LOG(WARNING) << "Host exit";
        });

        m_hostThread->detach();
    }
}

/**
 * @brief : connect to the server
 * @param _nodeIPEndpoint : the endpoint of the connected server
 */
void Host::asyncConnect(
        NodeIPEndpoint const& _nodeIPEndpoint,
        std::function<void(NetworkException, NodeID nodeID, std::shared_ptr<SessionFace>)> callback)
{
    if (!m_run) {
        return;
    }

    {
        Guard l(x_pendingNodeConns);
        if (m_pendingPeerConns.count(_nodeIPEndpoint.name())) {
            return;
        }
        m_pendingPeerConns.insert(_nodeIPEndpoint.name());
    }
    LOG(INFO) << "Attempting connection to node "
              << "@" << _nodeIPEndpoint.name();
    std::shared_ptr<SocketFace> socket = m_asioInterface->newSocket(_nodeIPEndpoint);
    // socket.reset(socket);
    auto endpoint = socket->remoteEndpoint();
    socket->sslref().set_verify_mode(ba::ssl::verify_peer);

    /// connect to the server
    m_asioInterface->asyncConnect(socket, _nodeIPEndpoint,
        [=](boost::system::error_code const& ec) {
            if (ec)
            {
                LOG(ERROR) << "Connection refused to node"
                           << "@" << _nodeIPEndpoint.name() << "(" << ec.message() << ")";
                Guard l(x_pendingNodeConns);
                m_pendingPeerConns.erase(_nodeIPEndpoint.name());
                socket->close();
                return;
            }
            else
            {
                /// get the public key of the server during handshake
                std::shared_ptr<std::string> endpointPublicKey = std::make_shared<std::string>();
                m_asioInterface->setVerifyCallback(socket, newVerifyCallback(endpointPublicKey));
                /// call handshakeClient after handshake succeed
                m_asioInterface->asyncHandshake(socket, ba::ssl::stream_base::client,
                    boost::bind(&Host::handshakeClient, shared_from_this(), ba::placeholders::error, socket,
                        endpointPublicKey, callback, _nodeIPEndpoint));
            }
        });
}

/**
 * @brief : start RLPxHandshake procedure after ssl handshake succeed
 * @param error: error returned by ssl handshake
 * @param socket : ssl socket
 * @param endpointPublicKey: public key of the server obtained from the certificate
 * @param _nodeIPEndpoint : endpoint of the server to connect
 */
void Host::handshakeClient(const boost::system::error_code& error,
    std::shared_ptr<SocketFace> socket, std::shared_ptr<std::string>& endpointPublicKey,
    std::function<void(NetworkException, NodeID, std::shared_ptr<SessionFace>)> callback,
    NodeIPEndpoint _nodeIPEndpoint)
{
    if(m_run) {
       std::string node_id_str(*endpointPublicKey);
       NodeID nodeID = NodeID(node_id_str);
       /// handshake failed
       if (error)
       {
           LOG(ERROR) << "Host::async_handshake client err:" << error.message();
           socket->close();
           return;
       }

       startPeerSession(nodeID, socket, callback);
   }
}

/// stop the network and worker thread
void Host::stop()
{
    // called to force io_service to kill any remaining tasks it might have -
    // such tasks may involve socket reads from Capabilities that maintain references
    // to resources we're about to free.
    {
        // Although m_run is set by stop() or start(), it effects m_runTimer so x_runTimer is
        // used instead of a mutex for m_run.
        Guard l(x_runTimer);
        // ignore if already stopped/stopping
        if (!m_run)
            return;
        // signal run() to prepare for shutdown and reset m_timer
        m_run = false;

        m_asioInterface->stop();

        m_hostThread->join();
    }
}

}  // namespace p2p
}  // namespace dev
