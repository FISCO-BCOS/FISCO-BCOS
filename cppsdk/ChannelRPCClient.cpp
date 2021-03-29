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
 * @file: ChannelRPCClient.cpp
 * @author: tianheguoyun
 * @date: 2017
 *
 * @author xingqiangbai
 * @date 2018.11.5
 * @modify use libp2p to send message
 */


#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
//#pragma GCC diagnostic ignored "-Werror=deprecated"

#include "CliSecureInitializer.h"
#include "ChannelRPCClient.h"
#include "libchannelserver/ChannelException.h"  // for CHANNEL_LOG
#include "libchannelserver/ChannelMessage.h"    // for TopicChannelM...
#include "ChannelConn.h"    // for ChannelSessio...
#include "libdevcore/Common.h"                  // for bytes, byte
#include "libethcore/Protocol.h"                // for AMOP, ProtocolID
#include "libnetwork/Common.h"                  // for NetworkException
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
//#include "libp2p/P2PInterface.h"                // for P2PInterface
//#include "libp2p/P2PMessageFactory.h"           // for P2PMessageFac...
//#include "libp2p/P2PSession.h"                  // for P2PSession
#include <json/json.h>
#include <libeventfilter/Common.h>
//#include <libp2p/P2PMessage.h>
//#include <libp2p/Service.h>
//#include <librpc/StatisticProtocolServer.h>
#include <unistd.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/random.hpp>
#include <boost/range/algorithm/remove_if.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <string>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::channel;
//using namespace dev::p2p;

static const int c_seqAbridgedLen = 8;

ChannelRPCClient::~ChannelRPCClient()
{
    if (_running)
    {
        _session->stop();
        {
           std::shared_ptr<ChannelConn> p1;
           //std::cout<<":~ChannelRPCClient   stop count "<<_session.use_count()<<std::endl;
           _session = p1;
        } 
        //delete _session_ptr;
    }

    _running = false;


}
// 初始化以后开始链接，再开始发数据
bool ChannelRPCClient::startRequst()
{
    if (!_running)
    {
        //CHANNEL_LOG(TRACE) << "Start ChannelRPCClient" << LOG_KV("Host", _listenAddr) << ":"<< _listenPort;

	   auto sslCtx = _sslContext->native_handle();
	   auto cert = SSL_CTX_get0_certificate(sslCtx);
	   /// get issuer name
	   const char* issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
	   std::string issuerName(issuer);
	   _certIssuerName = issuerName;
	   OPENSSL_free((void*)issuer);
	   
	   
	   try
       {
	
          //_session_ptr = new ChannelConn();
          //std::shared_ptr<ChannelConn> tempsession(_session_ptr);
           
	      _session = std::make_shared<ChannelConn>();
           
	      //_session = tempsession ;
	   
	      auto ioService = std::make_shared<boost::asio::io_context>();   
	      _session->setIOService(ioService);
	      _session->setEnableSSL(true);

	      _session->setSSLContext(_sslContext);
	      _session->setCheckCertIssuer(_checkCertIssuer);
	      _session->setSSLSocket(
		       std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket> >(
			   *ioService, *_sslContext));
          _session->setMessageFactory(std::make_shared<dev::channel::ChannelMessageFactory>());

		  std::function<void(dev::channel::ChannelConn::Ptr, dev::channel::ChannelException,
			  dev::channel::Message::Ptr)>
			  fp = std::bind(&dev::ChannelRPCClient::onClientRequest, this, std::placeholders::_1,
				  std::placeholders::_2, std::placeholders::_3);
		  _session->setMessageHandler(fp);
		  _session->setHostPort(_listenAddr ,_listenPort);
	      _session->connect();   
	      _running = true;
		  	   
       }
       catch (std::exception& e)
       {
	      CHANNEL_LOG(ERROR) << LOG_DESC("IO thread error")  << LOG_KV("what", boost::diagnostic_information(e));
       }	
		
    }

    return true;
}

