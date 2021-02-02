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
#include "Common.h"   // for HOST_LOG
#include "Session.h"  // for Sessio...
#include "libdevcore/CommonData.h"
#include "libdevcore/Guards.h"         // for Guard
#include "libdevcore/ThreadPool.h"     // for Thread...
#include "libnetwork/ASIOInterface.h"  // for ASIOIn...
#include "libnetwork/SocketFace.h"     // for Socket...
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

using namespace std;
using namespace dev;
using namespace dev::network;
using namespace dev::eth;
using namespace dev::crypto;

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
        HOST_LOG(INFO) << LOG_DESC("P2P StartAccept") << LOG_KV("Host", m_listenHost) << ":"
                       << m_listenPort;
        auto socket = m_asioInterface->newSocket(NodeIPEndpoint());
        // get and set the accepted endpoint to socket(client endpoint)
        /// define callback after accept connections
        m_asioInterface->asyncAccept(
            socket,
            [=](boost::system::error_code ec) {
                /// get the endpoint information of remote client after accept the connections
                auto endpoint = socket->remoteEndpoint();
                HOST_LOG(TRACE) << LOG_DESC("P2P Recv Connect, From=") << endpoint;
                /// network acception failed
                if (ec || !m_run)
                {
                    HOST_LOG(ERROR) << "Error: " << ec;
                    socket->close();
                    startAccept();

                    return;
                }

                /// if the connected peer over the limitation, drop socket
                socket->setNodeIPEndpoint(endpoint);
                HOST_LOG(INFO) << LOG_DESC("P2P Recv Connect, From=") << endpoint;
                /// register ssl callback to get the NodeID of peers
                std::shared_ptr<std::string> endpointPublicKey = std::make_shared<std::string>();
                m_asioInterface->setVerifyCallback(socket, newVerifyCallback(endpointPublicKey));
                m_asioInterface->asyncHandshake(socket, ba::ssl::stream_base::server,
                    boost::bind(&Host::handshakeServer, shared_from_this(), ba::placeholders::error,
                        endpointPublicKey, socket));

                startAccept();
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
 * modifications 2019.03.20: append subject name and issuer name after nodeIDOut for demand of
 * fisco-bcos-browser
 */
std::function<bool(bool, boost::asio::ssl::verify_context&)> Host::newVerifyCallback(
    std::shared_ptr<std::string> nodeIDOut)
{
    auto host = shared_from_this();
    return [host, nodeIDOut](bool preverified, boost::asio::ssl::verify_context& ctx) {
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
                HOST_LOG(ERROR) << LOG_DESC("Get cert failed");
                return preverified;
            }

            int crit = 0;
            BASIC_CONSTRAINTS* basic =
                (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, &crit, NULL);
            if (!basic)
            {
                HOST_LOG(ERROR) << LOG_DESC("Get ca basic failed");
                return preverified;
            }
            /// ignore ca
            if (basic->ca)
            {
                // ca or agency certificate
                HOST_LOG(TRACE) << LOG_DESC("Ignore CA certificate");
                BASIC_CONSTRAINTS_free(basic);
                return preverified;
            }

            BASIC_CONSTRAINTS_free(basic);
            if (!getPublicKeyFromCert(nodeIDOut, cert))
            {
                return preverified;
            }

            /// check nodeID in certBlacklist, only filter by nodeID.
            const std::vector<std::string>& certBlacklist = host->certBlacklist();
            std::string nodeID = boost::to_upper_copy(*nodeIDOut);
            if (find(certBlacklist.begin(), certBlacklist.end(), nodeID) != certBlacklist.end())
            {
                HOST_LOG(INFO) << LOG_DESC("NodeID in certificate blacklist")
                               << LOG_KV("nodeID", NodeID(nodeID).abridged());
                return false;
            }

            // check nodeID in certWhitelist, only filter by nodeID.
            if (host->whitelist() != nullptr && !host->whitelist()->has(nodeID))
            {
                HOST_LOG(INFO) << LOG_DESC("NodeID is not in certificate whitelist")
                               << LOG_KV("nodeID", NodeID(nodeID).abridged());
                return false;
            }


            /// append cert-name and issuer name after node ID
            /// get subject name
            const char* certName = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
            /// get issuer name
            const char* issuerName = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
            /// format: {nodeID}#{issuer-name}#{cert-name}
            nodeIDOut->append("#");
            nodeIDOut->append(issuerName);
            nodeIDOut->append("#");
            nodeIDOut->append(certName);
            OPENSSL_free((void*)certName);
            OPENSSL_free((void*)issuerName);
            return preverified;
        }
        catch (std::exception& e)
        {
            HOST_LOG(ERROR) << LOG_DESC("Cert verify failed") << boost::diagnostic_information(e);
            return preverified;
        }
    };
}

