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
#include "libp2p/P2PMessage.h"
#include <libcompress/LZ4Compress.h>
#include <libcompress/SnappyCompress.h>
#include <libdevcore/easylog.h>
#include <libnetwork/Host.h>
#include <boost/algorithm/algorithm.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace dev;
using namespace dev::p2p;
using namespace dev::initializer;

void P2PInitializer::initConfig(boost::property_tree::ptree const& _pt)
{
    INITIALIZER_LOG(DEBUG) << LOG_BADGE("P2PInitializer") << LOG_DESC("initConfig");
    std::string listenIP = _pt.get<std::string>("p2p.listen_ip", "0.0.0.0");
    int listenPort = _pt.get<int>("p2p.listen_port", 30300);
    try
    {
        std::map<NodeIPEndpoint, NodeID> nodes;
        for (auto it : _pt.get_child("p2p"))
        {
            if (it.first.find("node.") == 0)
            {
                INITIALIZER_LOG(TRACE)
                    << LOG_BADGE("P2PInitializer") << LOG_DESC("initConfig add staticNode")
                    << LOG_KV("data", it.second.data());

                std::vector<std::string> s;
                try
                {
                    boost::split(
                        s, it.second.data(), boost::is_any_of(":"), boost::token_compress_on);
                    if (s.size() != 2)
                    {
                        INITIALIZER_LOG(ERROR) << LOG_BADGE("P2PInitializer")
                                               << LOG_DESC("initConfig parse config faield")
                                               << LOG_KV("data", it.second.data());
                        ERROR_OUTPUT << LOG_BADGE("P2PInitializer")
                                     << LOG_DESC("initConfig parse config faield")
                                     << LOG_KV("data", it.second.data()) << std::endl;
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
                        << LOG_BADGE("P2PInitializer") << LOG_DESC("parse address faield")
                        << LOG_KV("data", it.second.data())
                        << LOG_KV("EINFO", boost::diagnostic_information(e));
                    ERROR_OUTPUT << LOG_BADGE("P2PInitializer") << LOG_DESC("parse address faield")
                                 << LOG_KV("data", it.second.data())
                                 << LOG_KV("EINFO", boost::diagnostic_information(e)) << std::endl;
                    exit(1);
                }
            }
        }

        std::vector<std::string> crl;
        /// CRL means certificate rejected list, CRL optional in config.ini
        if (_pt.get_child_optional("crl"))
        {
            for (auto it : _pt.get_child("crl"))
            {
                if (it.first.find("crl.") == 0)
                {
                    try
                    {
                        std::string nodeID = boost::to_upper_copy(it.second.data());
                        INITIALIZER_LOG(TRACE) << LOG_BADGE("P2PInitializer")
                                               << LOG_DESC("get certificate rejected by nodeID")
                                               << LOG_KV("nodeID", nodeID);
                        crl.push_back(nodeID);
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

        /// compress related option
        std::string compressAlogrithm = pt.get<std::string>("compress.algorithm", "");
        std::shared_ptr<dev::compress::CompressInterface> compressHandler = nullptr;
        if (dev::stringCmpIgnoreCase(compressAlogrithm, "snappy") == 0)
        {
            compressHandler = std::make_shared<dev::compress::SnappyCompress>();
        }
        if (dev::stringCmpIgnoreCase(compressAlogrithm, "lz4") == 0)
        {
            compressHandler = std::make_shared<dev::compress::LZ4Compress>();
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
        host->setCRL(crl);

        m_p2pService = std::make_shared<Service>();
        m_p2pService->setHost(host);
        m_p2pService->setStaticNodes(nodes);
        m_p2pService->setKeyPair(m_keyPair);
        m_p2pService->setP2PMessageFactory(messageFactory);
        /// set compress handler
        m_p2pService->setCompressHandler(compressHandler);
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