// 发送消息
// session->asyncSendMessage
void ChannelRPCClient::SendRPCMessage(const std::string& message_str, std::string& result)// throw(jsonrpc::JsonRpcException) 
{
	
	auto factory = _session->messageFactory();
	auto message = factory->buildMessage();
    // message->setGroupID(1); 
	message->setSeq(dev::newSeq()); //random 
	message->setResult(0);
	message->setType(CHANNEL_RPC_REQUEST);
	message->setData((const byte*)message_str.c_str(), message_str.length());

	_cliready = false; 
	_session->asyncSendMessage(message, dev::channel::ChannelConn::CallbackType(), 0);
    
	
    std::unique_lock<std::mutex> netreadlock( _cliNetReadMutex);
    //auto this_ptr = shared_from_this();
    //_cliNetCV.wait(netreadlock, [this]{return this->_cliready;});
    while(!_cliready) { _cliNetCV.wait(netreadlock); } 

    //std::cout<<"ChannelRPCClient::SendRPCMessage  receive netreadlock   "<< _curResponse << std::endl;
	result = _curResponse;	

}


void dev::ChannelRPCClient::onClientRequest(dev::channel::ChannelConn::Ptr session,
    dev::channel::ChannelException e, dev::channel::Message::Ptr message)
{
    if (e.errorCode() == 0)
    {
        //CHANNEL_LOG(TRACE) << "receive sdk message" << LOG_KV("length", message->length())
        //                   << LOG_KV("type", message->type())
        //                   << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen));

        switch (message->type())
        {
        case CHANNEL_RPC_REQUEST:
            onClientRPCRequest(session, message);
            break;
        case CLIENT_HEARTBEAT:
            onClientHeartbeat(session, message);
            break;
        case CLIENT_HANDSHAKE:
            onClientHandshake(session, message);
            break;
		case TRANSACTION_NOTIFY:  // guess  this is return. 
			onClientRPCRes(session, message);
            break;
        case CLIENT_REGISTER_EVENT_LOG:
        case CLIENT_UNREGISTER_EVENT_LOG:
        case AMOP_CLIENT_SUBSCRIBE_TOPICS:
        case UPDATE_TOPIICSTATUS:
        case AMOP_REQUEST:
        case AMOP_RESPONSE:
        case AMOP_MULBROADCAST:		
        default:
            CHANNEL_LOG(ERROR) << "unknown client message" << LOG_KV("type", message->type());
            break;
        }
    }
    else
    {
        CHANNEL_LOG(WARNING) << "onClientRequest" << LOG_KV("errorCode", e.errorCode())
                             << LOG_KV("what", e.what());
     
        _curResponse = std::string("exception 100");
        if(!_cliready)
        {
           _cliready = true;
           _cliNetCV.notify_all(); 
        } 
        //onDisconnect(dev::channel::ChannelException(), session);
    }
}


void dev::ChannelRPCClient::onClientRPCRes(dev::channel::ChannelConn::Ptr session, dev::channel::Message::Ptr message)
{

    GROUP_ID  _groupId;	
    std::string  seq;
    std::string receiptContext;
	auto channelMessage = _session->messageFactory()->buildMessage();
	channelMessage->setType(TRANSACTION_NOTIFY);
	channelMessage->setGroupID(  _groupId);
	channelMessage->setSeq(seq);
	channelMessage->setResult(0);
	channelMessage->setData(
		(const byte*)receiptContext.c_str(), receiptContext.size());

	//CHANNEL_LOG(TRACE) << "Push transaction notify: " << seq;
	
	std::string body(message->data(), message->data() + message->dataSize());

	
    std::unique_lock<std::mutex> netreadlock( _cliNetReadMutex);	
	
    _curResponse = body;
    	
	//;;_cliNetCV.notify_all(netreadlock);	
    _cliready = true;
	_cliNetCV.notify_all();	

}



void dev::ChannelRPCClient::onClientRPCRequest(
    dev::channel::ChannelConn::Ptr session, dev::channel::Message::Ptr message)
{
    //CHANNEL_LOG(DEBUG) << "receive client request return response";

    std::string body(message->data(), message->data() + message->dataSize());

    try
    {
       std::string body(message->data(), message->data() + message->dataSize());
       std::unique_lock<std::mutex> netreadlock( _cliNetReadMutex);
       //std::cout<< " read return  "<<body<<std::endl;
       _curResponse = body;
       //;;_cliNetCV.notify_all(netreadlock);  
       _cliready = true;
       _cliNetCV.notify_all();

    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "Error while OnRpcRequest rpc: " << boost::diagnostic_information(e);
    }

}


