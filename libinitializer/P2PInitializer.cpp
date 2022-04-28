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
#include "libdevcore/ThreadPool.h"
#include "libnetwork/ASIOInterface.h"
#include "libnetwork/Session.h"
#include "libp2p/P2PMessageFactory.h"
#include <libnetwork/Host.h>
#include <libnetwork/PeerWhitelist.h>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace std;
using namespace dev;
using namespace dev::p2p;
using namespace dev::initializer;

void P2PInitializer::initConfig(boost::property_tree::ptree const& _pt)
{
    INITIALIZER_LOG(DEBUG) << LOG_BADGE("P2PInitializer") << LOG_DESC("initConfig");
    std::string listenIP = _pt.get<std::string>("p2p.listen_ip", "0.0.0.0");
    int listenPort = _pt.get<int>("p2p.listen_port", 30300);
    if (!isValidPort(listenPort))
    {
        BOOST_THROW_EXCEPTION(
            InvalidPort() << errinfo_comment(
                "P2PInitializer:  initConfig for P2PInitializer failed! Invalid ListenPort!"));
    }
    int peersParamLimit = _pt.get<int>("p2p.peers_param_limit", 10);
    if (peersParamLimit <= 0)
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("P2PInitializer") << LOG_DESC("Invalid peersParamLimit")
                               << LOG_KV("peers_param_limit", peersParamLimit);
        ERROR_OUTPUT << LOG_BADGE("P2PInitializer") << LOG_DESC("Invalid peersParamLimit")
                     << LOG_KV("peers_param_limit", peersParamLimit) << std::endl;
        exit(1);
    }
    int maxNodesLimit = _pt.get<int>("p2p.max_nodes_linit", 100);
    if (maxNodesLimit <= 0)
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("P2PInitializer") << LOG_DESC("Invalid maxNodesLimit")
                               << LOG_KV("max_nodes_limit", maxNodesLimit);
        ERROR_OUTPUT << LOG_BADGE("P2PInitializer") << LOG_DESC("Invalid maxNodesLimit")
                     << LOG_KV("max_nodes_limit", maxNodesLimit) << std::endl;
        exit(1);
    }
    std::string certBlacklistSection = "crl";
    if (_pt.get_child_optional("certificate_blacklist"))
    {
        certBlacklistSection = "certificate_blacklist";
    }
    try
    {
        std::map<NodeIPEndpoint, NodeID> nodes;
        for (auto it : _pt.get_child("p2p"))
        {
            if (it.first.find("node.") == 0)
            {
                INITIALIZER_LOG(TRACE)
                    << LOG_BADGE("P2PInitializer") << LOG_DESC("initConfig add staticNode")
                    << LOG_KV("endpoint", it.second.data());
                std::vector<std::string> s;
                try
                {
                    boost::split(
                        s, it.second.data(), boost::is_any_of("]"), boost::token_compress_on);

                    if (s.size() == 2)
                    {  // ipv6
                        boost::asio::ip::address ip_address =
                            boost::asio::ip::make_address(s[0].data() + 1);
                        uint16_t port = boost::lexical_cast<uint16_t>(s[1].data() + 1);
                        nodes.insert(
                            std::make_pair(NodeIPEndpoint{ip_address, port}, NodeID()));
                    }
                    else if (s.size() == 1)
                    {  // ipv4 and ipv4 host
                        std::vector<std::string> ipv4Endpoint;
                        boost::split(ipv4Endpoint, it.second.data(), boost::is_any_of(":"),
                            boost::token_compress_on);
                        uint16_t port = boost::lexical_cast<uint16_t>(ipv4Endpoint[1]);
                        nodes.insert(
                            std::make_pair(NodeIPEndpoint{ipv4Endpoint[0], port}, NodeID()));
                    }
                    else
                    {
                        INITIALIZER_LOG(ERROR) << LOG_BADGE("P2PInitializer")
                                               << LOG_DESC("initConfig parse config failed")
                                               << LOG_KV("data", it.second.data());
                        ERROR_OUTPUT << LOG_BADGE("P2PInitializer")
                                     << LOG_DESC("initConfig parse config failed")
                                     << LOG_KV("endpoint", it.second.data()) << std::endl;
                        exit(1);
                    }
                }
                catch (std::exception& e)
                {
                    INITIALIZER_LOG(ERROR)
                        << LOG_BADGE("P2PInitializer") << LOG_DESC("parse address failed")
                        << LOG_KV("endpoint", it.second.data())
                        << LOG_KV("EINFO", boost::diagnostic_information(e));
                    ERROR_OUTPUT << LOG_BADGE("P2PInitializer") << LOG_DESC("parse address failed")
                                 << LOG_KV("endpoint", it.second.data())
                                 << LOG_KV("EINFO", boost::diagnostic_information(e)) << std::endl;
                    exit(1);
                }
            }
        }

        std::vector<std::string> certBlacklist;
        /// CRL means certificate rejected list, CRL optional in config.ini
        if (_pt.get_child_optional(certBlacklistSection))
        {
            for (auto it : _pt.get_child(certBlacklistSection))
            {
                if (it.first.find("crl.") == 0)
                {
                    try
                    {
                        std::string nodeID = boost::to_upper_copy(it.second.data());
                        INITIALIZER_LOG(TRACE) << LOG_BADGE("P2PInitializer")
                                               << LOG_DESC("get certificate rejected by nodeID")
                                               << LOG_KV("nodeID", nodeID);
                        certBlacklist.push_back(nodeID);
                    }
                    catch (std::exception& e)
                    {
                        INITIALIZER_LOG(ERROR) << LOG_BADGE("P2PInitializer")
                                               << LOG_DESC("get certificate rejected failed")
                                               << LOG_KV("EINFO", boost::diagnostic_information(e));
                    }
                }
            }
        }

        auto whitelist = parseWhitelistFromPropertyTree(_pt);

        auto asioInterface = std::make_shared<dev::network::ASIOInterface>();
        asioInterface->setIOService(std::make_shared<ba::io_service>());
        asioInterface->setSSLContext(m_SSLContext);
        asioInterface->setType(dev::network::ASIOInterface::SSL);

        std::shared_ptr<P2PMessageFactory> messageFactory = nullptr;

        if (g_BCOSConfig.version() >= dev::RC2_VERSION)
        {
            messageFactory = std::make_shared<P2PMessageFactoryRC2>();
        }
        else if (g_BCOSConfig.version() <= dev::RC1_VERSION)
        {
            messageFactory = std::make_shared<P2PMessageFactory>();
        }

        auto host = std::make_shared<dev::network::Host>();
        host->setASIOInterface(asioInterface);
        host->setSessionFactory(std::make_shared<dev::network::SessionFactory>());
        host->setMessageFactory(messageFactory);
        host->setHostPort(listenIP, listenPort);
        host->setThreadPool(std::make_shared<ThreadPool>("P2P", 4));
        host->setCRL(certBlacklist);

        m_p2pService = std::make_shared<Service>();
        m_p2pService->setHost(host);
        m_p2pService->setStaticNodes(nodes);
        m_p2pService->setKeyPair(m_keyPair);
        m_p2pService->setP2PMessageFactory(messageFactory);
        m_p2pService->setWhitelist(whitelist);
        m_p2pService->setPeersParamLimit(peersParamLimit);
        m_p2pService->setMaxNodesLimit(maxNodesLimit);
        // start the p2pService
        m_p2pService->start();
    }
    catch (std::exception& e)
    {
        // TODO: catch in Initializer::init, delete this catch
        INITIALIZER_LOG(ERROR) << LOG_BADGE("P2PInitializer")
                               << LOG_DESC("initConfig for P2PInitializer failed")
                               << LOG_KV("check Port", listenPort)
                               << LOG_KV("EINFO", boost::diagnostic_information(e));
        ERROR_OUTPUT << LOG_BADGE("P2PInitializer")
                     << LOG_DESC("initConfig for P2PInitializer failed")
                     << LOG_KV("check Port", listenPort)
                     << LOG_KV("EINFO", boost::diagnostic_information(e)) << std::endl;
        exit(1);
    }
}

