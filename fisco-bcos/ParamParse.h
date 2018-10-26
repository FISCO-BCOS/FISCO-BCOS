/**
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
 *
 * @brief: empty framework for main of lab-bcos
 *
 * @file: ParamParse.h
 * @author: chaychen
 * @date 2018-10-09
 */

#pragma once
#include <json_spirit/JsonSpiritHeaders.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/easylog.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Protocol.h>
#include <libp2p/Host.h>
#include <libp2p/Network.h>
#include <libp2p/P2pFactory.h>
#include <libp2p/Service.h>
#include <boost/program_options.hpp>
#include <memory>

INITIALIZE_EASYLOGGINGPP
using namespace dev;
using namespace dev::p2p;
using namespace dev::eth;
namespace js = json_spirit;
class Params
{
public:
    Params()
      : m_clientVersion("2.0"),
        m_listenIp("127.0.0.1"),
        m_p2pPort(30303),
        m_publicIp("127.0.0.1"),
        m_bootstrapPath(getDataDir().string() + "/bootstrapnodes.json"),
        m_groupSize(1)
    {
        updateBootstrapnodes();
    }

    Params(std::string const& _clientVersion, std::string const& _listenIp,
        uint16_t const& _p2pPort, std::string const& _publicIp, std::string const& _bootstrapPath,
        std::string const& _minerPath, uint16_t _sendMsgType)
      : m_clientVersion(_clientVersion),
        m_listenIp(_listenIp),
        m_p2pPort(_p2pPort),
        m_txSpeed(10),
        m_publicIp(_publicIp),
        m_bootstrapPath(_bootstrapPath),
        m_groupSize(1)
    {
        updateBootstrapnodes();
    }

    Params(boost::program_options::variables_map const& vm,
        boost::program_options::options_description const& option)
      : m_clientVersion("2.0"),
        m_listenIp("127.0.0.1"),
        m_p2pPort(30303),
        m_txSpeed(10),
        m_publicIp("127.0.0.1"),
        m_bootstrapPath(getDataDir().string() + "/bootstrapnodes.json"),
        m_groupSize(1)
    {
        initParams(vm, option);
        updateBootstrapnodes();
    }

    void initParams(boost::program_options::variables_map const& vm,
        boost::program_options::options_description const& option)
    {
        if (vm.count("client_version") || vm.count("v"))
            m_clientVersion = vm["client_version"].as<std::string>();
        if (vm.count("public_ip") || vm.count("i"))
            m_publicIp = vm["public_ip"].as<std::string>();
        if (vm.count("listen_ip") || vm.count("l"))
            m_listenIp = vm["listen_ip"].as<std::string>();
        if (vm.count("p2p_port") || vm.count("p"))
            m_p2pPort = vm["p2p_port"].as<uint16_t>();
        if (vm.count("bootstrap") || vm.count("b"))
            m_bootstrapPath = vm["bootstrap"].as<std::string>();
        if (vm.count("txSpeed") || vm.count("t"))
            m_txSpeed = vm["txSpeed"].as<float>();
        if (vm.count("groupSize") || vm.count("n"))
            m_groupSize = vm["groupSize"].as<int>();
    }
    /// --- set interfaces ---
    void setClientVersion(std::string const& _clientVersion) { m_clientVersion = _clientVersion; }
    void setListenIp(std::string const& _listenIp) { m_listenIp = _listenIp; }
    void setP2pPort(uint16_t const& _p2pPort) { m_p2pPort = _p2pPort; }
    void setPublicIp(std::string const& _publicIp) { m_publicIp = _publicIp; }
    /// --- get interfaces ---
    const std::string& clientVersion() const { return m_clientVersion; }
    const std::string& listenIp() const { return m_listenIp; }
    const uint16_t& p2pPort() const { return m_p2pPort; }
    const std::string& publicIp() const { return m_publicIp; }
    const int groupSize() const { return m_groupSize; }
    /// --- init NetworkConfig ----
    std::shared_ptr<NetworkConfig> creatNetworkConfig()
    {
        std::shared_ptr<NetworkConfig> network_config =
            std::make_shared<NetworkConfig>(m_publicIp, m_listenIp, m_p2pPort);
        return network_config;
    }

