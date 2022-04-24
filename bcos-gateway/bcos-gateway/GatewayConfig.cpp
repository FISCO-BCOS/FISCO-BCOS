/** @file GatewayConfig.cpp
 *  @author octopus
 *  @date 2021-05-19
 */

#include <bcos-gateway/GatewayConfig.h>
#include <bcos-utilities/FileUtility.h>
#include <json/json.h>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace gateway;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;

bool GatewayConfig::isValidPort(int port)
{
    if (port <= 1024 || port > 65535)
        return false;
    return true;
}

void GatewayConfig::hostAndPort2Endpoint(const std::string& _host, NodeIPEndpoint& _endpoint)
{
    std::string ip;
    uint16_t port;

    std::vector<std::string> s;
    boost::split(s, _host, boost::is_any_of("]"), boost::token_compress_on);
    if (s.size() == 2)
    {  // ipv6
        ip = s[0].data() + 1;
        port = boost::lexical_cast<int>(s[1].data() + 1);
    }
    else if (s.size() == 1)
    {  // ipv4
        std::vector<std::string> v;
        boost::split(v, _host, boost::is_any_of(":"), boost::token_compress_on);
        ip = v[0];
        port = boost::lexical_cast<int>(v[1]);
    }
    else
    {
        GATEWAY_CONFIG_LOG(ERROR) << LOG_DESC("not valid host value") << LOG_KV("host", _host);
        BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                  "GatewayConfig: the host is invalid, host=" + _host));
    }

    if (!isValidPort(port))
    {
        GATEWAY_CONFIG_LOG(ERROR) << LOG_DESC("the port is not valid") << LOG_KV("port", port);
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment(
                "GatewayConfig: the port is invalid, port=" + std::to_string(port)));
    }

    boost::system::error_code ec;
    boost::asio::ip::address ip_address = boost::asio::ip::make_address(ip, ec);
    if (ec.value() != 0)
    {
        GATEWAY_CONFIG_LOG(ERROR) << LOG_DESC("the host is invalid, make_address error")
                                  << LOG_KV("host", _host);
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment(
                "GatewayConfig: the host is invalid make_address error, host=" + _host));
    }

    _endpoint = NodeIPEndpoint{ip_address, port};
}

