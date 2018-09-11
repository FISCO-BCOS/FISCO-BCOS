/*
 * ConsoleServer.h
 *
 *  Created on: 2018年8月13日
 *      Author: monan
 */

#pragma once

#include <libchannelserver/ChannelServer.h>
#include <libwebthree/WebThree.h>
#include <libethereum/Interface.h>
#include <libstorage/Storage.h>

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
	void setWebThreeDirect(dev::WebThreeDirect *webThreeDirect) { _webThreeDirect = webThreeDirect; }
	void setStateStorage(dev::storage::Storage::Ptr stateStorage) { _stateStorage = stateStorage; }
	void onConnect(dev::channel::ChannelException e, dev::channel::ChannelSession::Ptr session);
	void onRequest(dev::channel::ChannelSession::Ptr session, dev::channel::ChannelException e, dev::channel::Message::Ptr message);

	std::string help(const std::vector<std::string> args);
	std::string status(const std::vector<std::string> args);
	std::string p2pPeers(const std::vector<std::string> args);
	std::string p2pMiners(const std::vector<std::string> args);
	std::string amdbSelect(const std::vector<std::string> args);

private:
	std::shared_ptr<dev::channel::ChannelServer> _server;
	dev::eth::Interface *_interface = nullptr;
	dev::WebThreeDirect *_webThreeDirect = nullptr;
	dev::storage::Storage::Ptr _stateStorage = nullptr;
	bool _running = false;
};

}
}
