/** @file GatewayConfig.h
 *  @author octopus
 *  @date 2021-05-19
 */

#pragma once
#include <bcos-gateway/Common.h>
#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-boostssl/interfaces/NodeInfo.h>
#include <bcos-boostssl/websocket/WsConfig.h>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace bcos
{
namespace gateway
{
class GatewayConfig
{
public:
    using Ptr = std::shared_ptr<GatewayConfig>;

    GatewayConfig() = default;
    ~GatewayConfig() = default;

public:
    // cert for ssl connection
    struct CertConfig
    {
        std::string caCert;
        std::string nodeKey;
        std::string nodeCert;
    };

    // cert for sm ssl connection
    struct SMCertConfig
    {
        std::string caCert;
        std::string nodeCert;
        std::string nodeKey;
        std::string enNodeCert;
        std::string enNodeKey;
    };


    /**
     * @brief: loads configuration items from the config.ini
     * @param _configPath: config.ini path
     * @return void
     */
    void initConfig(std::string const& _configPath, bool _uuidRequired = false);

    void setCertPath(std::string const& _certPath) { m_certPath = _certPath; }
    void setNodePath(std::string const& _nodePath) { m_nodePath = _nodePath; }
    void setNodeFileName(const std::string& _nodeFileName) { m_nodeFileName = _nodeFileName; }

    std::string const& certPath() const { return m_certPath; }
    std::string const& nodePath() const { return m_nodePath; }
    std::string const& nodeFileName() const { return m_nodeFileName; }

    // check if the port valid
    bool isValidPort(int port);
    void hostAndPort2Endpoint(const std::string& _host, boostssl::NodeIPEndpoint& _endpoint);
    void parseConnectedJson(const std::string& _json, std::set<boostssl::NodeIPEndpoint>& _nodeIPEndpointSet);
    // loads p2p configuration items from the configuration file
    void initP2PConfig(const boost::property_tree::ptree& _pt, bool _uuidRequired);
    // loads ca configuration items from the configuration file
    void initCertConfig(const boost::property_tree::ptree& _pt);
    // loads sm ca configuration items from the configuration file
    void initSMCertConfig(const boost::property_tree::ptree& _pt);
    // check if file exist, exception will be throw if the file not exist
    void checkFileExist(const std::string& _path);
    // load p2p connected peers
    void loadP2pConnectedNodes();
    boostssl::ws::EndPointsPtr obtainPeersForWsService(const std::set<boostssl::NodeIPEndpoint>& _nodeIPEndpointSet);

    std::string listenIP() const { return m_listenIP; }
    uint16_t listenPort() const { return m_listenPort; }
    uint32_t threadPoolSize() { return m_threadPoolSize; }
    bool smSSL() const { return m_smSSL; }

    boostssl::context::ContextConfig::CertConfig certConfig() const { return m_certConfig; }
    boostssl::context::ContextConfig::SMCertConfig smCertConfig() const { return m_smCertConfig; }
    const std::set<boostssl::NodeIPEndpoint>& connectedNodes() const { return m_connectedNodes; }

    std::string const& uuid() const { return m_uuid; }
    void setUUID(std::string const& _uuid) { m_uuid = _uuid; }

private:
    std::string m_uuid;
    // if SM SSL connection or not
    bool m_smSSL;
    // p2p network listen IP
    std::string m_listenIP;
    // p2p network listen Port
    uint16_t m_listenPort;
    // threadPool size
    uint32_t m_threadPoolSize{16};
    // p2p connected nodes host list
    std::set<boostssl::NodeIPEndpoint> m_connectedNodes;
    // cert config for ssl connection
    boostssl::context::ContextConfig::CertConfig m_certConfig;
    boostssl::context::ContextConfig::SMCertConfig m_smCertConfig;

    std::string m_certPath;
    std::string m_nodePath;
    std::string m_nodeFileName;
};

}  // namespace gateway
}  // namespace bcos