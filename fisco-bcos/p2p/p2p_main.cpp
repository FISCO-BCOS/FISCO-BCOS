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
#include <libp2p/Network.h>
#include <libp2p/P2pFactory.h>
#include <libp2p/Service.h>
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
        m_bootstrapPath(getDataDir().string() + "/bootstrapnodes.json"),
        m_sendMsgType(0)
    {
        updateBootstrapnodes();
    }

    Params(std::string const& _clientVersion, std::string const& _listenIp,
        uint16_t const& _p2pPort, std::string const& _publicIp, std::string const& _bootstrapPath,
        uint16_t _sendMsgType)
      : m_clientVersion(_clientVersion),
        m_listenIp(_listenIp),
        m_p2pPort(_p2pPort),
        m_publicIp(_publicIp),
        m_bootstrapPath(_bootstrapPath),
        m_sendMsgType(_sendMsgType)
    {
        updateBootstrapnodes();
    }

    Params(boost::program_options::variables_map const& vm,
        boost::program_options::options_description const& option)
      : m_clientVersion("2.0"),
        m_listenIp("127.0.0.1"),
        m_p2pPort(30303),
        m_publicIp("127.0.0.1"),
        m_bootstrapPath(getDataDir().string() + "/bootstrapnodes.json"),
        m_sendMsgType(0)
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
        if (vm.count("send_msg_type") || vm.count("s"))
            m_sendMsgType = vm["send_msg_type"].as<uint16_t>();
    }
    /// --- set interfaces ---
    void setClientVersion(std::string const& _clientVersion) { m_clientVersion = _clientVersion; }
    void setListenIp(std::string const& _listenIp) { m_listenIp = _listenIp; }
    void setP2pPort(uint16_t const& _p2pPort) { m_p2pPort = _p2pPort; }
    void setPublicIp(std::string const& _publicIp) { m_publicIp = _publicIp; }
    void setSendMsgType(uint16_t const& _sendMsgType) { m_sendMsgType = _sendMsgType; }
    /// --- get interfaces ---
    const std::string& clientVersion() const { return m_clientVersion; }
    const std::string& listenIp() const { return m_listenIp; }
    const uint16_t& p2pPort() const { return m_p2pPort; }
    const std::string& publicIp() const { return m_publicIp; }
    const uint16_t& sendMsgType() const { return m_sendMsgType; }

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
            NodeIPEndpoint m_endpoint;
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
    uint16_t m_sendMsgType;
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
        "path of bootstrapnodes.json")("help,h", "help of p2p module of FISCO-BCOS")(
        "send_msg_type,s", boost::program_options::value<uint16_t>(), "sendMessageType");

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

