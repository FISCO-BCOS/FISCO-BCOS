/** @file GatewayConfig.h
 *  @author octopus
 *  @date 2021-05-19
 */

#pragma once
#include <bcos-boostssl/context/ContextConfig.h>
#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-gateway/Common.h>
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

    GatewayConfig() : m_wsConfig(std::make_shared<boostssl::ws::WsConfig>()) {}
    ~GatewayConfig() = default;

public:
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
    void parseConnectedJson(
        const std::string& _json, std::set<boostssl::NodeIPEndpoint>& _nodeIPEndpointSet);

    // loads ca configuration items from the configuration file
    void initCertConfig(const boost::property_tree::ptree& _pt);
    // loads sm ca configuration items from the configuration file
    void initSMCertConfig(const boost::property_tree::ptree& _pt);

    // loads p2p configuration items from the configuration file
    void initP2PConfig(const boost::property_tree::ptree& _pt, bool _uuidRequired);
    void initWsConfig(const boost::property_tree::ptree& _pt);

    // load p2p connected peers
    void loadP2pConnectedNodes();

    uint32_t threadPoolSize() { return m_threadPoolSize; }

    std::string const& uuid() const { return m_uuid; }
    void setUUID(std::string const& _uuid) { m_uuid = _uuid; }

    std::shared_ptr<boostssl::ws::WsConfig> wsConfig() { return m_wsConfig; }
    void setWsConfig(std::shared_ptr<boostssl::ws::WsConfig> _wsConfig) { m_wsConfig = _wsConfig; }

    void checkFileExist(const std::string& _path);

    boostssl::context::ContextConfig::CertConfig certConfig() const { return m_certConfig; }
    boostssl::context::ContextConfig::SMCertConfig smCertConfig() const { return m_smCertConfig; }

private:
    std::shared_ptr<boostssl::ws::WsConfig> m_wsConfig;

    std::string m_uuid;
    // threadPool size
    uint32_t m_threadPoolSize{16};

    std::string m_certPath;
    std::string m_nodePath;
    std::string m_nodeFileName;

    boostssl::context::ContextConfig::CertConfig m_certConfig;
    boostssl::context::ContextConfig::SMCertConfig m_smCertConfig;
};

}  // namespace gateway
}  // namespace bcos