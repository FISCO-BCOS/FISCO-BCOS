/*
 * ConsoleServer.cpp
 *
 *  Created on: 2018年8月15日
 *      Author: monan
 */

#include "ConsoleServer.h"
#include <libpbftseal/PBFT.h>
#include <boost/throw_exception.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace dev;
using namespace console;

ConsoleServer::ConsoleServer() {
}

void ConsoleServer::StartListening() {
	try {
		LOG(DEBUG) << "ConsoleServer started";

		std::function<void(dev::channel::ChannelException, dev::channel::ChannelSession::Ptr)> fp = std::bind(&ConsoleServer::onConnect, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
		_server->setConnectionHandler(fp);

		_server->run();

		_running = true;
	}
	catch(std::exception &e) {
		LOG(ERROR) << "StartListening failed! " << boost::diagnostic_information(e);

		BOOST_THROW_EXCEPTION(e);
	}
}

void ConsoleServer::StopListening() {
	if (_running) {
		_server->stop();
	}

	_running = false;
}

void ConsoleServer::onConnect(dev::channel::ChannelException e, dev::channel::ChannelSession::Ptr session) {
	if(e.errorCode() != 0) {
		LOG(ERROR) << "Accept console connection error!" << boost::diagnostic_information(e);

		return;
	}

	std::function<void(dev::channel::ChannelSession::Ptr, dev::channel::ChannelException, dev::channel::Message::Ptr)> fp = std::bind(
			&ConsoleServer::onRequest,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2,
			std::placeholders::_3);
	session->setMessageHandler(fp);

	session->run();
}

void ConsoleServer::onRequest(dev::channel::ChannelSession::Ptr session, dev::channel::ChannelException e, dev::channel::Message::Ptr message) {
	if(e.errorCode() != 0) {
		LOG(ERROR) << "ConsoleServer onRequest error: " << boost::diagnostic_information(e);

		return;
	}

	std::string output;
	try {
		std::string request((const char*)message->data(), message->dataSize());
		LOG(TRACE) << "ConsoleServer onRequest: " << request;

		std::vector<std::string> args;
		boost::split(args, request, boost::is_any_of(" "));

		if(args.empty()) {
			output = "Emput input!";

			throw std::exception();
		}

		std::string func = args[0];
		args.erase(args.begin());

		if(func == "help") {
			output = help(args);
		}
		else if(func == "status") {
			output = status(args);
		}
		else {
			output = "Unknown command, enter 'help' for command list";
		}
	}
	catch(std::exception &e) {
		LOG(WARNING) << "Error: " << boost::diagnostic_information(e);
	}

	auto response = session->messageFactory()->buildMessage();
	response->setData((byte*)output.data(), output.size());

	session->asyncSendMessage(response, dev::channel::ChannelSession::CallbackType(), 0);

}

std::string ConsoleServer::help(const std::vector<std::string> args) {
	std::stringstream ss;

	//ss << ""
}

std::string ConsoleServer::status(const std::vector<std::string> args) {
	std::string output;

	try {
		std::stringstream ss;

		ss << "=============Node status=============\n";
		ss << "Status: ";
		if(_interface->wouldSeal()) {
			ss << "sealing";
		}
		else if(_interface->isSyncing() || _interface->isMajorSyncing()){
			ss << "syncing block...";
		}

		ss << "\n";

		ss << "Block number:" << _interface->number() << " at view:" << dynamic_cast<dev::eth::PBFT*>(_interface->sealEngine())->view() << "\n";

		output = ss.str();
	}
	catch(std::exception &e) {
		LOG(ERROR) << "ERROR: " << boost::diagnostic_information(e);

		output = "ERROR while status";
	}

	return output;
}
