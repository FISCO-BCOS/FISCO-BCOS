/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: ChannelSession.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include <string>
#include <thread>
#include <queue>
#include <arpa/inet.h>
#include <libdevcore/easylog.h>
#include <libdevcore/Guards.h>

#include <libdevcore/FixedHash.h>
#include <boost/asio.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl.hpp>
#include "ChannelException.h"
#include "ChannelMessage.h"
#include "ThreadPool.h"

namespace dev
{

namespace channel {

class ChannelSession: public std::enable_shared_from_this<ChannelSession> {
public:
	ChannelSession();
	virtual ~ChannelSession() {
		LOG(DEBUG) << "session exit";
	};

	typedef std::shared_ptr<ChannelSession> Ptr;
	typedef std::function<void(dev::channel::ChannelException, dev::channel::Message::Ptr)> CallbackType;

	const size_t bufferLength = 1024;

	virtual Message::Ptr sendMessage(Message::Ptr request, size_t timeout = 0) throw(ChannelException);
	virtual void asyncSendMessage(Message::Ptr request, std::function<void(dev::channel::ChannelException, Message::Ptr)> callback, uint32_t timeout = 0);

	virtual void run();

	virtual bool actived() { return _actived; };

	virtual void setMessageHandler(std::function<void(ChannelSession::Ptr, dev::channel::ChannelException, Message::Ptr)> handler) { _messageHandler = handler; };

	virtual std::string host() { return _host; };
	virtual int port() { return _port; };

	virtual void setHost(std::string host) { _host = host; };
	virtual void setPort(int port) { _port = port; };

	virtual std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket> > sslSocket() { return _sslSocket;};
	virtual void setSSLSocket(std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket> > socket);

	virtual std::shared_ptr<boost::asio::ip::tcp::socket> socket() { return _socket; };
	virtual void setSocket(std::shared_ptr<boost::asio::ip::tcp::socket> socket) { _socket = socket; };

	virtual void setIOService(std::shared_ptr<boost::asio::io_service> IOService) { _ioService = IOService; };

	std::set<std::string> topics() 
	{
		dev::ReadGuard l(x_topics); 
		return *_topics; 
	};
	void setTopics(std::shared_ptr<std::set<std::string> > topics) 
	{
		dev::WriteGuard l(x_topics); 
		*_topics = *topics; 
	};
	void setThreadPool(ThreadPool::Ptr threadPool) { _threadPool = threadPool; }

private:
	void onHandshake(const boost::system::error_code& error);

	void startRead();
	void onRead(const boost::system::error_code& error, size_t bytesTransferred);

	void startWrite();
	void onWrite(const boost::system::error_code& error, std::shared_ptr<bytes> buffer, size_t bytesTransferred);
	void writeBuffer(std::shared_ptr<bytes> buffer);

	void onMessage(dev::channel::ChannelException e, Message::Ptr message);
	void onTimeout(const boost::system::error_code& error, std::string seq);

	void onIdle(const boost::system::error_code& error);

	void disconnect(dev::channel::ChannelException e);

	void updateIdleTimer();

	std::function<void(ChannelSession::Ptr, dev::channel::ChannelException, Message::Ptr)> _messageHandler;

	bool _actived = false;

	std::string _host;
	int _port = 0;

	byte _recvBuffer[1024];
	bytes _recvProtocolBuffer;

	std::queue<std::shared_ptr<bytes> > _sendBufferList;
	std::queue<uint64_t > _sendTimeList;
	bool _writing = false;

	std::shared_ptr<boost::asio::deadline_timer> _idleTimer;
	std::recursive_mutex _mutex;

	std::shared_ptr<boost::asio::io_service> _ioService;
	std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket> > _sslSocket;
	std::shared_ptr<boost::asio::ip::tcp::socket> _socket;

	struct ResponseCallback: public std::enable_shared_from_this<ResponseCallback> {
		typedef std::shared_ptr<ResponseCallback> Ptr;

		std::string seq = "";
		std::function<void(ChannelException, Message::Ptr)> callback;
		std::shared_ptr<boost::asio::deadline_timer> timeoutHandler;
	};

	std::map<std::string, ResponseCallback::Ptr> _responseCallbacks;

	mutable dev::SharedMutex x_topics;
	std::shared_ptr<std::set<std::string> > _topics; //该session关注的topic
	ThreadPool::Ptr _threadPool;
};

}

}