static void sendMsg(Params const& _params, std::shared_ptr<Service> _service)
{
    switch (_params.sendMsgType())
    {
    case 0:
    {
        _service->registerHandlerByProtoclID(
            1, [](P2PException e, std::shared_ptr<Session> s, Message::Ptr msg) {
                LOG(INFO) << "Receive message by protocolID:" << msg->protocolID()
                          << ", packetType:" << msg->packetType() << ", content:"
                          << std::string((const char*)msg->buffer()->data(), msg->buffer()->size());

                /// send response package
                if (msg->isRequestPacket())
                {
                    /// update protoclID
                    msg->setProtocolID(msg->getResponceProtocolID());
                    msg->setLength(Message::HEADER_LENGTH + msg->buffer()->size());
                    msg->Print("Send response package by protoclID");
                    std::shared_ptr<bytes> buf = std::make_shared<bytes>();
                    msg->encode(*buf);
                    s->send(buf);
                }
            });

        auto msg = std::make_shared<Message>();
        msg->setProtocolID(1);
        msg->setPacketType(2);
        std::shared_ptr<bytes> buffer(new bytes());
        std::string s = "hello world!";
        msg->setBuffer(buffer);
        buffer->assign(s.begin(), s.end());
        Options options = {0};
        options.timeout = 3000;  ///< ms
        _service->asyncSendMessageByNodeID(
            h512("46787132f4d6285bfe108427658baf2b48de169bdb745e01610efd7930043dcc414dc6f6ddc3da6fc"
                 "491cc1c15f46e621ea7304a9b5f0b3fb85ba20a6b1c0fc1"),
            msg,
            [](P2PException e, Message::Ptr msg) {
                if (e.errorCode() == 0)
                {
                    LOG(INFO) << "Callback in response package by seq! seq=" << msg->seq();
                }
                else
                {
                    LOG(INFO) << "Callback in response package error, seq=" << msg->seq()
                              << ", msg:" << e.what();
                }
            },
            options);

        break;
    }
    case 1:
    {
        auto msg = std::make_shared<Message>();
        msg->setPacketType(2);
        std::shared_ptr<bytes> buffer(new bytes());
        std::string s = "hello world!";
        msg->setBuffer(buffer);
        buffer->assign(s.begin(), s.end());
        _service->sendMessageByNodeID(
            h512("46787132f4d6285bfe108427658baf2b48de169bdb745e01610efd7930043dcc414dc6f6ddc3da6fc"
                 "491cc1c15f46e621ea7304a9b5f0b3fb85ba20a6b1c0fc1"),
            msg);
        break;
    }
    case 2:
    {
        _service->registerHandlerByProtoclID(
            2, [](P2PException e, std::shared_ptr<Session> s, Message::Ptr msg) {
                LOG(INFO) << "Receive message by protocolID:" << msg->protocolID()
                          << ", packetType:" << msg->packetType() << ", content:"
                          << std::string((const char*)msg->buffer()->data(), msg->buffer()->size());

                /// send response package
                if (msg->isRequestPacket())
                {
                    /// update protoclID
                    msg->setProtocolID(msg->getResponceProtocolID());
                    msg->setLength(Message::HEADER_LENGTH + msg->buffer()->size());
                    msg->Print("Send response package by protoclID");
                    std::shared_ptr<bytes> buf = std::make_shared<bytes>();
                    msg->encode(*buf);
                    s->send(buf);
                }
            });

        auto msg = std::make_shared<Message>();
        msg->setProtocolID(2);
        msg->setPacketType(1);
        std::shared_ptr<bytes> buffer(new bytes());
        std::string s = "hello world!";
        msg->setBuffer(buffer);
        buffer->assign(s.begin(), s.end());
        _service->asyncSendMessageByNodeID(
            h512("46787132f4d6285bfe108427658baf2b48de169bdb745e01610efd7930043dcc414dc6f6ddc3da6fc"
                 "491cc1c15f46e621ea7304a9b5f0b3fb85ba20a6b1c0fc1"),
            msg);
        break;
    }
    default:
    {
        LOG(ERROR) << "Invalid Message Type, now type only is 0,1,2.";
        break;
    }
    }
}

static void InitNetwork(Params& m_params)
{
    std::shared_ptr<AsioInterface> m_asioInterface = std::make_shared<AsioInterface>();
    std::shared_ptr<NetworkConfig> m_netConfig = m_params.creatNetworkConfig();
    /// create m_socketFactory
    std::shared_ptr<SocketFactory> m_socketFactory = std::make_shared<SocketFactory>();
    /// create m_sessionFactory
    std::shared_ptr<SessionFactory> m_sessionFactory = std::make_shared<SessionFactory>();
    std::shared_ptr<Host> m_host =
        std::make_shared<Host>(m_params.clientVersion(), CertificateServer::GetInstance().keypair(),
            *m_netConfig.get(), m_asioInterface, m_socketFactory, m_sessionFactory);
    m_host->setStaticNodes(m_params.staticNodes());
    auto p2pMsgHandler = std::make_shared<P2PMsgHandler>();
    auto service = std::make_shared<Service>(m_host, p2pMsgHandler);
    /// begin working
    m_host->start();
    while (true)
    {
        sendMsg(m_params, service);
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
