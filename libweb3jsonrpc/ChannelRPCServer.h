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
#include <libdevcore/FixedHash.h>
#include <libchannelserver/ChannelSession.h>
#include <libchannelserver/ChannelServer.h>
#include <libethereum/Client.h>
#include <libethereum/Web3Observer.h>
#include "IpcServerBase.h"

namespace dev
{

class ChannelRPCServer: public jsonrpc::AbstractServerConnector, public std::enable_shared_from_this<ChannelRPCServer> {
public:
	enum ChannelERRORCODE {
		REMOTE_PEER_UNAVAILIBLE = 100,
		REMOTE_CLIENT_PEER_UNAVAILBLE = 101,
		TIMEOUT = 102
	};

	struct ChannelMessageSession {
		//节点主动发起链上链下消息时使用
		dev::channel::ChannelSession::Ptr fromSession;
		h512 toNodeID;
		std::set<h512> failedNodeIDs;

		//收到其它节点的链上链下消息时使用
		h512 fromNodeID;
		dev::channel::ChannelSession::Ptr toSession;
		std::set<dev::channel::ChannelSession::Ptr> failedSessions;

		//消息
		dev::channel::Message::Ptr message;


	};

	typedef std::shared_ptr<ChannelRPCServer> Ptr;

	ChannelRPCServer(std::string listenAddr = "", int listenPort = 0): jsonrpc::AbstractServerConnector(), _listenAddr(listenAddr), _listenPort(listenPort) {};
	virtual ~ChannelRPCServer();
	virtual bool StartListening() override;
	virtual bool StopListening() override;
	virtual bool SendResponse(std::string const& _response, void* _addInfo = nullptr) override;

	//收到连接
	void onConnect(dev::channel::ChannelException e, dev::channel::ChannelSession::Ptr session);

	//断开连接
	void onDisconnect(dev::channel::ChannelException e, dev::channel::ChannelSession::Ptr session);

	//收到请求
	void onClientRequest(dev::channel::ChannelSession::Ptr session, dev::channel::ChannelException e, dev::channel::Message::Ptr message);

	//收到来自前置的链上链下请求
	void onClientMessage(dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message);

	//收到来自前置的区块链请求
	void onClientEthereumRequest(dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message);

	//来自前置的topic请求
	void onClientTopicRequest(dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message);

	//来自前置的链上链下二期请求
	void onClientChannelRequest(dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message);

	//收到来自其他节点的请求
	void onNodeRequest(dev::h512 nodeID, std::shared_ptr<dev::bytes> message);

	//收到来自其他节点的链上链下请求
	void onNodeMessage(h512 nodeID, dev::channel::Message::Ptr message);

	//收到来自其它节点的链上链下二期请求
	void onNodeChannelRequest(h512 nodeID, dev::channel::Message::Ptr message);

	void setListenAddr(const std::string &listenAddr);

	void setListenPort(int listenPort);

	void removeSession(int sessionID);

	void CloseConnection(int _socket);

	Web3Observer::Ptr buildObserver();

	void setHost(std::weak_ptr<EthereumHost> host);

private:
	h512 sendChannelMessageToNode(std::string topic, dev::channel::Message::Ptr message, const std::set<h512> &exclude);

	dev::channel::ChannelSession::Ptr sendChannelMessageToSession(std::string topic, dev::channel::Message::Ptr message, const std::set<dev::channel::ChannelSession::Ptr> &exclude);

	void updateHostTopics();

	std::vector<dev::channel::ChannelSession::Ptr> getSessionByTopic(const std::string &topic);

	std::string topicStrip(std::string topic);

	bool _running = false;

	std::string _listenAddr;
	int _listenPort;
	std::shared_ptr<boost::asio::io_service> _ioService;

	std::shared_ptr<dev::channel::ChannelServer> _server;
	std::shared_ptr<std::thread> _topicThread;

	std::map<int, dev::channel::ChannelSession::Ptr> _sessions; //所有当前session，用于下发消息

	std::map<std::string, dev::channel::ChannelSession::Ptr> _seq2session; //用于查找seq对应的回包
	std::mutex _seqMutex;

	std::map<std::string, ChannelMessageSession> _seq2MessageSession; //用于查找链上链下消息2期的session
	std::mutex _seqMessageMutex;

	//std::map<std::string, h512> _seq2NodeID; //用于查找seq对应的nodeID

	int _sessionCount = 1;

	std::weak_ptr<EthereumHost> _host;
};

}