    std::map<NodeIPEndpoint, NodeID>& staticNodes() { return m_staticNodes; }
    h512s const minerList() { return m_minerList; }
    float txSpeed() { return m_txSpeed; }

private:
    void updateBootstrapnodes()
    {
        try
        {
            std::string json = asString(contents(boost::filesystem::path(m_bootstrapPath)));
            js::mValue val;
            js::read_string(json, val);
            js::mObject jsObj = val.get_obj();
            NodeIPEndpoint m_endpoint;
            /// obtain node related info
            if (jsObj.count("nodes"))
            {
                for (auto node : jsObj["nodes"].get_array())
                {
                    if (node.get_obj()["host"].get_str().empty() ||
                        node.get_obj()["p2pport"].get_str().empty())
                        continue;

                    string host;
                    uint16_t p2pport = -1;
                    host = node.get_obj()["host"].get_str();
                    p2pport = uint16_t(std::stoi(node.get_obj()["p2pport"].get_str()));

                    LOG(INFO) << "bootstrapnodes host :" << host << ",p2pport :" << p2pport;
                    m_endpoint.address = bi::address::from_string(host);
                    m_endpoint.tcpPort = p2pport;
                    m_endpoint.udpPort = p2pport;
                    if (m_endpoint.address.to_string() != "0.0.0.0")
                    {
                        m_staticNodes.insert(std::make_pair(NodeIPEndpoint(m_endpoint), NodeID()));
                    }
                }
            }
            /// obtain minerList
            if (jsObj.count("miners"))
            {
                m_minerList.clear();
                for (auto miner : jsObj["miners"].get_array())
                {
                    std::string miner_str = miner.get_str();
                    if (miner_str.substr(0, 2) != "0x")
                    {
                        miner_str = "0x" + miner_str;
                    }
                    m_minerList.push_back(jsToPublic(miner_str));
                    std::cout << "##### push back miner:" << miner_str << std::endl;
                }
            }
        }
        catch (...)
        {
            LOG(ERROR) << "p2p_main Parse bootstrapnodes.json Fail! ";
            exit(-1);
        }
        if (m_staticNodes.size())
            LOG(INFO) << "p2p_main Parse bootstrapnodes.json Suc! " << m_staticNodes.size();
        else
        {
            LOG(INFO) << "p2p_main Parse bootstrapnodes.json Empty! Please Add "
                         "Some Node Message!";
        }
    }

private:
    std::string m_clientVersion;
    std::string m_listenIp;
    uint16_t m_p2pPort;
    float m_txSpeed;
    std::string m_publicIp;
    std::string m_bootstrapPath;
    h512s m_minerList;
    std::map<NodeIPEndpoint, NodeID> m_staticNodes;
    dev::GROUP_ID m_groupSize;
};

static Params initCommandLine(int argc, const char* argv[])
{
    boost::program_options::options_description server_options("p2p module of FISCO-BCOS");
    server_options.add_options()("client_version,v", boost::program_options::value<std::string>(),
        "client version, default is 2.0")("public_ip,i",
        boost::program_options::value<std::string>(),
        "public ip address")("listen_ip,l", boost::program_options::value<std::string>(),
        "listen ip address")("p2p_port,p", boost::program_options::value<uint16_t>(), "p2p port")(
        "bootstrap,b", boost::program_options::value<std::string>(), "path of bootstrapnodes.json")(
        "txSpeed,t", boost::program_options::value<float>(), "transaction generate speed")(
        "groupSize,n", boost::program_options::value<int>(), "group size")(
        "help,h", "help of p2p module of FISCO-BCOS");

    boost::program_options::variables_map vm;
    try
    {
        boost::program_options::store(
            boost::program_options::parse_command_line(argc, argv, server_options), vm);
    }
    catch (...)
    {
        std::cout << "invalid input" << std::endl;
        exit(0);
    }
    /// help information
    if (vm.count("help") || vm.count("h"))
    {
        std::cout << server_options << std::endl;
        exit(0);
    }
    Params m_params(vm, server_options);
    return m_params;
}