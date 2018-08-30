#include "Initializer.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include <libweb3jsonrpc/ChannelRPCServer.h>

using namespace dev;
using namespace dev::eth;

void Initializer::init(const std::string &path) {
	boost::property_tree::ptree pt;
	boost::property_tree::read_ini(path, pt);

	_commonInitializer = std::make_shared<CommonInitializer>();
	_commonInitializer->initConfig(pt);

	_secureInitializer = std::make_shared<SecureInitializer>();
	_secureInitializer->setDataPath(_commonInitializer->dataPath());
	_secureInitializer->initConfig(pt);

	_rpcInitializer = std::make_shared<RPCInitializer>();
	_rpcInitializer->setSSLContext(_secureInitializer->sslContext());
	_rpcInitializer->initConfig(pt);

	_consoleInitializer = std::make_shared<ConsoleInitializer>();
	_consoleInitializer->setSSLContext(_secureInitializer->sslContext());
	_consoleInitializer->initConfig(pt);

	_amdbInitializer = std::make_shared<AMDBInitializer>();
	_amdbInitializer->setChannelRPCServer(_rpcInitializer->channelRPCServer());
	_amdbInitializer->setDataPath(_commonInitializer->dataPath());
	_amdbInitializer->initConfig(pt);

	_pbftInitializer = std::make_shared<PBFTInitializer>();
	_pbftInitializer->setStateStorage(_amdbInitializer->stateStorage());
	_pbftInitializer->initConfig(pt);

	_p2pInitializer = std::make_shared<p2p::P2PInitializer>();
	_p2pInitializer->setKey(_secureInitializer->key());
	_p2pInitializer->setSSLContext(_secureInitializer->sslContext());
	_p2pInitializer->initConfig(pt);
}

RPCInitializer::Ptr Initializer::rpcInitializer() {
	return _rpcInitializer;
}

CommonInitializer::Ptr Initializer::commonInitializer() {
	return _commonInitializer;
}

SecureInitializer::Ptr Initializer::secureInitializer() {
	return _secureInitializer;
}

AMDBInitializer::Ptr Initializer::amdbInitializer() {
	return _amdbInitializer;
}

PBFTInitializer::Ptr Initializer::pbftInitializer() {
	return _pbftInitializer;
}

p2p::P2PInitializer::Ptr Initializer::p2pInitializer() {
	return _p2pInitializer;
}

ConsoleInitializer::Ptr Initializer::consoleInitializer() {
	return _consoleInitializer;
}
