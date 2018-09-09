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
/**
 * @file: CertificateServer.cpp
 * @author: toxotguo
 * @date: 2018
 */
#include "CertificateServer.h"
#include <libdevcore/FileSystem.h>

using namespace std;
using namespace dev;
using namespace dev::eth;

void CertificateServer::loadEcdsaFile()
{
    auto caCrt = contents(getDataDir().string() + "/ca.crt");
    m_ca = asString(caCrt);
    auto agencyCrt = contents(getDataDir().string() + "/agency.crt");
    m_agency = asString(agencyCrt);
    auto nodeCrt = contents(getDataDir().string() + "/node.crt");
    m_node = asString(nodeCrt);

    auto nodeKey = contents(getDataDir().string() + "/node.key");
    m_nodePri = asString(nodeKey);

    if (m_ca.empty() || m_agency.empty() || nodeCrt.empty() || nodeKey.empty())
    {
        LOG(ERROR)
            << "CertificateServer Init Fail! ca.crt or agency.crt or node.crt or node.key File !";
        exit(-1);
    }

    auto nodePrivate = contents(getDataDir().string() + "/node.private");
    string pri = asString(nodePrivate);
    if (pri.size() >= 64)
    {
        m_keyPair = KeyPair(Secret(fromHex(pri.substr(0, 64))));
        LOG(INFO) << " CertificateServer Load KeyPair " << toPublic(m_keyPair.secret());
    }
    else
    {
        LOG(ERROR) << "CertificateServer Load KeyPair Fail! Please Check node.private File.";
        exit(-1);
    }
}
CertificateServer::CertificateServer()
{
    loadEcdsaFile();
}

/**
 * @brief : get CA/agency/node certificates
 * @param certificates: vector that stores certificate information
 *        element 0: information of CA certificate
 *        element 1: information of agency certificate
 *        element 2: information of node certificate
 * @param nodepri: private key of the node
 */
void CertificateServer::getCertificateList(
    vector<pair<string, Public> >& certificates, string& nodePrivate)
{
    certificates.clear();
    certificates.push_back(make_pair(m_ca, m_caPub));
    certificates.push_back(make_pair(m_agency, m_agencyPub));
    certificates.push_back(make_pair(m_node, m_nodePub));

    nodePrivate = m_nodePri;
}

Signature CertificateServer::sign(h256 const& data)
{
    return dev::sign(m_keyPair.secret(), data);
}

CertificateServer& CertificateServer::GetInstance()
{
    static CertificateServer cs;
    return cs;
}
