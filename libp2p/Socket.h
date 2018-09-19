/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Socket.h
 * @author toxotguo
 * @date 2018
 *
 * @ author: yujiechen
 * @ date: 2018-09-17
 * @ modification: rename RLPXSocket.h to Socket.h
 */

#pragma once

#include "CertificateServer.h"
#include "Common.h"
#include "SocketFace.h"
#include <libdevcore/FileSystem.h>
#include <libdevcore/easylog.h>
#include <openssl/ec.h>
#include <openssl/ssl.h>
#include <boost/filesystem.hpp>

using namespace dev::eth;
using namespace dev::p2p;

namespace dev
{
namespace p2p
{
class Socket : public SocketFace, public std::enable_shared_from_this<Socket>
{
public:
    Socket(ba::io_service& _ioService, NodeIPEndpoint _nodeIPEndpoint)
      : m_nodeIPEndpoint(_nodeIPEndpoint)
    {
        try
        {
            Socket::initContext();
            m_sslSocket = std::make_shared<ba::ssl::stream<bi::tcp::socket> >(
                _ioService, Socket::m_sslContext);
        }
        catch (Exception const& _e)
        {
            LOG(ERROR) << "ERROR: " << diagnostic_information(_e);
            LOG(ERROR) << "Ssl Socket Init Fail! Please Check CERTIFICATE!";
        }
        LOG(INFO) << "CERTIFICATE LOAD SUC!";
    }
    ~Socket() { close(); }
    bool isConnected() const { return m_sslSocket->lowest_layer().is_open(); }
    virtual void close()
    {
        try
        {
            boost::system::error_code ec;
            m_sslSocket->lowest_layer().shutdown(bi::tcp::socket::shutdown_both, ec);
            if (m_sslSocket->lowest_layer().is_open())
                m_sslSocket->lowest_layer().close();
        }
        catch (...)
        {
        }
    }

    bi::tcp::endpoint remoteEndpoint()
    {
        boost::system::error_code ec;
        return m_sslSocket->lowest_layer().remote_endpoint(ec);
    }

    bi::tcp::socket& ref() { return m_sslSocket->next_layer(); }
    ba::ssl::stream<bi::tcp::socket>& sslref() { return *m_sslSocket; }
    static void initContext()
    {
        if (!Socket::m_isInit)
        {
            vector<pair<string, Public> > certificates;
            string nodePrivateKey;

            m_sslContext.set_options(boost::asio::ssl::context::default_workarounds |
                                     boost::asio::ssl::context::no_sslv2 |
                                     boost::asio::ssl::context::no_sslv3 |
                                     boost::asio::ssl::context::no_tlsv1);

            CertificateServer::GetInstance().getCertificateList(certificates, nodePrivateKey);

#if defined(SSL_CTX_set_ecdh_auto)
            SSL_CTX_set_ecdh_auto(m_sslContext.native_handle(), 1);
            LOG(INFO) << "use SSL_CTX_set_ecdh_auto";
#else

            EC_KEY* ecdh = EC_KEY_new_by_curve_name(NID_secp256k1);
            SSL_CTX_set_tmp_ecdh(m_sslContext.native_handle(), ecdh);
            EC_KEY_free(ecdh);
            LOG(INFO) << "use SSL_CTX_set_tmp_ecdh";
#endif
            m_sslContext.set_verify_depth(3);
            m_sslContext.set_verify_mode(ba::ssl::verify_peer);

            //-------------------------------------------
            // certificates[0]: root certificate
            // certificates[1]: sign certificate of agency
            // certificates[2]: sign certificate of node
            //--------------------------------------------
            //==== add root certificate to authority
            if (certificates[0].first != "")
            {
                m_sslContext.add_certificate_authority(boost::asio::const_buffer(
                    certificates[0].first.c_str(), certificates[0].first.size()));
            }
            else
            {
                LOG(ERROR) << "Ssl Socket Init Fail! Please Check ca.crt !";
                exit(-1);
            }
            if (certificates[1].first == "")
            {
                LOG(ERROR) << "Ssl Socket Init Fail! Please Check agency.crt !";
                exit(-1);
            }
            if (certificates[2].first == "")
            {
                LOG(ERROR) << "Ssl Socket Init Fail! Please Check node.crt !";
                exit(-1);
            }
            //===== set certificates chain:
            // root certificate <--- sign certificate of agency
            string chain = certificates[0].first + certificates[1].first;
            m_sslContext.use_certificate_chain(
                boost::asio::const_buffer(chain.c_str(), chain.size()));
            //==== load sign certificate of node
            m_sslContext.use_certificate(boost::asio::const_buffer(certificates[2].first.c_str(),
                                             certificates[2].first.size()),
                ba::ssl::context::file_format::pem);

            if (nodePrivateKey != "")
            {
                //=== load private key of node certificate
                m_sslContext.use_private_key(
                    boost::asio::const_buffer(nodePrivateKey.c_str(), nodePrivateKey.size()),
                    ba::ssl::context_base::pem);
            }
            else
            {
                LOG(ERROR) << "Ssl Socket Init Fail! Please Check node.key !";
                exit(-1);
            }
            Socket::m_isInit = true;
        }
    }

    const NodeIPEndpoint& nodeIPEndpoint() const { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint) { m_nodeIPEndpoint = _nodeIPEndpoint; }
    boost::asio::ip::tcp::endpoint remote_endpoint() { return ref().remote_endpoint(); }
    static ba::ssl::context m_sslContext;
    static bool m_isInit;

protected:
    NodeIPEndpoint m_nodeIPEndpoint;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket> > m_sslSocket;
};

}  // namespace p2p
}  // namespace dev
