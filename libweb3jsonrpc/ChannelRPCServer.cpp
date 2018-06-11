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
 * @file: ChannelRPCServer.cpp
 * @author: fisco-dev
 * @date: 2017
 * @author: toxotguo
 * @date: 2018
 */

#include "ChannelRPCServer.h"

#include <cstdlib>
#include <sys/socket.h>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <boost/random.hpp>
#include <libethereum/EthereumHost.h>
#include <libdevcore/easylog.h>
#include "JsonHelper.h"
#include <libweb3jsonrpc/RPCallback.h>

using namespace dev;
using namespace dev::eth;

ChannelRPCServer::~ChannelRPCServer() {
	StopListening();
}

bool ChannelRPCServer::StartListening() {
	if (!_running)
	{
		LOG(INFO) << "start listen: " << _listenAddr << ":" << _listenPort;

		_ioService = std::make_shared<boost::asio::io_service>(1);
		
		if (dev::getSSL() == SSL_SOCKET_V2)
		{
#if ETH_ENCRYPTTYPE
			initContext();
#else
			initSSLContext();
#endif
		}
		else{
			initContext();
		}

		_server->run();

		LOG(INFO) << "start heartbeat thread";
		_topicThread = std::make_shared<std::thread>([ = ]() {
			while (true) {
				sleep(1);
				try {
					_host.lock()->sendTopicsMessage(p2p::NodeID(), 0, _host.lock()->getTopicsSeq(), std::make_shared<std::set<std::string> >());
				}
				catch (std::exception &e) {
					LOG(ERROR) << "send topics error:" << e.what();
				}
			}
		});

		_topicThread->detach();

		LOG(INFO) << "listen started";

		_running = true;

	}

	return true;
}

