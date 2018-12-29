/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file P2PInitializer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#include "P2PInitializer.h"
#include <libdevcore/easylog.h>
#include <libnetwork/Host.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace dev;
using namespace dev::p2p;
using namespace dev::initializer;

void P2PInitializer::initConfig(boost::property_tree::ptree const& _pt)
{
    INITIALIZER_LOG(DEBUG) << "[#P2PInitializer::initConfig]";
    std::string listenIP = _pt.get<std::string>("p2p.listen_ip", "0.0.0.0");
    int listenPort = _pt.get<int>("p2p.listen_port", 30300);
    try
    {
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
                    boost::split(
                        s, it.second.data(), boost::is_any_of(":"), boost::token_compress_on);
                    if (s.size() != 2)
                    {
                        INITIALIZER_LOG(ERROR)
                            << "[#P2PInitializer::initConfig] parse address faield: [data]: "
                            << it.second.data();
                        ERROR_OUTPUT
                            << "[#P2PInitializer::initConfig] parse address faield, [data]: "
                            << it.second.data() << std::endl;
                        exit(1);
                    }
                    NodeIPEndpoint endpoint;
                    endpoint.address = boost::asio::ip::address::from_string(s[0]);
                    endpoint.tcpPort = boost::lexical_cast<uint16_t>(s[1]);
                    endpoint.udpPort = boost::lexical_cast<uint16_t>(s[1]);
                    nodes.insert(std::make_pair(endpoint, NodeID()));
                }
                catch (std::exception& e)
                {
                    INITIALIZER_LOG(ERROR)
                        << "[#P2PInitializer::initConfig] parse address faield: [data/EINFO]: "
                        << it.second.data() << "/" << boost::diagnostic_information(e);
                    ERROR_OUTPUT
                        << "[#P2PInitializer::initConfig] parse address faield: [data/EINFO]:"
                        << it.second.data() << "/" << boost::diagnostic_information(e) << std::endl;
                    exit(1);
                }
            }
        }

        auto asioInterface = std::make_shared<dev::network::ASIOInterface>();
        asioInterface->setIOService(std::make_shared<ba::io_service>());
        asioInterface->setSSLContext(m_SSLContext);
        asioInterface->setType(dev::network::ASIOInterface::SSL);

        auto messageFactory = std::make_shared<P2PMessageFactory>();

        auto host = std::make_shared<dev::network::Host>();
        host->setASIOInterface(asioInterface);
        host->setSessionFactory(std::make_shared<dev::network::SessionFactory>());
        host->setMessageFactory(messageFactory);
        host->setHostPort(listenIP, listenPort);
        host->setThreadPool(std::make_shared<ThreadPool>("P2P", 4));

        m_p2pService = std::make_shared<Service>();
        m_p2pService->setHost(host);
        m_p2pService->setStaticNodes(nodes);
        m_p2pService->setKeyPair(m_keyPair);
        m_p2pService->setP2PMessageFactory(messageFactory);

        m_p2pService->start();
    }
    catch (std::exception& e)
    {
        INITIALIZER_LOG(ERROR) << "[#P2PInitializer] initConfig for P2PInitializer failed [EINFO]: "
                               << boost::diagnostic_information(e);
        ERROR_OUTPUT << "[#P2PInitializer] P2PInitializer failed! Please check port: " << listenPort
                     << " is unused! ERROR:" << boost::diagnostic_information(e) << std::endl;
        exit(1);
    }
}
