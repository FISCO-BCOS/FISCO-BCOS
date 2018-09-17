/*
 * ConsoleServer.h
 *
 *  Created on: 2018年8月13日
 *      Author: monan
 */

#pragma once

#include <libchannelserver/ChannelServer.h>
#include <libethereum/Interface.h>
#include <libstorage/Storage.h>
#include <libp2p/Host.h>

namespace dev {

namespace console {

class ConsoleServer: public std::enable_shared_from_this<ConsoleServer> {
public:
	typedef std::shared_ptr<ConsoleServer> Ptr;

	ConsoleServer();
	virtual ~ConsoleServer() {};

	virtual void StartListening();
	virtual void StopListening();

	void setChannelServer(std::shared_ptr<dev::channel::ChannelServer> server) { _server = server; }
	void setInterface(dev::eth::Interface *interface) { _interface = interface; }
	void setHost(dev::p2p::Host::Ptr host) { _host = host; }
	void setStateStorage(dev::storage::Storage::Ptr stateStorage) { _stateStorage = stateStorage; }
	void onConnect(dev::channel::ChannelException e, dev::channel::ChannelSession::Ptr session);
	void onRequest(dev::channel::ChannelSession::Ptr session, dev::channel::ChannelException e, dev::channel::Message::Ptr message);

	std::string help(const std::vector<std::string> args);
	std::string status(const std::vector<std::string> args);
	std::string p2pPeers(const std::vector<std::string> args);
	std::string p2pMiners(const std::vector<std::string> args);
	std::string p2pUpdate(const std::vector<std::string> args);
	std::string amdbSelect(const std::vector<std::string> args);

private:
	std::shared_ptr<dev::channel::ChannelServer> _server;
	dev::eth::Interface *_interface = nullptr;
	dev::p2p::Host::Ptr _host = nullptr;
	dev::storage::Storage::Ptr _stateStorage = nullptr;
	bool _running = false;

	void printSingleLine(std::stringstream &ss);
	void printDoubleLine(std::stringstream &ss);
};

}
}
