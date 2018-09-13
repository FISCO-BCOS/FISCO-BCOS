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
 * @file: p2p_main.cpp
 * @author: yujiechen
 * @date 2018-09-10
 */
#include <json_spirit/JsonSpiritHeaders.h>
#include <libdevcore/easylog.h>
#include <libp2p/CertificateServer.h>
#include <libp2p/Host.h>
#include <libp2p/HostCapability.h>
#include <libp2p/Network.h>
#include <boost/program_options.hpp>
#include <memory>

INITIALIZE_EASYLOGGINGPP
using namespace dev;
using namespace dev::p2p;
namespace js = json_spirit;
class Params
{
public:
    Params()
      : m_clientVersion("2.0"),
        m_listenIp("127.0.0.1"),
        m_p2pPort(30303),
        m_publicIp("127.0.0.1"),
        m_bootstrapPath(getDataDir().string() + "/bootstrapnodes.json")
    {
        updateBootstrapnodes();
    }

    Params(std::string const& _clientVersion, std::string const& _listenIp,
        uint16_t const& _p2pPort, std::string const& _publicIp, std::string const& _bootstrapPath)
      : m_clientVersion(_clientVersion),
        m_listenIp(_listenIp),
        m_p2pPort(_p2pPort),
        m_publicIp(_publicIp),
        m_bootstrapPath(_bootstrapPath)
    {
        updateBootstrapnodes();
    }

    Params(boost::program_options::variables_map const& vm,
        boost::program_options::options_description const& option)
      : m_clientVersion("2.0"),
        m_listenIp("127.0.0.1"),
        m_p2pPort(30303),
        m_publicIp("127.0.0.1"),
        m_bootstrapPath(getDataDir().string() + "/bootstrapnodes.json")
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

    /// --- init NetworkConfig ----
    std::shared_ptr<NetworkConfig> creatNetworkConfig()
    {
        std::shared_ptr<NetworkConfig> network_config =
            std::make_shared<NetworkConfig>(m_publicIp, m_listenIp, m_p2pPort);
        return network_config;
    }

    std::map<NodeIPEndpoint, NodeID>& staticNodes() { return m_staticNodes; }

private:
    void updateBootstrapnodes()
    {
        try
        {
            std::string json = asString(contents(boost::filesystem::path(m_bootstrapPath)));
            js::mValue val;
            js::read_string(json, val);
            js::mObject jsObj = val.get_obj();
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
                    // NodeIPEndpoint nodeendpoint=NodeIPEndpoint(bi::address::from_string(host),
                    // p2pport,p2pport);
                    NodeIPEndpoint nodeEndpoint(host, p2pport, p2pport);
                    if (nodeEndpoint.address.to_string() != "0.0.0.0")
                        m_staticNodes[nodeEndpoint] = Public(0);
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
    std::string m_publicIp;
    std::string m_bootstrapPath;
    std::map<NodeIPEndpoint, NodeID> m_staticNodes;
};

static Params initCommandLine(int argc, const char* argv[])
{
    boost::program_options::options_description server_options("p2p module of FISCO-BCOS");
    server_options.add_options()("client_version,v", boost::program_options::value<std::string>(),
        "client version, default is 2.0")(
        "public_ip,i", boost::program_options::value<std::string>(), "public ip address")(
        "listen_ip,l", boost::program_options::value<std::string>(), "listen ip address")(
        "p2p_port,p", boost::program_options::value<uint16_t>(), "p2p port")("bootstrap, b",
        boost::program_options::value<std::string>(),
        "path of bootstrapnodes.json")("help,h", "help of p2p module of FISCO-BCOS");

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

static void InitNetwork(Params& m_params)
{
    std::shared_ptr<NetworkConfig> m_netConfig = m_params.creatNetworkConfig();
    std::shared_ptr<Host> m_host = std::make_shared<Host>(
        m_params.clientVersion(), CertificateServer::GetInstance().keypair(), *m_netConfig.get());
    m_host->setStaticNodes(m_params.staticNodes());
    /// begin working
    m_host->start();
    while (true)
    {
        this_thread::sleep_for(chrono::milliseconds(1000));
    }
}

int main(int argc, const char* argv[])
{
    /// init params
    Params m_params = initCommandLine(argc, argv);
    /// init p2p
    InitNetwork(m_params);
}
