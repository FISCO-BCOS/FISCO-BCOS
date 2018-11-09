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
/** @file P2PInitializer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#include "P2PInitializer.h"
#include <libp2p/Host.h>
#include <libp2p/Network.h>
#include <libp2p/P2pFactory.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace dev;
using namespace dev::p2p;
using namespace dev::initializer;

void P2PInitializer::initConfig(boost::property_tree::ptree const& _pt)
{
    INITIALIZER_LOG(INFO) << "[#P2PInitializer::initConfig]";
    std::string publicID = _pt.get<std::string>("p2p.public_ip", "127.0.0.1");
    std::string listenIP = _pt.get<std::string>("p2p.listen_ip", "0.0.0.0");
    int listenPort = _pt.get<int>("p2p.listen_port", 30300);
    NetworkConfig network_config = NetworkConfig(publicID, listenIP, listenPort);

    std::map<NodeIPEndpoint, NodeID> nodes;
    for (auto it : _pt.get_child("p2p"))
    {
        if (it.first.find("node.") == 0)
        {
            SESSION_LOG(TRACE) << "[#P2PInitializer::initConfig] add staticNode: " << it.first
                               << "/" << it.second.data();

            std::vector<std::string> s;
            try
            {
                boost::split(s, it.second.data(), boost::is_any_of(":"), boost::token_compress_on);

                if (s.size() != 2)
                {
                    SESSION_LOG(ERROR)
                        << "[#P2PInitializer::initConfig] parse address faield: [data]: "
                        << it.second.data();
                    continue;
                }

                NodeIPEndpoint endpoint;
                endpoint.address = boost::asio::ip::address::from_string(s[0]);
                endpoint.tcpPort = boost::lexical_cast<uint16_t>(s[1]);
                endpoint.host = s[0];

                nodes.insert(std::make_pair(endpoint, NodeID()));
            }
            catch (std::exception& e)
            {
                SESSION_LOG(ERROR)
                    << "[#P2PInitializer::initConfig] parse address faield: [data/EINFO]: "
                    << it.second.data() << "/" << e.what();
                continue;
            }
        }
    }

    auto p2pMsgHandler = std::make_shared<P2PMsgHandler>();
    auto asioInterface = std::make_shared<AsioInterface>();
    auto socketFactory = std::make_shared<SocketFactory>();
    auto sessionFactory = std::make_shared<SessionFactory>();
    auto host = std::make_shared<Host>("2.0", m_keyPair, network_config, asioInterface,
        socketFactory, sessionFactory, m_SSLContext);
    host->setStaticNodes(nodes);
    m_p2pService = std::make_shared<Service>(host, p2pMsgHandler);
    m_p2pService->setMessageFactory(std::make_shared<P2PMessageFactory>());
    host->start();
}