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
/** @file Initializer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#include "Initializer.h"

using namespace dev;
using namespace dev::initializer;

void Initializer::init(std::string const& _path)
{
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(_path, pt);

    m_commonInitializer = std::make_shared<CommonInitializer>();
    m_commonInitializer->initConfig(pt);

    m_secureInitiailizer = std::make_shared<SecureInitializer>();
    m_secureInitiailizer->setDataPath(m_commonInitializer->dataPath());
    m_secureInitiailizer->initConfig(pt);

    m_p2pInitializer = std::make_shared<P2PInitializer>();
    m_p2pInitializer->setSSLContext(m_secureInitiailizer->SSLContext());
    m_p2pInitializer->setKeyPair(m_secureInitiailizer->keyPair());
    m_p2pInitializer->initConfig(pt);

    m_ledgerInitiailizer = std::make_shared<LedgerInitiailizer>();
    m_ledgerInitiailizer->setP2PService(m_p2pInitializer->p2pService());
    m_ledgerInitiailizer->setKeyPair(m_secureInitiailizer->keyPair());
    m_ledgerInitiailizer->initConfig(pt);

    m_rpcInitiailizer = std::make_shared<RPCInitiailizer>();
    m_rpcInitiailizer->setP2PService(m_p2pInitializer->p2pService());
    m_rpcInitiailizer->setSSLContext(m_secureInitiailizer->SSLContext());
    m_rpcInitiailizer->setLedgerManager(m_ledgerInitiailizer->ledgerManager());
    m_rpcInitiailizer->initConfig(pt);
}