void dev::ChannelRPCClient::onClientHandshake(
    dev::channel::ChannelConn::Ptr session, dev::channel::Message::Ptr message)
{
    try
    {
        CHANNEL_LOG(INFO) << LOG_DESC("on client handshake") << LOG_KV("seq", message->seq());
        std::string data(message->data(), message->data() + message->dataSize());
        Json::Value value;
        Json::Reader jsonReader;
        if (!jsonReader.parse(data, value))
        {
            CHANNEL_LOG(ERROR) << "parse to json failed" << LOG_KV(" data ", data);
            return;
        }

        auto minimumProtocol = static_cast<ProtocolVersion>(value.get("minimumSupport", 1).asInt());
        auto maximumProtocol = static_cast<ProtocolVersion>(value.get("maximumSupport", 1).asInt());
        auto clientType = value.get("clientType", "Unknow Client Type").asString();
        if (session->maximumProtocolVersion() < minimumProtocol ||
            session->minimumProtocolVersion() > maximumProtocol)
        {  // If the scope does not intersect, disconnect
            CHANNEL_LOG(WARNING) << "onClientHandshake failed, unsupported protocol"
                                 << LOG_KV("clientType", clientType)
                                 << LOG_KV("SDKMinSupport", value.get("minimumSupport", 1).asInt())
                                 << LOG_KV("SDKMaxSupport", value.get("maximumSupport", 1).asInt());
            //session->disconnectByQuit();
            //onDisconnect(dev::channel::ChannelException(-1, "Unsupport protocol"), session);
            return;
        }
        // Choose the next largest version number
        session->setProtocolVersion(session->maximumProtocolVersion() > maximumProtocol ?
                                        maximumProtocol :
                                        session->maximumProtocolVersion());

        Json::Value response;
        response["protocol"] = static_cast<int>(session->protocolVersion());
        response["nodeVersion"] = g_BCOSConfig.supportedVersion();
        Json::FastWriter writer;
        auto resp = writer.write(response);
        message->setData((const byte*)resp.data(), resp.size());
        session->asyncSendMessage(message, dev::channel::ChannelConn::CallbackType(), 0);
        CHANNEL_LOG(INFO) << "onClientHandshake" << LOG_KV("ProtocolVersion", response["protocol"])
                          << LOG_KV("clientType", clientType) ;
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "onClientHandshake" << boost::diagnostic_information(e);
    }
}

void dev::ChannelRPCClient::onClientHeartbeat(
    dev::channel::ChannelConn::Ptr session, dev::channel::Message::Ptr message)
{
    std::string data((char*)message->data(), message->dataSize());
    if (session->protocolVersion() == ProtocolVersion::v1)
    {
        // default is ProtocolVersion::v1
        if (data == "0")
        {
            data = "1";
            message->setData((const byte*)data.data(), data.size());
            session->asyncSendMessage(message, dev::channel::ChannelConn::CallbackType(), 0);
        }
    }
    // v2 and v3 for now
    else
    {
        Json::Value response;
        response["heartBeat"] = 1;
        Json::FastWriter writer;
        auto resp = writer.write(response);
        message->setData((const byte*)resp.data(), resp.size());
        session->asyncSendMessage(message, dev::channel::ChannelConn::CallbackType(), 0);
    }
}


void  ChannelRPCClient::initChannelRPCClient(boost::property_tree::ptree const& _pt)
{

	// listen ip for channel service, load from rpc.listen_ip if rpc.channel_listen_ip is not setted
	_listenAddr = _pt.get<std::string>("rpc.channel_listen_ip", "127.0.0.1");

	_listenPort = _pt.get<int>("rpc.channel_listen_port", 20200);

	_checkCertIssuer = _pt.get<bool>("network_security.check_cert_issuer", true);
	//CHANNEL_LOG(INFO) << LOG_BADGE("ChannelRPCClient") << LOG_KV("network_security.check_cert_issuer", _checkCertIssuer);

	std::shared_ptr<CliSecureInitializer> secureInitializer;
	secureInitializer = std::make_shared<CliSecureInitializer>();
	secureInitializer->initConfig(_pt);
	
	setSSLContext(secureInitializer->SSLContext(CliSecureInitializer::Usage::ForRPC));
	startRequst();
	
}


void ChannelRPCClient::setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext)
{
    _sslContext = sslContext;
}