NodeInfo Host::nodeInfo()
{
    try
    {
        if (m_nodeInfo.nodeID == h512())
        {
            /// get certificate
            auto sslContext = m_asioInterface->sslContext()->native_handle();
            X509* cert = SSL_CTX_get0_certificate(sslContext);

            /// get issuer name
            const char* issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
            std::string issuerName(issuer);

            /// get subject name
            const char* subject = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
            std::string subjectName(subject);

            /// get nodeID
            std::shared_ptr<std::string> nodeIDOut = std::make_shared<std::string>();
            if (getPublicKeyFromCert(nodeIDOut, cert))
            {
                std::string nodeID = boost::to_upper_copy(*nodeIDOut);
                m_nodeInfo.nodeID = NodeID(nodeID);
            }

            /// fill in the node informations
            m_nodeInfo.agencyName = obtainCommonNameFromSubject(issuerName);
            m_nodeInfo.nodeName = obtainCommonNameFromSubject(subjectName);

            /// free resources
            OPENSSL_free((void*)issuer);
            OPENSSL_free((void*)subject);
        }
    }
    catch (std::exception& e)
    {
        HOST_LOG(ERROR) << LOG_DESC("Get node information from cert failed.")
                        << boost::diagnostic_information(e);
        return m_nodeInfo;
    }
    return m_nodeInfo;
}

/**
 * @brief: obtain the common name from the subject of certificate
 *
 * @param subject : the subject of the certificat
 *   the subject format is: /CN=xx/O=xxx/OU=xxx/ commonly
 * @return std::string: the common name of the certificate
 */
std::string Host::obtainCommonNameFromSubject(std::string const& subject)
{
    std::vector<std::string> fields;
    boost::split(fields, subject, boost::is_any_of("/"), boost::token_compress_on);
    for (auto field : fields)
    {
        std::size_t pos = field.find("CN");
        if (pos != std::string::npos)
        {
            std::vector<std::string> cn_fields;
            boost::split(cn_fields, field, boost::is_any_of("="), boost::token_compress_on);
            /// use the whole fields as the common name
            if (cn_fields.size() < 2)
            {
                return field;
            }
            // the key field must be CN
            if (dev::stringCmpIgnoreCase(cn_fields[0], "CN") != 0)
            {
                continue;
            }
            /// return real common name
            return cn_fields[1];
        }
    }
    return subject;
}

