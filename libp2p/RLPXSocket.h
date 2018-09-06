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
/** @file RLPXSocket.h
 * @author toxotguo
 * @date 2018
 */

#pragma once

#include "CertificateServer.h"
#include "Common.h"
#include "RLPXSocketApi.h"
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
class RLPXSocketSSL : public RLPXSocketApi, public std::enable_shared_from_this<RLPXSocketSSL>
{
public:
    RLPXSocketSSL(ba::io_service& _ioService, NodeIPEndpoint _nodeIPEndpoint)
      : m_nodeIPEndpoint(_nodeIPEndpoint)
    {
        try
        {
            RLPXSocketSSL::initContext();
            m_sslSocket = std::make_shared<ba::ssl::stream<bi::tcp::socket> >(
                _ioService, RLPXSocketSSL::sslContext);
        }
        catch (Exception const& _e)
        {
            LOG(ERROR) << "ERROR: " << diagnostic_information(_e);
            LOG(ERROR) << "Ssl Socket Init Fail! Please Check CERTIFICATE!";
        }
        LOG(INFO) << "CERTIFICATE LOAD SUC!";
    }
    ~RLPXSocketSSL() { close(); }

    bool isConnected() const { return m_sslSocket->lowest_layer().is_open(); }
    void close()
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
        if (!RLPXSocketSSL::isInit)
        {
            vector<pair<string, Public> > certificates;
            string nodepri;

            sslContext.set_options(boost::asio::ssl::context::default_workarounds |
                                   boost::asio::ssl::context::no_sslv2 |
                                   boost::asio::ssl::context::no_sslv3 |
                                   boost::asio::ssl::context::no_tlsv1);

            CertificateServer::GetInstance().getCertificateList(certificates, nodepri);

            EC_KEY* ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
            SSL_CTX_set_tmp_ecdh(sslContext.native_handle(), ecdh);
            EC_KEY_free(ecdh);

            sslContext.set_verify_depth(3);
            sslContext.set_verify_mode(ba::ssl::verify_peer);

            //-------------------------------------------
            // certificates[0]: root certificate
            // certificates[1]: sign certificate of agency
            // certificates[2]: sign certificate of node
            //--------------------------------------------
            //==== add root certificate to authority
            if (certificates[0].first != "")
            {
                sslContext.add_certificate_authority(boost::asio::const_buffer(
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
            sslContext.use_certificate_chain(
                boost::asio::const_buffer(chain.c_str(), chain.size()));
            //==== load sign certificate of node
            sslContext.use_certificate(boost::asio::const_buffer(certificates[2].first.c_str(),
                                           certificates[2].first.size()),
                ba::ssl::context::file_format::pem);

            if (nodepri != "")
            {
                //=== load private key of node certificate
                sslContext.use_private_key(
                    boost::asio::const_buffer(nodepri.c_str(), nodepri.size()),
                    ba::ssl::context_base::pem);
            }
            else
            {
                LOG(ERROR) << "Ssl Socket Init Fail! Please Check node.key !";
                exit(-1);
            }

            RLPXSocketSSL::isInit = true;
        }
    }

    const NodeIPEndpoint& nodeIPEndpoint() const { return m_nodeIPEndpoint; }
    void setNodeIPEndpoint(NodeIPEndpoint _nodeIPEndpoint) { m_nodeIPEndpoint = _nodeIPEndpoint; }

protected:
    NodeIPEndpoint m_nodeIPEndpoint;
    static ba::ssl::context sslContext;
    static bool isInit;
    std::shared_ptr<ba::ssl::stream<bi::tcp::socket> > m_sslSocket;
};

}  // namespace p2p
}  // namespace dev
