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
 * @file: ChannelConn.h
 * @author: tianheguoyun-alvin
 *
 * @date: 2017
 */

#pragma once

#include <arpa/inet.h>
#include <queue>
#include <string>
#include <thread>

#include "libchannelserver/ChannelException.h"
#include "libchannelserver/Message.h"
#include "libdevcore/ThreadPool.h"
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>
#include <libstat/ChannelNetworkStatHandler.h>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/stream.hpp>

namespace dev
{
namespace channel
{

bool getPublicKeyFromCert(std::shared_ptr<std::string> _nodeIDOut, X509* cert);

class ChannelConn : public std::enable_shared_from_this<ChannelConn>
{
public:
    ChannelConn(){ std::cout<<"ChannelConn()"<<std::endl; };
    virtual ~ChannelConn() ; //{ std::cout<<"~channelconn"<<std::endl; CHANNEL_LOG(TRACE) << "ChannelConn exit"; };

    typedef std::shared_ptr<ChannelConn> Ptr;
    typedef std::function<void(dev::channel::ChannelException, dev::channel::Message::Ptr)>
        CallbackType;

    const size_t bufferLength = 1024;

    Message::Ptr sendMessage(Message::Ptr request, size_t timeout = 0);
    void asyncSendMessage(Message::Ptr request,
        std::function<void(dev::channel::ChannelException, Message::Ptr)> callback,
        uint32_t timeout = 0);

   
    void  connect();

    bool actived() { return _actived; };

    void setMessageHandler(
        std::function<void(ChannelConn::Ptr, dev::channel::ChannelException, Message::Ptr)>
            handler)
    {
        _messageHandler = handler;
    };

	void setCheckCertIssuer(bool checkCertIssuer) { _checkCertIssuer = checkCertIssuer; }
	void setHostPort(std::string host ,int port){_host = host; _port = port;  };

    bool enableSSL() { return _enableSSL; }
    void setEnableSSL(bool ssl) { _enableSSL = ssl; }

    std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> sslSocket()
    {
        return _sslSocket;
    };
    void setSSLSocket(
        std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> socket);

	void setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext)
	{
		_sslContext = sslContext;
	};
		

    void setIOService(std::shared_ptr<boost::asio::io_context> IOService)
    {
        _ioService = IOService;
    };

    MessageFactory::Ptr messageFactory() { return _messageFactory; }

    void setMessageFactory(MessageFactory::Ptr messageFactory) { _messageFactory = messageFactory; }

    void setIdleTime(size_t idleTime) { _idleTime = idleTime; }

    void disconnectByQuit() { disconnect(ChannelException(-1, "quit")); }

    dev::ProtocolVersion protocolVersion() { return _channelProtocol; }
    dev::ProtocolVersion maximumProtocolVersion() { return _maximumProtocol; }
    dev::ProtocolVersion minimumProtocolVersion() { return _minimumProtocol; }
    dev::ProtocolVersion setProtocolVersion(dev::ProtocolVersion channelProtocol)
    {
        return _channelProtocol = channelProtocol;
    }
    std::string clientType() { return _clientType; }


    void setRemotePublicKey(dev::h512 const& remotePublicKey)
    {
        CHANNEL_SESSION_LOG(INFO) << LOG_DESC("setRemotePublicKey: set sdk public key")
                                  << LOG_KV("sdkPublicKey", remotePublicKey.abridged());
        _remotePublicKey = remotePublicKey;
    }

    dev::h512 const& remotePublicKey() { return _remotePublicKey; }
    std::function<bool(bool, boost::asio::ssl::verify_context&)> newVerifyCallback(std::shared_ptr<std::string> _sdkPublicKey);
    void stop();

private:

    void startRead();
    void onRead(const boost::system::error_code& error, size_t bytesTransferred);

    void handle_connect(const boost::system::error_code& error);
    void handle_handshake(const boost::system::error_code& error);

    void startWrite();
    void onWrite(const boost::system::error_code& error, std::shared_ptr<bytes> buffer,
        size_t bytesTransferred);
    void writeBuffer(std::shared_ptr<bytes> buffer);

    void onMessage(dev::channel::ChannelException e, Message::Ptr message);

    void onHandshake(const boost::system::error_code& error,
                      std::shared_ptr<std::string> _sdkPublicKey, ChannelConn::Ptr session);
    void onIdle(const boost::system::error_code& error);

    void disconnect(dev::channel::ChannelException e);
    void simpleclose();
    void updateIdleTimer();
    bool verify_certificate(bool preverified, boost::asio::ssl::verify_context& ctx);


private:


    MessageFactory::Ptr _messageFactory;
    std::function<void(ChannelConn::Ptr, dev::channel::ChannelException, Message::Ptr)>
        _messageHandler;

    bool _actived = false;
    bool _enableSSL = true;

    std::string _host;
    int _port = 0;

    byte _recvBuffer[1024];
    bytes _recvProtocolBuffer;

    std::queue<std::shared_ptr<bytes>> _sendBufferList;
    bool _writing = false;

    std::shared_ptr<boost::asio::deadline_timer> _idleTimer;
    std::recursive_mutex _mutex;

    //std::shared_ptr<boost::asio::io_service> _ioService;
    std::shared_ptr<boost::asio::io_context> _ioService;
    std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> _sslSocket;

	std::shared_ptr<boost::asio::ssl::context> _sslContext;
	bool _checkCertIssuer = true;
    // default _channelProtocol is minVersion
    dev::ProtocolVersion _channelProtocol = dev::ProtocolVersion::v1;
    dev::ProtocolVersion _minimumProtocol = dev::ProtocolVersion::minVersion;
    dev::ProtocolVersion _maximumProtocol = dev::ProtocolVersion::maxVersion;
    std::string _clientType;
    // set idle time interval to 60s
    size_t _idleTime = 60;
    std::shared_ptr<std::thread> _serverThread;

    std::function<void(dev::channel::ChannelException, ChannelConn::Ptr)> _connectionHandler;
    bool         _ready = false;	
    std::condition_variable  _handshakeCV;
    std::mutex               _handkshakeMutex;

    dev::h512   _remotePublicKey;
    std::string _certIssuerName; 

};

}  // namespace channel

}  // namespace dev
