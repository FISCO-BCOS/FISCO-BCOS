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
/** @file RPCInitializer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#pragma once
#include "Common.h"  // for INITIALIZER_LOG
#include "LedgerInitializer.h"
#include "librpc/ModularServer.h"               // for ModularServer
#include <libchannelserver/ChannelRPCServer.h>  // for ChannelRPCServer
#include <boost/property_tree/ptree_fwd.hpp>    // for ptree
#include <vector>                               // for vector

namespace boost
{
namespace asio
{
namespace ssl
{
class context;
}
}  // namespace asio
}  // namespace boost


namespace bcos
{
class SafeHttpServer;
namespace ledger
{
class LedgerManager;
}
namespace p2p
{
class P2PInterface;
}
namespace rpc
{
class Rpc;
}
namespace stat
{
class ChannelNetworkStatHandler;
}
namespace initializer
{
class RPCInitializer : public std::enable_shared_from_this<RPCInitializer>
{
public:
    typedef std::shared_ptr<RPCInitializer> Ptr;

    virtual ~RPCInitializer() { stop(); }

    void stop()
    {
        // stop networkStatHandler
        if (m_networkStatHandler)
        {
            m_networkStatHandler->stop();
            INITIALIZER_LOG(INFO) << "stop channelNetworkStatistics.";
        }
        /// stop channel first
        if (m_channelRPCHttpServer)
        {
            m_channelRPCHttpServer->StopListening();
            delete m_channelRPCHttpServer;
            m_channelRPCHttpServer = nullptr;
            INITIALIZER_LOG(INFO) << "ChannelRPCHttpServer deleted.";
        }
        if (m_jsonrpcHttpServer)
        {
            m_jsonrpcHttpServer->StopListening();
            delete m_jsonrpcHttpServer;
            m_jsonrpcHttpServer = nullptr;
            INITIALIZER_LOG(INFO) << "JsonrpcHttpServer deleted.";
        }
    }

    void initChannelRPCServer(boost::property_tree::ptree const& _pt);

    void initConfig(boost::property_tree::ptree const& _pt);
    void setP2PService(std::shared_ptr<p2p::P2PInterface> _p2pService)
    {
        m_p2pService = _p2pService;
    }
    void setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext)
    {
        m_sslContext = sslContext;
    }

    void setLedgerInitializer(LedgerInitializer::Ptr _ledgerInitializer)
    {
        m_ledgerInitializer = _ledgerInitializer;
        m_ledgerManager = m_ledgerInitializer->ledgerManager();
    }

    ChannelRPCServer::Ptr channelRPCServer() { return m_channelRPCServer; }
    std::shared_ptr<ledger::LedgerManager> getLedgerManager() { return m_ledgerManager; }

private:
    std::shared_ptr<bcos::stat::ChannelNetworkStatHandler> createNetWorkStatHandler(
        boost::property_tree::ptree const& _pt);

    std::shared_ptr<bcos::flowlimit::RPCQPSLimiter> createQPSLimiter(
        boost::property_tree::ptree const& _pt);

    bcos::flowlimit::RateLimiter::Ptr createNetworkBandwidthLimit(
        boost::property_tree::ptree const& _pt);

private:
    std::shared_ptr<p2p::P2PInterface> m_p2pService;
    std::shared_ptr<ledger::LedgerManager> m_ledgerManager;
    LedgerInitializer::Ptr m_ledgerInitializer;
    std::shared_ptr<boost::asio::ssl::context> m_sslContext;
    std::shared_ptr<bcos::rpc::Rpc> m_rpcForChannel;
    std::shared_ptr<bcos::SafeHttpServer> m_safeHttpServer;
    ChannelRPCServer::Ptr m_channelRPCServer;
    ModularServer<>* m_channelRPCHttpServer;
    ModularServer<>* m_jsonrpcHttpServer;
    bcos::stat::ChannelNetworkStatHandler::Ptr m_networkStatHandler;
};

}  // namespace initializer

}  // namespace bcos
