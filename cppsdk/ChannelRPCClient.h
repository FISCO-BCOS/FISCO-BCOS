/*
 * @CopyRight:
 * FISCO-BCOS-CHAIN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS-CHAIN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS-CHAIN.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 tianhe-dev contributors.
 */
/**
 * @file: ChannelRPCClient.h
 * @author: tianheguoyun
 * @date: 2017
 *
 * @author xingqiangbai
 * @date 2018.11.5
 * @modify use libp2p to send message
 */

#pragma once

#include "libchannelserver/ChannelMessage.h"  // for TopicChannelM...
#include "ChannelConn.h"  // for ChannelSessio...
#include "libchannelserver/Message.h"         // for Message, Mess...
#include "libethcore/Common.h"
//#include "libp2p/P2PMessage.h"
#include <jsonrpccpp/client/iclientconnector.h>
#include <jsonrpccpp/common/exception.h>
//#include <libflowlimit/RPCQPSLimiter.h>
//#include <libnetwork/PeerWhitelist.h>
//#include <libstat/ChannelNetworkStatHandler.h>
#include <boost/asio/io_context.hpp>  // for io_service
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <atomic>                     // for atomic
#include <map>                        // for map
#include <mutex>                      // for mutex
#include <set>                        // for set
#include <string>                     // for string
#include <thread>
#include <utility>  // for swap, move
#include <vector>   // for vector
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

namespace Json
{
class Value;
}  // namespace Json

namespace dev
{
namespace channel
{
class ChannelException;
class ChannelServer;
}  // namespace channel
namespace network
{
class NetworkException;
}

class ChannelRPCClient :  public jsonrpc::IClientConnector , 
          public std::enable_shared_from_this<ChannelRPCClient>
{
public:
    enum ChannelERRORCODE
    {
        REMOTE_PEER_UNAVAILABLE = 100,
        REMOTE_CLIENT_PEER_UNAVAILABLE = 101,
        TIMEOUT = 102,
        REJECT_AMOP_REQ_FOR_OVER_BANDWIDTHLIMIT = 103
    };
    typedef std::shared_ptr<ChannelRPCClient> Ptr;

    ChannelRPCClient(std::string listenAddr = "", int listenPort = 0)
      : jsonrpc::IClientConnector(),
        _listenAddr(listenAddr),
        _listenPort(listenPort)
        {};

    virtual ~ChannelRPCClient();

	bool  startRequst();


    virtual void SendRPCMessage(const std::string& message, std::string& result) override ; // throw(jsonrpc::JsonRpcException)   override;


    void onClientRequest(dev::channel::ChannelConn::Ptr session, dev::channel::ChannelException e, dev::channel::Message::Ptr message);


    void setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext);

    void  initChannelRPCClient(boost::property_tree::ptree const& _pt);
    void  onClientRPCRes(dev::channel::ChannelConn::Ptr session, dev::channel::Message::Ptr message);

private:

    bool OnRpcRequest(
        dev::channel::ChannelConn::Ptr session, const std::string& request, void* addInfo);

    void onClientRPCRequest(
        dev::channel::ChannelConn::Ptr session, dev::channel::Message::Ptr message);


    void onClientChannelRequest(
        dev::channel::ChannelConn::Ptr session, dev::channel::Message::Ptr message);


    void onClientHandshake(
        dev::channel::ChannelConn::Ptr session, dev::channel::Message::Ptr message);

    void onClientHeartbeat(
        dev::channel::ChannelConn::Ptr session, dev::channel::Message::Ptr message);


    bool _running = false;

    std::string _listenAddr;
	
    int _listenPort;
    std::shared_ptr<boost::asio::io_context> _ioService;

    std::shared_ptr<boost::asio::ssl::context> _sslContext;

    std::shared_ptr<dev::channel::ChannelConn> _session;
    dev::channel::ChannelConn  * _session_ptr;

    std::mutex _sessionMutex;

    std::mutex _seqMutex;

    int _sessionCount = 1;

	std::string  _certIssuerName; 
    //std::vector<dev::eth::Handler<int64_t>> _handlers;
	bool _checkCertIssuer;

	std::condition_variable  _cliNetCV;
	std::mutex               _cliNetReadMutex;
	bool                     _cliready = false;
	std::string              _curResponse;


};

}  // namespace dev
