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
/** @file SecureInitiailizer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#include "SecureInitiailizer.h"

using namespace dev;
using namespace dev::initializer;

void SecureInitiailizer::initConfig(boost::property_tree::ptree const& _pt)
{
    m_SSLContext = std::make_shared<bas::context>(bas::context::tlsv12);

    vector<pair<string, Public> > certificates;
    string nodePrivateKey;

    m_SSLContext->set_options(bas::context::default_workarounds | bas::context::no_sslv2 |
                              bas::context::no_sslv3 | bas::context::no_tlsv1);

    CertificateServer::GetInstance().getCertificateList(certificates, nodePrivateKey);

#if defined(SSL_CTX_set_ecdh_auto)
    SSL_CTX_set_ecdh_auto(m_SSLContext->native_handle(), 1);
    LOG(INFO) << "SecureInitiailizer::initConfig use SSL_CTX_set_ecdh_auto";
#else

    EC_KEY* ecdh = EC_KEY_new_by_curve_name(NID_secp256k1);
    SSL_CTX_set_tmp_ecdh(m_SSLContext->native_handle(), ecdh);
    EC_KEY_free(ecdh);
    LOG(INFO) << "SecureInitiailizer::initConfig use SSL_CTX_set_tmp_ecdh";
#endif
    m_SSLContext->set_verify_depth(3);
    m_SSLContext->set_verify_mode(bas::verify_peer);

    //-------------------------------------------
    // certificates[0]: root certificate
    // certificates[1]: sign certificate of agency
    // certificates[2]: sign certificate of node
    //--------------------------------------------
    //==== add root certificate to authority
    if (certificates[0].first != "")
    {
        m_SSLContext->add_certificate_authority(
            boost::asio::const_buffer(certificates[0].first.c_str(), certificates[0].first.size()));
    }
    else
    {
        LOG(ERROR) << "SecureInitiailizer::initConfig fail! Please Check ca.crt!";
        exit(-1);
    }
    if (certificates[1].first == "")
    {
        LOG(ERROR) << "SecureInitiailizer::initConfig fail! Please Check agency.crt!";
        exit(-1);
    }
    if (certificates[2].first == "")
    {
        LOG(ERROR) << "SecureInitiailizer::initConfig fail! Please Check node.crt!";
        exit(-1);
    }
    //===== set certificates chain:
    // root certificate <--- sign certificate of agency
    string chain = certificates[0].first + certificates[1].first;
    m_SSLContext->use_certificate_chain(boost::asio::const_buffer(chain.c_str(), chain.size()));
    //==== load sign certificate of node
    m_SSLContext->use_certificate(
        boost::asio::const_buffer(certificates[2].first.c_str(), certificates[2].first.size()),
        bas::context::file_format::pem);

    if (nodePrivateKey != "")
    {
        //=== load private key of node certificate
        m_SSLContext->use_private_key(
            boost::asio::const_buffer(nodePrivateKey.c_str(), nodePrivateKey.size()),
            bas::context_base::pem);
    }
    else
    {
        LOG(ERROR) << "SecureInitiailizer::initConfig fail! Please Check node.key!";
        exit(-1);
    }
}