void ChannelRPCServer::initContext()
{
	string certData = asString( contents( getDataDir() + "/ca.crt") );
	if (certData == "")
	{
		LOG(ERROR)<<"Get ca.crt File Err......................";
		exit(-1);
	}
	certData = asString( contents( getDataDir() + "/server.crt") );
	if (certData == "")
	{
		LOG(ERROR)<<"Get server.crt File Err......................";
		exit(-1);
	}
	certData = asString( contents( getDataDir() + "/server.key") );
	if (certData == "")
	{
		LOG(ERROR)<<"Get server.key File Err......................";
		exit(-1);
	}
	m_sslcontext->load_verify_file(getDataDir() + "ca.crt");
	m_sslcontext->use_certificate_chain_file(getDataDir() + "server.crt");
	m_sslcontext->use_private_key_file(getDataDir() + "server.key", boost::asio::ssl::context_base::pem);

	_server = make_shared<dev::channel::ChannelServer>();
	_server->setIOService(_ioService);
	_server->setSSLContext(m_sslcontext);

	_server->setEnableSSL(true);
	_server->setBind(_listenAddr, _listenPort);

	std::function<void(dev::channel::ChannelException, dev::channel::ChannelSession::Ptr)> fp = std::bind(&ChannelRPCServer::onConnect, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
	_server->setConnectionHandler(fp);
}
void ChannelRPCServer::initSSLContext()
{
	vector< pair<string,Public> >  certificates;
	string nodepri;
	CertificateServer::GetInstance().getCertificateList(certificates,nodepri);

	EC_KEY * ecdh=EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
	SSL_CTX_set_tmp_ecdh(m_sslcontext->native_handle(),ecdh);
	EC_KEY_free(ecdh);

	m_sslcontext->set_verify_depth(3);
	m_sslcontext->add_certificate_authority(boost::asio::const_buffer(certificates[0].first.c_str(), certificates[0].first.size()));
	
	string chain=certificates[0].first+certificates[1].first;
	m_sslcontext->use_certificate_chain(boost::asio::const_buffer(chain.c_str(), chain.size()));
	m_sslcontext->use_certificate(boost::asio::const_buffer(certificates[2].first.c_str(), certificates[2].first.size()),ba::ssl::context::file_format::pem);
	
	m_sslcontext->use_private_key(boost::asio::const_buffer(nodepri.c_str(), nodepri.size()),ba::ssl::context_base::pem);
	
	_server = make_shared<dev::channel::ChannelServer>();
	_server->setIOService(_ioService);
	_server->setSSLContext(m_sslcontext);

	_server->setEnableSSL(true);
	_server->setBind(_listenAddr, _listenPort);

	std::function<void(dev::channel::ChannelException, dev::channel::ChannelSession::Ptr)> fp = std::bind(&ChannelRPCServer::onConnect, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
	_server->setConnectionHandler(fp);
}

bool ChannelRPCServer::StopListening()
{
	if (_running) {
		_ioService->stop();
	}

	_running = false;

	return true;
}

bool ChannelRPCServer::SendResponse(const std::string& _response, void* _addInfo) {
	std::string addInfo = *((std::string*)_addInfo);

	std::lock_guard<std::mutex> lock(_seqMutex);
	auto it = _seq2session.find(addInfo);

	delete (std::string*)_addInfo;

	if (it != _seq2session.end()) {
		LOG(DEBUG) << "send ethereum response seq：" << it->first << " response:" << _response;

		std::shared_ptr<bytes> resp(new bytes());

		dev::channel::Message::Ptr message = make_shared<dev::channel::Message>();
		message->seq = it->first;
		message->result = 0;
		message->type = 0x12;

		message->data->assign(_response.begin(), _response.end());

		it->second->asyncSendMessage(message, dev::channel::ChannelSession::CallbackType(), 0);

		_seq2session.erase(it);
	}
	else {
		LOG(ERROR) << "source seq not found，probably timeout:" << addInfo;
	}

	return false;
}

void dev::ChannelRPCServer::removeSession(int sessionID) {
	auto it = _sessions.find(sessionID);

	if (it != _sessions.end()) {
		_sessions.erase(it);
	}
}

void ChannelRPCServer::onConnect(dev::channel::ChannelException e, dev::channel::ChannelSession::Ptr session) {
	if (e.errorCode() == 0) {
		LOG(INFO) << "channel reveive new connection";

		auto sessionID = ++_sessionCount;
		_sessions.insert(std::make_pair(sessionID, session));

		std::function<void(dev::channel::ChannelException, dev::channel::Message::Ptr)> fp =
		    std::bind(&dev::ChannelRPCServer::onClientRequest,
		              shared_from_this(), session, std::placeholders::_1,
		              std::placeholders::_2);
		session->setMessageHandler(fp);

		session->run();
		LOG(INFO) << "start reveive data";
	}
	else {
		LOG(ERROR) << "connection error: " << e.errorCode() << ", " << e.what();
	}
}

void ChannelRPCServer::onDisconnect(dev::channel::ChannelException e, dev::channel::ChannelSession::Ptr session) {
	LOG(ERROR) << "remove session: " << session->host() << ":" << session->port() << " successed";

	for (auto it : _sessions) {
		if (it.second == session) {
			_sessions.erase(it.first);
			break;
		}
	}

	updateHostTopics();
}

void dev::ChannelRPCServer::onClientRequest(dev::channel::ChannelSession::Ptr session, dev::channel::ChannelException e, dev::channel::Message::Ptr message) {
	if (e.errorCode() == 0) {
		LOG(DEBUG) << "receive message from sdk length:" << message->length << " type:" << message->type << " sessionID:" << message->seq;

		switch (message->type) {
		case 0x20:
		case 0x21:
			onClientMessage(session, message);
			break;
		case 0x12:
			onClientEthereumRequest(session, message);
			break;
		case 0x13:
		{
			std::string data((char*)message->data->data(), message->data->size());

			if (data == "0") {
				data = "1";
				message->data->clear();
				message->data->assign(data.begin(), data.end());
				session->asyncSendMessage(message, dev::channel::ChannelSession::CallbackType(), 0);
			}
			else if (data == "1") {
			}
			break;
		}
		case 0x30:
		case 0x31:
			onClientChannelRequest(session, message);
			break;
		case 0x32:
			onClientTopicRequest(session, message);
			break;
		default:
			LOG(ERROR) << "unknown client type: " << message->type;
			break;
		}
	}
	else {
		LOG(ERROR) << "error: " << e.errorCode() << ", " << e.what();

		onDisconnect(dev::channel::ChannelException(), session);
	}
}

void dev::ChannelRPCServer::onClientMessage(dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message) {
	LOG(DEBUG) << "receive sdk channel message";

	if (message->data->size() < 128) {
		LOG(ERROR) << "invaild channel message，too short:" << message->data->size();
		return;
	}

	LOG(DEBUG) << "target node:" << std::string((char*)message->data->data(), 128);

	h512 nodeID(std::string((char*)message->data->data(), 128), dev::h512::FromHex);

	int result = 0;
	try {
		std::lock_guard<std::mutex> lock(_seqMutex);
		auto it = _seq2session.insert(std::make_pair(message->seq, session));
		if (it.second == false) {
			LOG(DEBUG) << "seq exists";
			it.first->second = session;
		}

		auto buffer = std::make_shared<bytes>();
		message->encode(*buffer);

		LOG(DEBUG) << "encode message to endpoint:" << buffer->size();

		_host.lock()->sendCustomMessage(nodeID, buffer);
	}
	catch (exception &e) {
		LOG(ERROR) << "send channel message error，code 100:" << e.what();
		result = REMOTE_PEER_UNAVAILIBLE;

		message->type = 0x21;
		message->result = result;
		message->data->clear();

		session->asyncSendMessage(message, dev::channel::ChannelSession::CallbackType(), 0);
	}

	LOG(DEBUG) << "send result:" << result;
}

void dev::ChannelRPCServer::onClientEthereumRequest(dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message) {
	LOG(DEBUG) << "receive ethereum request from sdk";

	std::string body(message->data->data(), message->data->data() + message->data->size());

	LOG(DEBUG) << "seq:" << message->seq << "  ethereum request:" << std::string((char*)message->data->data(), message->data->size());

	{
		std::lock_guard<std::mutex> lock(_seqMutex);
		_seq2session.insert(std::make_pair(message->seq, session));
	}

	std::string *addInfo = new std::string(message->seq);

	OnRequest(body, addInfo);
	RPCallback::getInstance().parseAndSaveSession(body, message->seq, session);
}

void dev::ChannelRPCServer::onClientTopicRequest(dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message) {
	LOG(DEBUG) << "receive SDK topic request";

	std::string body(message->data->data(), message->data->data() + message->data->size());

	LOG(DEBUG) << "seq:" << message->seq << "  topic request:" << body;

	try {
		std::stringstream ss;
		ss << body;

		Json::Value root;
		ss >> root;

		std::shared_ptr<std::set<std::string> > topics = std::make_shared<std::set<std::string> >();
		Json::Value topicsValue = root;
		if (!topicsValue.empty()) {
			for (size_t i = 0; i < topicsValue.size(); ++i) {
				std::string topic = topicsValue[(int)i].asString();

				LOG(DEBUG) << "topic:" << topic;

				topics->insert(topic);
			}
		}

		session->setTopics(topics);

		updateHostTopics();
	}
	catch (exception &e) {
		LOG(ERROR) << "parse request error:" << e.what();
	}
}

void dev::ChannelRPCServer::onClientChannelRequest(
    dev::channel::ChannelSession::Ptr session,
    dev::channel::Message::Ptr message) {
	LOG(DEBUG) << "receive SDK channel2 message";

	if (message->data->size() < 1) {
		LOG(ERROR) << "invalid channel2 message，too short:" << message->data->size();
		return;
	}

	uint8_t topicLen = *((uint8_t*)message->data->data());
	std::string topic((char*)message->data->data() + 1, topicLen - 1);

	LOG(DEBUG) << "target topic:" << topic;

	std::lock_guard<std::mutex> lock(_seqMessageMutex);
	auto it = _seq2MessageSession.find(message->seq);

	if (message->type == 0x30) {
		try {
			LOG(DEBUG) << "channel2 request:" << message->seq;

			ChannelMessageSession messageSession;
			messageSession.fromSession = session;
			messageSession.message = message;

			auto newIt = _seq2MessageSession.insert(std::make_pair(message->seq, messageSession));
			if (newIt.second == false) {
				LOG(WARNING) << "seq:" << message->seq << " session repeat，override";

				newIt.first->second.fromSession = session;
			}
			it = newIt.first;

			LOG(DEBUG) << "send message to endpoint:" << it->second.message->seq;
			h512 nodeID = sendChannelMessageToNode(topic, it->second.message, it->second.failedNodeIDs);

			it->second.toNodeID = nodeID;
		}
		catch (exception &e) {
			LOG(ERROR) << "send message error:" << e.what();

			message->type = 0x31;
			message->result = REMOTE_PEER_UNAVAILIBLE;
			message->data->clear();

			it->second.fromSession->asyncSendMessage(message, dev::channel::ChannelSession::CallbackType(), 0);
		}
	}
	else if (message->type == 0x31) {
		try {
			if (it == _seq2MessageSession.end()) {
				LOG(WARNING) << "not found response's seq，timeout？";

				return;
			}

			if (message->result != 0) {
				try {
					LOG(DEBUG) << "message" << message->seq << "push to " << it->second.toSession->host() << ":" << it->second.toSession->port() << " failed:" << message->result;
					it->second.failedSessions.insert(it->second.toSession);

					auto session = sendChannelMessageToSession(topic, message, it->second.failedSessions);

					LOG(DEBUG) << "try push to" << session->host() << ":" << session->port() << " failed:" << message->result;
					it->second.toSession = session;
				}
				catch (exception &e) {
					LOG(ERROR) << "message push totally failed:" << e.what();

					message->result = REMOTE_CLIENT_PEER_UNAVAILBLE;
					message->type = 0x31;

					auto buffer = make_shared<bytes>();
					message->encode(*buffer);

					_host.lock()->sendCustomMessage(it->second.fromNodeID, buffer);
				}
			}
			else {
				LOG(DEBUG) << "received SDK channel2 message response:" << message->seq;

				auto buffer = std::make_shared<bytes>();
				message->encode(*buffer);

				LOG(DEBUG) << "encode message to enpoint:" << it->second.fromNodeID;
				_host.lock()->sendCustomMessage(it->second.fromNodeID, buffer);

				_seq2MessageSession.erase(it);
			}
		}
		catch (exception &e) {
			LOG(ERROR) << "send response error:" << e.what();
		}
	}
	else {
		LOG(ERROR) << "unknow type:" << message->type;
	}
}

void dev::ChannelRPCServer::onNodeRequest(h512 nodeID, std::shared_ptr<dev::bytes> message) {
	auto msg = std::make_shared<dev::channel::Message>();
	ssize_t result = msg->decode(message->data(), message->size());

	if (result <= 0) {
		LOG(ERROR) << "decode error:" << result << " package size:" << message->size();
		return;
	}

	LOG(DEBUG) << "receive node message length:" << message->size() << " type:" << msg->type << " seq:" << msg->seq;

	switch (msg->type) {
	case 0x20:
	case 0x21:
		onNodeMessage(nodeID, msg);
		break;
	case 0x30:
	case 0x31:
		onNodeChannelRequest(nodeID, msg);
		break;
	default:
		break;
	}
}

void dev::ChannelRPCServer::onNodeMessage(h512 nodeID, dev::channel::Message::Ptr message) {
	LOG(DEBUG) << "received channel message from node:" << message->data->size() + 14;

	bool sended = false;

	std::lock_guard<std::mutex> lock(_seqMutex);
	auto it = _seq2session.find(message->seq);
	if (it != _seq2session.end()) {
		LOG(DEBUG) << "response message seq:" << message->seq;

		if (it->second->actived()) {
			it->second->asyncSendMessage(message, dev::channel::ChannelSession::CallbackType(), 0);

			LOG(DEBUG) << "response message to seq[" << it->first << "] [" << it->second->host() << ":" << it->second->port() << "]successed";
			sended = true;
		}
	}

	if (!sended) {
		LOG(DEBUG) << "no found seq，PUSH";

		for (auto it : _sessions) {
			if (it.second->actived()) {
				it.second->asyncSendMessage(message, dev::channel::ChannelSession::CallbackType(), 0);

				LOG(DEBUG) << "push to session[" << it.first << "] [" << it.second->host() << ":" << it.second->port() << "]successed";
				sended = true;
				break;
			}
		}
	}

	if (!sended) {
		LOG(ERROR) << "push message failed，sdk connection not found，code 101";

		if (message->result == 0) {
			message->result = REMOTE_CLIENT_PEER_UNAVAILBLE;
			message->type = 0x21;
			message->data->clear();

			auto buffer = make_shared<bytes>();
			message->encode(*buffer);

			_host.lock()->sendCustomMessage(nodeID, buffer);
		}
	}
}

void ChannelRPCServer::onNodeChannelRequest(h512 nodeID, dev::channel::Message::Ptr message) {
	LOG(DEBUG) << "receive from node:" << nodeID << " channel message, length:" << message->data->size() + 14;

	if (message->data->size() < 1) {
		LOG(ERROR) << "invaild channel message，too short:" << message->data->size();
		return;
	}

	uint8_t topicLen = *((uint8_t*)message->data->data());
	std::string topic((char*)message->data->data() + 1, topicLen - 1);

	LOG(DEBUG) << "target topic:" << topic;

	std::lock_guard<std::mutex> lock(_seqMessageMutex);
	auto it = _seq2MessageSession.find(message->seq);

	if (message->type == 0x30) {
		try {
			if (it == _seq2MessageSession.end()) {
				LOG(DEBUG) << "new channel message";

				ChannelMessageSession messageSession;
				messageSession.fromNodeID = nodeID;
				messageSession.message = message;

				auto newIt = _seq2MessageSession.insert(std::make_pair(message->seq, messageSession));
				if (newIt.second == false) {
					LOG(WARNING) << "seq:" << message->seq << " session repeat，override";

					newIt.first->second = messageSession;
				}

				it = newIt.first;
			}

			auto session = sendChannelMessageToSession(topic, message, it->second.failedSessions);

			it->second.toSession = session;
		}
		catch (exception &e) {
			LOG(ERROR) << "push message totally failed:" << e.what();

			message->result = REMOTE_CLIENT_PEER_UNAVAILBLE;
			message->type = 0x31;
			message->data->clear();

			auto buffer = make_shared<bytes>();
			message->encode(*buffer);

			_host.lock()->sendCustomMessage(nodeID, buffer);
		}
	}
	else if (message->type == 0x31) {
		if (it == _seq2MessageSession.end()) {
			LOG(ERROR) << "error，no found session:" << message->seq;
			return;
		}

		if (message->result != 0) {
			LOG(DEBUG) << "message:" << message->seq << " send to node" << it->second.toNodeID << "failed:" << message->result;
			try {
				it->second.failedNodeIDs.insert(it->second.toNodeID);

				h512 nodeID = sendChannelMessageToNode(topic, it->second.message, it->second.failedNodeIDs);

				LOG(DEBUG) << "try send to node:" << nodeID << " successed";
				it->second.toNodeID = nodeID;
			}
			catch (exception &e) {
				LOG(ERROR) << "process response failed:" << e.what();

				message->type = 0x31;
				message->result = REMOTE_PEER_UNAVAILIBLE;
				message->data->clear();

				it->second.fromSession->asyncSendMessage(message, dev::channel::ChannelSession::CallbackType(), 0);
			}
		}
		else {
			LOG(DEBUG) << "response message seq:" << message->seq;

			if (it->second.fromSession->actived()) {
				it->second.fromSession->asyncSendMessage(message, dev::channel::ChannelSession::CallbackType(), 0);

				LOG(DEBUG) << "response to seq[" << it->first << "] [" << it->second.fromSession->host()
				           << ":" << it->second.fromSession->port() << "]successed";
			}

			_seq2MessageSession.erase(it);
		}
	}
}

void ChannelRPCServer::setListenAddr(const std::string& listenAddr) {
	_listenAddr = listenAddr;
}

void ChannelRPCServer::setListenPort(int listenPort) {
	_listenPort = listenPort;
}

void ChannelRPCServer::CloseConnection(int _socket)
{
	close(_socket);
}

Web3Observer::Ptr ChannelRPCServer::buildObserver() {
	class Web3ObserverImpl: public Web3Observer {
	public:
		Web3ObserverImpl(ChannelRPCServer::Ptr server): _server(server) {};
		virtual ~Web3ObserverImpl() {}

		virtual void onReceiveChannelMessage(const h512 nodeID, std::shared_ptr<bytes> buffer) {
			_server->onNodeRequest(nodeID, buffer);
		}

	private:
		ChannelRPCServer::Ptr _server;
	};

	return std::make_shared<Web3ObserverImpl>(shared_from_this());
}

void ChannelRPCServer::setHost(std::weak_ptr<EthereumHost> host) {
	_host = host;
}

h512 ChannelRPCServer::sendChannelMessageToNode(std::string topic, dev::channel::Message::Ptr message, const std::set<h512> &exclude) {
	std::vector<p2p::NodeID> peers = _host.lock()->getPeersByTopic(topic);

	for (auto it = peers.begin(); it != peers.end();) {
		if (exclude.find(*it) != exclude.end()) {
			it = peers.erase(it);
		}
		else {
			++it;
		}
	}

	if (peers.empty()) {
		LOG(ERROR) << "send failed，nobody care topic:" << topic;

		throw dev::channel::ChannelException(103, "send failed，nobody care topic:" + topic);
	}

	boost::mt19937 rng(static_cast<unsigned>(std::time(0)));
	boost::uniform_int<int> index(0, peers.size() - 1);

	auto ri = index(rng);
	LOG(DEBUG) << "random:" << ri;
	p2p::NodeID targetNodeID = peers[ri];

	auto buffer = std::make_shared<bytes>();
	message->encode(*buffer);

	_host.lock()->sendCustomMessage(targetNodeID, buffer);

	LOG(DEBUG) << "message send to " << targetNodeID;

	return targetNodeID;
}

dev::channel::ChannelSession::Ptr ChannelRPCServer::sendChannelMessageToSession(std::string topic, dev::channel::Message::Ptr message, const std::set<dev::channel::ChannelSession::Ptr> &exclude) {
	std::vector<dev::channel::ChannelSession::Ptr> activedSessions =
	    getSessionByTopic(topic);

	for (auto sessionIt = activedSessions.begin();
	        sessionIt != activedSessions.end();) {
		if (exclude.find(*sessionIt)
		        != exclude.end()) {
			sessionIt = activedSessions.erase(sessionIt);
		} else {
			++sessionIt;
		}
	}

	if (activedSessions.empty()) {
		LOG(ERROR) << "nobody care topic:" << topic;
		throw dev::channel::ChannelException(104, "nobody care topic:" + topic);
	}

	boost::mt19937 rng(static_cast<unsigned>(std::time(0)));
	boost::uniform_int<int> index(0, activedSessions.size() - 1);

	auto ri = index(rng);
	LOG(DEBUG) << "random node:" << ri;

	auto session = activedSessions[ri];

	session->asyncSendMessage(message,
	                          dev::channel::ChannelSession::CallbackType(), 0);

	LOG(DEBUG) << "push to session: " << session->host() << ":" << session->port() << " successed";

	return session;
}

void ChannelRPCServer::updateHostTopics() {
	std::shared_ptr<std::set<std::string> > allTopics = std::make_shared<std::set<std::string> >();

	for (auto it : _sessions) {
		auto topics = it.second->topics();
		allTopics->insert(topics->begin(), topics->end());
	}

	_host.lock()->setTopics(allTopics);
}

std::vector<dev::channel::ChannelSession::Ptr> ChannelRPCServer::getSessionByTopic(const std::string &topic) {
	std::vector<dev::channel::ChannelSession::Ptr> activedSessions;

	for (auto it : _sessions) {
		if (it.second->topics()->empty() || !it.second->actived()) {
			continue;
		}

		auto topicIt = it.second->topics()->find(topic);
		if (topicIt != it.second->topics()->end()) {
			activedSessions.push_back(it.second);
		}
	}

	return activedSessions;
}

std::string ChannelRPCServer::topicStrip(std::string topic) {
	topic.push_back('\0');
	std::string r = std::string(topic.c_str());

	return r;
}