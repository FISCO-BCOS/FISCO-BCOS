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
#include <boost/algorithm/string/replace.hpp>

using namespace dev;
using namespace dev::initializer;

void SecureInitiailizer::initConfig(boost::property_tree::ptree const& _pt)
{
    m_SSLContext = std::make_shared<bas::context>(bas::context::tlsv12);

    loadFile(_pt);

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

    //==== add root certificate to authority
    if (m_ca != "")
    {
        m_SSLContext->add_certificate_authority(
            boost::asio::const_buffer(m_ca.c_str(), m_ca.size()));
    }
    else
    {
        LOG(ERROR) << "SecureInitiailizer::initConfig fail! Please Check ca.crt!";
        exit(-1);
    }
    if (m_agency == "")
    {
        LOG(ERROR) << "SecureInitiailizer::initConfig fail! Please Check agency.crt!";
        exit(-1);
    }
    if (m_node == "")
    {
        LOG(ERROR) << "SecureInitiailizer::initConfig fail! Please Check node.crt!";
        exit(-1);
    }
    //===== set certificates chain:
    // root certificate <--- sign certificate of agency
    std::string chain = m_ca + m_agency;
    m_SSLContext->use_certificate_chain(boost::asio::const_buffer(chain.c_str(), chain.size()));
    //==== load sign certificate of node
    m_SSLContext->use_certificate(
        boost::asio::const_buffer(m_node.c_str(), m_node.size()), bas::context::file_format::pem);

    if (m_nodeKey != "")
    {
        //=== load private key of node certificate
        m_SSLContext->use_private_key(
            boost::asio::const_buffer(m_nodeKey.c_str(), m_nodeKey.size()), bas::context_base::pem);
    }
    else
    {
        LOG(ERROR) << "SecureInitiailizer::initConfig fail! Please Check node.key!";
        exit(-1);
    }
}

void SecureInitiailizer::loadFile(boost::property_tree::ptree const& _pt)
{
    std::string caCert = _pt.get<std::string>("secure.ca_cert", "${DATAPATH}/ca.crt");
    std::string agencyCert = _pt.get<std::string>("secure.agency_cert", "${DATAPATH}/agency.crt");
    std::string nodeCert = _pt.get<std::string>("secure.node_cert", "${DATAPATH}/node.crt");
    std::string nodeKey = _pt.get<std::string>("secure.node_key", "${DATAPATH}/node.key");
    std::string nodePri = _pt.get<std::string>("secure.node_pri", "${DATAPATH}/node.private");

    completePath(caCert);
    completePath(agencyCert);
    completePath(nodeCert);
    completePath(nodeKey);
    completePath(nodePri);

    m_ca = asString(contents(caCert));
    m_agency = asString(contents(agencyCert));
    m_node = asString(contents(nodeCert));
    m_nodeKey = asString(contents(nodeKey));

    LOG(INFO) << "caCert:" << caCert << "," << m_ca;
    LOG(INFO) << "agencyCert:" << agencyCert << "," << m_agency;
    LOG(INFO) << "nodeCert:" << nodeCert << "," << m_node;
    LOG(INFO) << "nodePri:" << nodeKey << "," << m_nodeKey;

    if (m_ca.empty() || m_agency.empty() || m_node.empty() || m_nodeKey.empty())
    {
        LOG(ERROR) << "Init Fail! ca.crt or agency.crt or node.crt or node.key File !";
        exit(-1);
    }

    std::string pri = asString(contents(nodePri));
    if (pri.size() >= 64)
    {
        m_keyPair = KeyPair(Secret(fromHex(pri.substr(0, 64))));
        LOG(INFO) << "Load KeyPair " << toPublic(m_keyPair.secret());
    }
    else
    {
        LOG(ERROR) << "Load KeyPair Fail! Please Check node.private File.";
        exit(-1);
    }
}

void SecureInitiailizer::completePath(std::string& _path)
{
    boost::algorithm::replace_first(_path, "${DATAPATH}", m_dataPath + "/");
}