/// obtain nodeInfo from given vector
void Host::obtainNodeInfo(NodeInfo& info, std::string const& node_info)
{
    std::vector<std::string> node_info_vec;
    boost::split(node_info_vec, node_info, boost::is_any_of("#"), boost::token_compress_on);
    if (node_info_vec.size() > 0)
    {
        info.nodeID = NodeID(node_info_vec[0]);
    }
    if (node_info_vec.size() > 1)
    {
        info.agencyName = obtainCommonNameFromSubject(node_info_vec[1]);
    }
    if (node_info_vec.size() > 2)
    {
        info.nodeName = obtainCommonNameFromSubject(node_info_vec[2]);
    }
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
    if (error)
    {
        HOST_LOG(WARNING) << LOG_DESC("handshakeServer Handshake failed")
                          << LOG_KV("errorValue", error.value())
                          << LOG_KV("message", error.message())
                          << LOG_KV("endpoint", socket->nodeIPEndpoint());
        socket->close();
        return;
    }
    if (endpointPublicKey->empty())
    {
        HOST_LOG(WARNING) << LOG_DESC("handshakeServer get nodeID failed")
                          << LOG_KV("remote endpoint", socket->remoteEndpoint());
        socket->close();
        return;
    }
    if (m_run)
    {
        /// node info splitted with #
        /// format: {nodeId}{#}{agencyName}{#}{nodeName}
        std::string node_info(*endpointPublicKey);
        NodeInfo info;
        obtainNodeInfo(info, node_info);
        HOST_LOG(INFO) << LOG_DESC("handshakeServer succ")
                       << LOG_KV("remote endpoint", socket->remoteEndpoint());
        startPeerSession(info, socket, m_connectionHandler);
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
// TODO: asyncConnect pass handle to startPeerSession, make use of it
void Host::startPeerSession(NodeInfo const& nodeInfo, std::shared_ptr<SocketFace> const& socket,
    std::function<void(NetworkException, NodeInfo const&, std::shared_ptr<SessionFace>)>)
{
    auto weakHost = std::weak_ptr<Host>(shared_from_this());
    std::shared_ptr<SessionFace> ps =
        m_sessionFactory->create_session(weakHost, socket, m_messageFactory);

    auto connectionHandler = m_connectionHandler;
    m_threadPool->enqueue([ps, connectionHandler, nodeInfo]() {
        if (connectionHandler)
        {
            connectionHandler(NetworkException(0, ""), nodeInfo, ps);
        }
        else
        {
            HOST_LOG(WARNING) << LOG_DESC("No connectionHandler, new connection may lost");
        }
    });
    HOST_LOG(INFO) << LOG_DESC("startPeerSession, Remote=") << socket->remoteEndpoint()
                   << LOG_KV("local endpoint", socket->localEndpoint())
                   << LOG_KV("nodeID", nodeInfo.nodeID.abridged());
}

/**
 * @brief: remove expired timer
 *         modify alived peers to m_peers
 *         reconnect all nodes recorded in m_staticNodes periodically
 */
void Host::start()
{
    /// if the p2p network has been stoped, then stop related service
    if (!haveNetwork())
    {
        m_run = true;
        m_asioInterface->init(m_listenHost, m_listenPort);
        m_hostThread = std::make_shared<std::thread>([&] {
            dev::pthread_setThreadName("io_service");
            while (haveNetwork())
            {
                try
                {
                    if (asioInterface()->acceptor())
                    {
                        startAccept();
                    }
                    asioInterface()->run();
                }
                catch (std::exception& e)
                {
                    HOST_LOG(WARNING) << LOG_DESC("Exception in Host Thread:")
                                      << boost::diagnostic_information(e);
                }

                asioInterface()->reset();
            }

            HOST_LOG(INFO) << "Host exit";
        });
    }
}

/**
 * @brief : connect to the server
 * @param _nodeIPEndpoint : the endpoint of the connected server
 */
void Host::asyncConnect(NodeIPEndpoint const& _nodeIPEndpoint,
    std::function<void(NetworkException, NodeInfo const&, std::shared_ptr<SessionFace>)> callback)
{
    if (!m_run)
    {
        return;
    }
    HOST_LOG(INFO) << LOG_DESC("Connecting to node") << LOG_KV("endpoint", _nodeIPEndpoint);
    {
        Guard l(x_pendingConns);
        if (m_pendingConns.count(_nodeIPEndpoint))
        {
            LOG(TRACE) << LOG_DESC("asyncConnected node is in the pending list")
                       << LOG_KV("endpoint", _nodeIPEndpoint);
            return;
        }
    }

    std::shared_ptr<SocketFace> socket = m_asioInterface->newSocket(_nodeIPEndpoint);
    /// if async connect timeout, close the socket directly
    auto connect_timer = std::make_shared<boost::asio::deadline_timer>(
        *(m_asioInterface->ioService()), boost::posix_time::milliseconds(m_connectTimeThre));
    connect_timer->async_wait([=](const boost::system::error_code& error) {
        /// return when cancel has been called
        if (error == boost::asio::error::operation_aborted)
        {
            HOST_LOG(DEBUG) << LOG_DESC("AsyncConnect handshake handler revoke this operation");
            return;
        }
        /// connection timer error
        if (error && error != boost::asio::error::operation_aborted)
        {
            HOST_LOG(ERROR) << LOG_DESC("AsyncConnect timer failed")
                            << LOG_KV("errorValue", error.value())
                            << LOG_KV("message", error.message());
        }
        if (socket->isConnected())
        {
            LOG(WARNING) << LOG_DESC("AsyncConnect timeout erase")
                         << LOG_KV("endpoint", _nodeIPEndpoint);
            erasePendingConns(_nodeIPEndpoint);
            socket->close();
        }
    });
    /// callback async connect
    m_asioInterface->asyncResolveConnect(socket, [=](boost::system::error_code const& ec) {
        if (ec)
        {
            HOST_LOG(ERROR) << LOG_DESC("TCP Connection refused by node")
                            << LOG_KV("endpoint", _nodeIPEndpoint)
                            << LOG_KV("message", ec.message());
            socket->close();

            m_threadPool->enqueue([callback, _nodeIPEndpoint]() {
                callback(NetworkException(ConnectError, "Connect failed"), NodeInfo(),
                    std::shared_ptr<SessionFace>());
            });
            return;
        }
        else
        {
            insertPendingConns(_nodeIPEndpoint);
            /// get the public key of the server during handshake
            std::shared_ptr<std::string> endpointPublicKey = std::make_shared<std::string>();
            m_asioInterface->setVerifyCallback(socket, newVerifyCallback(endpointPublicKey));
            /// call handshakeClient after handshake succeed
            m_asioInterface->asyncHandshake(socket, ba::ssl::stream_base::client,
                boost::bind(&Host::handshakeClient, shared_from_this(), ba::placeholders::error,
                    socket, endpointPublicKey, callback, _nodeIPEndpoint, connect_timer));
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
    std::function<void(NetworkException, NodeInfo const&, std::shared_ptr<SessionFace>)> callback,
    NodeIPEndpoint _nodeIPEndpoint, std::shared_ptr<boost::asio::deadline_timer> timerPtr)
{
    timerPtr->cancel();
    erasePendingConns(_nodeIPEndpoint);
    if (error)
    {
        HOST_LOG(WARNING) << LOG_DESC("handshakeClient failed")
                          << LOG_KV("endpoint", _nodeIPEndpoint)
                          << LOG_KV("errorValue", error.value())
                          << LOG_KV("message", error.message());

        if (socket->isConnected())
        {
            socket->close();
        }
        return;
    }
    if (endpointPublicKey->empty())
    {
        HOST_LOG(WARNING) << LOG_DESC("handshakeClient get nodeID failed")
                          << LOG_KV("local endpoint", socket->localEndpoint());
        socket->close();
        return;
    }

    if (m_run)
    {
        std::string node_info(*endpointPublicKey);
        NodeInfo info;
        obtainNodeInfo(info, node_info);
        HOST_LOG(INFO) << LOG_DESC("handshakeClient succ")
                       << LOG_KV("local endpoint", socket->localEndpoint());
        startPeerSession(info, socket, callback);
    }
}

/// stop the network and worker thread
void Host::stop()
{
    // ignore if already stopped/stopping
    if (!m_run)
        return;
    // signal run() to prepare for shutdown and reset m_timer
    m_run = false;
    m_asioInterface->stop();
    m_hostThread->join();
    if (m_threadPool)
    {
        m_threadPool->stop();
    }
}
