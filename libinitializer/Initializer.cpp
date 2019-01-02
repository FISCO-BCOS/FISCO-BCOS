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
    try
    {
        boost::property_tree::ptree pt;
        boost::property_tree::read_ini(_path, pt);
        /// init log
        m_logInitializer = std::make_shared<LogInitializer>();
        m_logInitializer->initLog(pt);
        /// init certificates
        m_secureInitializer = std::make_shared<SecureInitializer>();
        m_secureInitializer->initConfig(pt);

        m_p2pInitializer = std::make_shared<P2PInitializer>();
        m_p2pInitializer->setSSLContext(m_secureInitializer->SSLContext());
        m_p2pInitializer->setKeyPair(m_secureInitializer->keyPair());
        m_p2pInitializer->initConfig(pt);

        m_ledgerInitializer = std::make_shared<LedgerInitializer>();
        m_ledgerInitializer->setP2PService(m_p2pInitializer->p2pService());
        m_ledgerInitializer->setKeyPair(m_secureInitializer->keyPair());
        m_ledgerInitializer->initConfig(pt);

        m_rpcInitializer = std::make_shared<RPCInitializer>();
        m_rpcInitializer->setP2PService(m_p2pInitializer->p2pService());
        m_rpcInitializer->setSSLContext(m_secureInitializer->SSLContext());
        m_rpcInitializer->setLedgerManager(m_ledgerInitializer->ledgerManager());
        m_rpcInitializer->initConfig(pt);
        m_ledgerInitializer->startAll();
    }
    catch (std::exception& e)
    {
        INITIALIZER_LOG(ERROR) << "[#Initializer::init] load configuration failed! [EINFO]:  "
                               << boost::diagnostic_information(e);
        ERROR_OUTPUT << "[#Initializer] INIT Failed, [ERROR INFO]: "
                     << boost::diagnostic_information(e) << std::endl;
        BOOST_THROW_EXCEPTION(e);
    }
}
