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
 * @file: ChannelRPCServer.h
 * @author: fisco-dev
 * 
 * @date: 2017

 */

#pragma once

#include <string>
#include <thread>
#include <queue>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <boost/asio.hpp>
#include <libdevcore/FixedHash.h>
#include <libchannelserver/ChannelException.h>
#include <libchannelserver/ChannelSession.h>
#include <libchannelserver/ChannelServer.h>
#include <libethereum/Web3Observer.h>
#include <libchannelserver/ThreadPool.h>
#include "IpcServerBase.h"

namespace dev
{
namespace eth {
class EthereumHost;
}

class ChannelRPCServer: public jsonrpc::AbstractServerConnector, public std::enable_shared_from_this<ChannelRPCServer> {
public:
	enum ChannelERRORCODE {
		REMOTE_PEER_UNAVAILIBLE = 100,
		REMOTE_CLIENT_PEER_UNAVAILBLE = 101,
		TIMEOUT = 102
	};

	struct ChannelMessageSession {
		dev::channel::ChannelSession::Ptr fromSession;
		h512 toNodeID;
		std::set<h512> failedNodeIDs;

		h512 fromNodeID;
		dev::channel::ChannelSession::Ptr toSession;
		std::set<dev::channel::ChannelSession::Ptr> failedSessions;

		dev::channel::Message::Ptr message;
	};

	typedef std::shared_ptr<ChannelRPCServer> Ptr;

	ChannelRPCServer(std::string listenAddr = "", int listenPort = 0): jsonrpc::AbstractServerConnector(), _listenAddr(listenAddr), _listenPort(listenPort) {
		_sslContext = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);
	};
	virtual ~ChannelRPCServer();
	virtual bool StartListening() override;
	virtual bool StopListening() override;
	virtual bool SendResponse(std::string const& _response, void* _addInfo = nullptr) override;


	virtual void onConnect(dev::channel::ChannelException e, dev::channel::ChannelSession::Ptr session);


	virtual void onDisconnect(dev::channel::ChannelException e, dev::channel::ChannelSession::Ptr session);


	virtual void onClientRequest(dev::channel::ChannelSession::Ptr session, dev::channel::ChannelException e, dev::channel::Message::Ptr message);


	virtual void onClientMessage(dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message);


	virtual void onClientEthereumRequest(dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message);

	virtual void onClientTopicRequest(dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message);

	virtual void onClientChannelRequest(dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message);

	virtual void onNodeRequest(dev::h512 nodeID, std::shared_ptr<dev::bytes> message);

	virtual void onNodeMessage(h512 nodeID, dev::channel::Message::Ptr message);

	virtual void onNodeChannelRequest(h512 nodeID, dev::channel::Message::Ptr message);

	void setListenAddr(const std::string &listenAddr);

	void setListenPort(int listenPort);

	void removeSession(int sessionID);

	void CloseConnection(int _socket);

	dev::eth::Web3Observer::Ptr buildObserver();

	void setHost(std::weak_ptr<dev::eth::EthereumHost> host);

	void setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext);

	void asyncPushChannelMessage(std::string topic, dev::channel::Message::Ptr message,	std::function<void(dev::channel::ChannelException, dev::channel::Message::Ptr)> callback);

	virtual dev::channel::TopicMessage::Ptr pushChannelMessage(dev::channel::TopicMessage::Ptr message);

	virtual std::string newSeq();

private:
	void initContext();
	void initSSLContext();
	
	h512 sendChannelMessageToNode(std::string topic, dev::channel::Message::Ptr message, const std::set<h512> &exclude);

	dev::channel::ChannelSession::Ptr sendChannelMessageToSession(std::string topic, dev::channel::Message::Ptr message, const std::set<dev::channel::ChannelSession::Ptr> &exclude);

	void updateHostTopics();

	std::vector<dev::channel::ChannelSession::Ptr> getSessionByTopic(const std::string &topic);

	bool _running = false;

	std::string _listenAddr;
	int _listenPort;
	std::shared_ptr<boost::asio::io_service> _ioService;

	std::shared_ptr<boost::asio::ssl::context> _sslContext;
	std::shared_ptr<dev::channel::ChannelServer> _server;
	std::shared_ptr<std::thread> _topicThread;

	std::map<int, dev::channel::ChannelSession::Ptr> _sessions;
	std::mutex _sessionMutex;

	std::map<std::string, dev::channel::ChannelSession::Ptr> _seq2session;
	std::mutex _seqMutex;

	std::map<std::string, ChannelMessageSession> _seq2MessageSession;
	std::mutex _seqMessageMutex;

	int _sessionCount = 1;

	std::weak_ptr<dev::eth::EthereumHost> _host;
};

}
