/*
 * P2PInitializer.cpp
 *
 *  Created on: 2018年5月10日
 *      Author: mo nan
 */

#include "P2PInitializer.h"
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <map>

using namespace dev;
using namespace dev::p2p;

void P2PInitializer::initConfig(const boost::property_tree::ptree &pt) {
	std::string listenIP = pt.get<std::string>("p2p.listen_ip", "0.0.0.0");
	int listenPort = pt.get<int>("p2p.listen_port", 30300);
	int idleConnections = pt.get<int>("p2p.idle_connections", 100);
	int reconnectInerval = pt.get<int>("p2p.reconnect_interval", 60);

	std::map<NodeIPEndpoint, NodeID> nodes;

	for(auto it: pt.get_child("p2p")) {
		if(it.first.find("node.") == 0) {
			LOG(DEBUG) << "Add staticNode: " << it.first << " : " << it.second.data();

			std::vector<std::string> s;
			try {
				boost::split(s, it.second.data(), boost::is_any_of(":"), boost::token_compress_on);

				if(s.size() != 2) {
					LOG(ERROR) << "parse address failed:" << it.second.data();
					continue;
				}

				NodeIPEndpoint endpoint;
				endpoint.address = boost::asio::ip::address::from_string(s[0]);
				endpoint.tcpPort = boost::lexical_cast<uint16_t>(s[1]);

				nodes.insert(std::make_pair(endpoint, NodeID()));
			}
			catch(std::exception &e) {
				LOG(ERROR) << "Parse address faield:" << it.second.data() << ", " << e.what();
				continue;
			}
		}
	}

	auto netPrefs = NetworkPreferences(listenIP, listenPort, false);
	_host = std::make_shared<p2p::Host>("FISCO-BCOS", _key, netPrefs);
	_host->setStaticNodes(nodes);
	_host->setSSLContext(_sslContext);
	if(idleConnections > 0) {
		_host->setIdealPeerCount(idleConnections);
	}

	LOG(DEBUG) << "P2P hosts:" << nodes.size();
}

Host::Ptr P2PInitializer::host() {
	return _host;
}

void P2PInitializer::setKey(KeyPair key) {
	_key = key;
}

void P2PInitializer::setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext) {
	_sslContext = sslContext;
}
