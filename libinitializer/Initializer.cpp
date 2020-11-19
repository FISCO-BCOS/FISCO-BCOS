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
#include "Common.h"
#include "GlobalConfigureInitializer.h"

using namespace bcos;
using namespace bcos::initializer;
using namespace bcos::p2p;

void Initializer::init(std::string const& _path)
{
    try
    {
        boost::property_tree::ptree pt;
        boost::property_tree::read_ini(_path, pt);

        /// init log
        m_logInitializer = std::make_shared<LogInitializer>();
        m_logInitializer->initLog(pt);
        /// init global config. must init before DB, for compatibility
        initGlobalConfig(pt);

        // init the statLog
        if (g_BCOSConfig.enableStat())
        {
            m_logInitializer->initStatLog(pt);
        }

        // init certificates
        m_secureInitializer = std::make_shared<SecureInitializer>();
        m_secureInitializer->initConfig(pt);

        m_p2pInitializer = std::make_shared<P2PInitializer>();
        m_p2pInitializer->setSSLContext(
            m_secureInitializer->SSLContext(SecureInitializer::Usage::ForP2P));
        m_p2pInitializer->setKeyPair(m_secureInitializer->keyPair());
        m_p2pInitializer->initConfig(pt);

        m_rpcInitializer = std::make_shared<RPCInitializer>();
        m_rpcInitializer->setP2PService(m_p2pInitializer->p2pService());
        m_rpcInitializer->setSSLContext(
            m_secureInitializer->SSLContext(SecureInitializer::Usage::ForRPC));
        /// must start RPC server here for Ledger initializer depends on AMDB, AMDB depends on RPC
        m_rpcInitializer->initChannelRPCServer(pt);

        m_ledgerInitializer = std::make_shared<LedgerInitializer>();
        m_ledgerInitializer->setP2PService(m_p2pInitializer->p2pService());
        m_ledgerInitializer->setKeyPair(m_secureInitializer->keyPair());
        m_ledgerInitializer->setChannelRPCServer(m_rpcInitializer->channelRPCServer());
        m_ledgerInitializer->initConfig(pt);

        m_rpcInitializer->setLedgerInitializer(m_ledgerInitializer);
        m_rpcInitializer->initConfig(pt);
        m_ledgerInitializer->startAll();
    }
    catch (std::exception& e)
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("Initializer") << LOG_DESC("Init failed")
                               << LOG_KV("EINFO", boost::diagnostic_information(e));
        ERROR_OUTPUT << LOG_BADGE("Initializer") << LOG_DESC("Init failed")
                     << LOG_KV("EINFO", boost::diagnostic_information(e)) << std::endl;
        BOOST_THROW_EXCEPTION(e);
    }
}

Initializer::~Initializer()
{
    /// modify the destructure order to ensure that the log is destructed at last
    /// stop the ledger
    if (m_ledgerInitializer)
    {
        m_ledgerInitializer->stopAll();
    }
    INITIALIZER_LOG(INFO) << LOG_DESC("ledgerInitializer stopped");
    /// stop rpc
    if (m_rpcInitializer)
    {
        m_rpcInitializer->stop();
    }
    INITIALIZER_LOG(INFO) << LOG_DESC("RPCInitializer stopped");
    /// stop p2p
    if (m_p2pInitializer)
    {
        m_p2pInitializer->stop();
    }
    INITIALIZER_LOG(INFO) << LOG_DESC("P2PInitializer stopped");
}
