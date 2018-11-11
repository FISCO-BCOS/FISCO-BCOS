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
/** @file RPCInitiailizer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#pragma once

#include "Common.h"
#include <libchannelserver/ChannelRPCServer.h>
#include <libledger/LedgerManager.h>
#include <libp2p/P2PInterface.h>
#include <librpc/Rpc.h>
#include <librpc/SafeHttpServer.h>

namespace dev
{
namespace initializer
{
class RPCInitiailizer : public std::enable_shared_from_this<RPCInitiailizer>
{
public:
    typedef std::shared_ptr<RPCInitiailizer> Ptr;

    virtual ~RPCInitiailizer()
    {
        if (m_channelRPCHttpServer)
        {
            m_channelRPCHttpServer->StopListening();
            INITIALIZER_LOG(INFO) << "ChannelRPCHttpServer stoped.";
        }
        if (m_jsonrpcHttpServer)
        {
            m_jsonrpcHttpServer->StopListening();
            INITIALIZER_LOG(INFO) << "JsonrpcHttpServer stoped.";
        }
    };

    void initConfig(boost::property_tree::ptree const& _pt);
    void setP2PService(std::shared_ptr<p2p::P2PInterface> _p2pService)
    {
        m_p2pService = _p2pService;
    }
    void setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext)
    {
        m_sslContext = sslContext;
    }
    void setLedgerManager(std::shared_ptr<ledger::LedgerManager> _ledgerManager)
    {
        m_ledgerManager = _ledgerManager;
    }

private:
    std::shared_ptr<p2p::P2PInterface> m_p2pService;
    std::shared_ptr<ledger::LedgerManager> m_ledgerManager;
    std::shared_ptr<boost::asio::ssl::context> m_sslContext;
    std::shared_ptr<dev::SafeHttpServer> m_safeHttpServer;
    ChannelRPCServer::Ptr m_channelRPCServer;
    ModularServer<>* m_channelRPCHttpServer;
    ModularServer<>* m_jsonrpcHttpServer;
};

}  // namespace initializer

}  // namespace dev