void GatewayConfig::parseConnectedJson(
    const std::string& _json, std::set<NodeIPEndpoint>& _nodeIPEndpointSet)
{
    /*
    {"nodes":["127.0.0.1:30355","127.0.0.1:30356"}]}
    */
    Json::Value root;
    Json::Reader jsonReader;

    try
    {
        if (!jsonReader.parse(_json, root))
        {
            GATEWAY_CONFIG_LOG(ERROR)
                << "unable to parse connected nodes json" << LOG_KV("json:", _json);
            BOOST_THROW_EXCEPTION(
                InvalidParameter() << errinfo_comment("GatewayConfig: unable to parse p2p "
                                                      "connected nodes json"));
        }

        Json::Value jNodes = root["nodes"];
        if (jNodes.isArray())
        {
            unsigned int jNodesSize = jNodes.size();
            for (unsigned int i = 0; i < jNodesSize; i++)
            {
                std::string host = jNodes[i].asString();

                NodeIPEndpoint endpoint;
                hostAndPort2Endpoint(host, endpoint);
                _nodeIPEndpointSet.insert(endpoint);

                GATEWAY_CONFIG_LOG(INFO)
                    << LOG_DESC("add one connected node") << LOG_KV("host", host);
            }
        }
    }
    catch (const std::exception& e)
    {
        GATEWAY_CONFIG_LOG(ERROR) << LOG_KV(
            "parseConnectedJson error: ", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(e);
    }
}

/**
 * @brief: loads configuration items from the config.ini
 * @param _configPath: config.ini path
 * @return void
 */
void GatewayConfig::initConfig(std::string const& _configPath, bool _uuidRequired)
{
    try
    {
        boost::property_tree::ptree pt;
        boost::property_tree::ini_parser::read_ini(_configPath, pt);
        m_wsConfig = std::make_shared<boostssl::ws::WsConfig>();

        initP2PConfig(pt, _uuidRequired);
        initWsConfig(pt);
    }
    catch (const std::exception& e)
    {
        boost::filesystem::path full_path(boost::filesystem::current_path());

        GATEWAY_CONFIG_LOG(ERROR) << LOG_KV("configPath", _configPath)
                                  << LOG_KV("currentPath", full_path.string())
                                  << LOG_KV("initConfig error: ", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment("initConfig: currentPath:" + full_path.string() +
                                                  " ,error:" + boost::diagnostic_information(e)));
    }

    GATEWAY_CONFIG_LOG(INFO) << LOG_DESC("initConfig ok!") << LOG_KV("configPath", _configPath)
                             << LOG_KV("listenIP", m_wsConfig->listenIP())
                             << LOG_KV("listenPort", m_wsConfig->listenPort())
                             << LOG_KV("smSSL", m_wsConfig->smSSL());
}

void GatewayConfig::initWsConfig(const boost::property_tree::ptree& _pt)
{
    // Mixed = server + client
    m_wsConfig->setModel(boostssl::ws::WsModel::Mixed);
    m_wsConfig->setModuleName("GATEWAY");
    m_wsConfig->setThreadPoolSize(m_threadPoolSize);
    m_wsConfig->setDisableSsl(false);

    // init contextConfig
    auto contextConfig = std::make_shared<boostssl::context::ContextConfig>();
    if (m_wsConfig->smSSL())
    {
        contextConfig->initSMCertConfig(_pt);
    }
    else
    {
        contextConfig->initCertConfig(_pt);
    }

    m_wsConfig->setContextConfig(contextConfig);
}

/// loads p2p configuration items from the configuration file
void GatewayConfig::initP2PConfig(const boost::property_tree::ptree& _pt, bool _uuidRequired)
{
    /*
    [p2p]
      ; uuid
      uuid =
      ; ssl or sm ssl
      sm_ssl=true
      listen_ip=0.0.0.0
      listen_port=30300
      nodes_path=./
      nodes_file=nodes.json
    */
    m_uuid = _pt.get<std::string>("p2p.uuid", "");
    if (_uuidRequired && m_uuid.size() == 0)
    {
        BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                  "initP2PConfig: invalid uuid! Must be non-empty!"));
    }
    bool smSSL = _pt.get<bool>("p2p.sm_ssl", false);
    std::string listenIP = _pt.get<std::string>("p2p.listen_ip", "0.0.0.0");
    int listenPort = _pt.get<int>("p2p.listen_port", 30300);
    if (!isValidPort(listenPort))
    {
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment(
                "initP2PConfig: invalid listen port, port=" + std::to_string(listenPort)));
    }

    // not set the nodePath, load from the config
    if (m_nodePath.size() == 0)
    {
        m_nodePath = _pt.get<std::string>("p2p.nodes_path", "./");
    }

    m_nodeFileName = _pt.get<std::string>("p2p.nodes_file", "nodes.json");

    m_wsConfig->setSmSSL(smSSL);
    m_wsConfig->setListenIP(listenIP);
    m_wsConfig->setListenPort((uint16_t)listenPort);

    GATEWAY_CONFIG_LOG(INFO) << LOG_DESC("initP2PConfig ok!") << LOG_KV("listenIP", listenIP)
                             << LOG_KV("listenPort", listenPort) << LOG_KV("smSSL", smSSL)
                             << LOG_KV("nodePath", m_nodePath)
                             << LOG_KV("nodeFileName", m_nodeFileName);
}

// load p2p connected peers
void GatewayConfig::loadP2pConnectedNodes()
{
    std::string nodeFilePath = m_nodePath + "/" + m_nodeFileName;
    // load p2p connected nodes
    std::set<NodeIPEndpoint> nodes;
    auto jsonContent = readContentsToString(boost::filesystem::path(nodeFilePath));
    if (!jsonContent || jsonContent->empty())
    {
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment(
                "initP2PConfig: unable to read nodes json file, path=" + nodeFilePath));
    }

    parseConnectedJson(*jsonContent.get(), nodes);
    auto nodesPtr = std::make_shared<std::set<NodeIPEndpoint>>();
    for (auto node : nodes)
    {
        nodesPtr->insert(node);
    }
    m_wsConfig->setConnectedPeers(nodesPtr);

    GATEWAY_CONFIG_LOG(INFO) << LOG_DESC("loadP2pConnectedNodes ok!")
                             << LOG_KV("nodePath", m_nodePath)
                             << LOG_KV("nodeFileName", m_nodeFileName)
                             << LOG_KV("nodes", nodes.size());
}