void P2PInitializer::resetWhitelist(const std::string& _configFile)
{
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(_configFile, pt);

    auto whitelist = parseWhitelistFromPropertyTree(pt);

    p2pService()->setWhitelist(whitelist);
}

PeerWhitelist::Ptr P2PInitializer::parseWhitelistFromPropertyTree(
    boost::property_tree::ptree const& _pt)
{
    std::string certWhitelistSection = "cal";
    if (_pt.get_child_optional("certificate_whitelist"))
    {
        certWhitelistSection = "certificate_whitelist";
    }
    std::vector<std::string> certWhitelist;
    bool enableWhitelist = false;
    // CAL means certificate accepted list, CAL optional in config.ini
    if (_pt.get_child_optional(certWhitelistSection))
    {
        for (auto it : _pt.get_child(certWhitelistSection))
        {
            if (it.first.find("cal.") == 0)
            {
                try
                {
                    std::string nodeID = boost::to_upper_copy(it.second.data());
                    INITIALIZER_LOG(DEBUG) << LOG_BADGE("P2PInitializer")
                                           << LOG_DESC("get certificate accepted by nodeID")
                                           << LOG_KV("nodeID", nodeID);

                    if (isNodeIDOk(nodeID))
                    {
                        enableWhitelist = true;
                        certWhitelist.push_back(nodeID);
                    }
                    else
                    {
                        INITIALIZER_LOG(ERROR)
                            << LOG_BADGE("P2PInitializer")
                            << LOG_DESC("get certificate accepted by nodeID failed, illegal nodeID")
                            << LOG_KV("nodeID", nodeID);
                    }
                }
                catch (std::exception& e)
                {
                    INITIALIZER_LOG(ERROR) << LOG_BADGE("P2PInitializer")
                                           << LOG_DESC("get certificate accepted failed")
                                           << LOG_KV("EINFO", boost::diagnostic_information(e));
                }
            }
        }
    }
    return std::make_shared<PeerWhitelist>(certWhitelist, enableWhitelist);
}
