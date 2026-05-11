/** @file GatewayConfig.h
 *  @author octopus
 *  @date 2021-05-19
 */

#pragma once

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-gateway/Common.h"
#include "bcos-utilities/ObjectCounter.h"
#include <boost/algorithm/string.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <array>

namespace bcos::gateway
{
class GatewayConfig : public bcos::ObjectCounter<GatewayConfig>
{
public:
    using Ptr = std::shared_ptr<GatewayConfig>;
    GatewayConfig();

    // cert for ssl connection
    struct CertConfig
    {
        std::optional<std::string> caCert;
        std::optional<std::string> nodeKey;
        std::optional<std::string> nodeCert;
        std::string multiCaPath;
    };

    // cert for sm ssl connection
    struct SMCertConfig
    {
        std::optional<std::string> caCert;
        std::optional<std::string> nodeCert;
        std::optional<std::string> nodeKey;
        std::optional<std::string> enNodeCert;
        std::optional<std::string> enNodeKey;
        std::string multiCaPath;
    };

    // config for redis
    struct RedisConfig
    {
        // redis server ip
        std::string host;
        // redis server port
        uint16_t port;
        // redis request timeout
        int32_t timeout = -1;
        // redis connection pool size, default 16
        int32_t connectionPoolSize = 16;
        // redis password, default empty
        std::string password;
        // redis db, default 0th
        int db = 0;
    };

    // config for rate limit
    struct RateLimiterConfig
    {
        bool enable = false;
        // time window for rate limiter
        int32_t timeWindowSec = 1;
        // allow outgoing msg exceed max permit size
        bool allowExceedMaxPermitSize = false;

        bool enableConnectDebugInfo = false;

        // stat reporter interval, unit: ms
        int32_t statInterval = 60000;

        // distributed ratelimit switch
        bool enableDistributedRatelimit = false;
        // distributed ratelimit local cache switch
        bool enableDistributedRateLimitCache = true;
        // distributed ratelimit local cache percent
        int32_t distributedRateLimitCachePercent = 20;

        //-------------- output bandwidth ratelimit begin------------------
        // total outgoing bandwidth limit
        int64_t totalOutgoingBwLimit = -1;

        // per connection outgoing bandwidth limit
        int64_t connOutgoingBwLimit = -1;
        // specify IP bandwidth limiting
        std::unordered_map<std::string, int64_t> ip2BwLimit;

        // per connection outgoing bandwidth limit
        int64_t groupOutgoingBwLimit = -1;
        // specify group bandwidth limiting
        std::unordered_map<std::string, int64_t> group2BwLimit;

        // the message of modules that do not limit bandwidth
        std::set<uint16_t> modulesWithoutLimit;
        //-------------- output bandwidth ratelimit end-------------------

        //-------------- incoming qps ratelimit begin---------------------

        int32_t p2pBasicMsgQPS = -1;
        std::set<uint16_t> p2pBasicMsgTypes;
        int32_t p2pModuleMsgQPS = -1;
        int32_t moduleMsg2QPSSize = 0;
        std::array<int32_t, std::numeric_limits<uint16_t>::max()> moduleMsg2QPS{};

        //-------------- incoming qps ratelimit end-----------------------

        // whether any configuration takes effect
        bool enableOutRateLimit() const;

        bool enableOutGroupRateLimit() const;

        bool enableOutConnRateLimit() const;

        bool enableInRateLimit() const;
    bool enableInP2pBasicMsgLimit() const;

        bool enableInP2pModuleMsgLimit(uint16_t _moduleID) const
        {
            if ((p2pModuleMsgQPS <= 0) && (moduleMsg2QPSSize <= 0))
            {
                return false;
            }

            return p2pModuleMsgQPS > 0 || (moduleMsg2QPS.at(_moduleID) != 0);
        }
    };

    /**
     * @brief: loads configuration items from the config.ini
     * @param _configPath: config.ini path
     * @return void
     */
    void initConfig(std::string const& _configPath, bool _uuidRequired = false);

    void setCertPath(std::string const& _certPath);
    void setNodePath(std::string const& _nodePath);
    void setNodeFileName(const std::string& _nodeFileName);
    void setConfigFile(const std::string& _configFile);

    std::string const& certPath() const;
    std::string const& nodePath() const;
    std::string const& nodeFileName() const;
    std::string const& configFile() const;

    // check if the port valid
    bool isValidPort(int port);
    // check if the ip valid
    bool isValidIP(const std::string& _ip);
    // MB to bit
    int64_t doubleMBToBit(double _d);
    bool isIPAddress(const std::string& _input);
    bool isHostname(const std::string& _input);
    void hostAndPort2Endpoint(const std::string& _host, NodeIPEndpoint& _endpoint);
    void parseConnectedJson(const std::string& _json, std::set<NodeIPEndpoint>& _nodeIPEndpointSet);
    // loads p2p configuration items from the configuration file
    void initP2PConfig(const boost::property_tree::ptree& _pt, bool _uuidRequired);
    // loads ca configuration items from the configuration file
    void initCertConfig(const boost::property_tree::ptree& _pt);
    // loads sm ca configuration items from the configuration file
    void initSMCertConfig(const boost::property_tree::ptree& _pt);
    // loads ratelimit config
    void initFlowControlConfig(const boost::property_tree::ptree& _pt);
    // loads redis config
    void initRedisConfig(const boost::property_tree::ptree& _pt);
    // loads peer blacklist config
    void initPeerBlacklistConfig(const boost::property_tree::ptree& _pt);
    // loads peer whitelist config
    void initPeerWhitelistConfig(const boost::property_tree::ptree& _pt);
    // check if file exist, exception will be throw if the file not exist
    template <typename R>
    R checkFileExist(const std::string& _path);

    // load p2p connected peers
    void loadP2pConnectedNodes();

    std::string listenIP() const;
    uint16_t listenPort() const;
    uint32_t threadPoolSize() const;
    bool smSSL() const;
    uint8_t sslClientMode() const;
    uint8_t sslServerMode() const;

    CertConfig certConfig() const;
    SMCertConfig smCertConfig() const;
    RateLimiterConfig rateLimiterConfig() const;
    RedisConfig redisConfig() const;

    const std::set<NodeIPEndpoint>& connectedNodes() const;

    bool enableBlacklist() const;
    const std::set<std::string>& peerBlacklist() const;
    bool enableWhitelist() const;
    const std::set<std::string>& peerWhitelist() const;

    void loadPeerBlacklist();
    void loadPeerWhitelist();

    std::string const& uuid() const;
    void setUUID(std::string const& _uuid);

    bool readonly() const;
    void setEnableRIPProtocol(bool _enableRIPProtocol);
    bool enableRIPProtocol() const;

    void setEnableCompress(bool _enableCompress);
    bool enableCompress() const;

    uint32_t allowMaxMsgSize() const;
    void setAllowMaxMsgSize(uint32_t _allowMaxMsgSize);

    uint32_t sessionRecvBufferSize() const;
    void setSessionRecvBufferSize(uint32_t _sessionRecvBufferSize);

    uint32_t maxReadDataSize() const;
    void setMaxReadDataSize(uint32_t _maxReadDataSize);

    uint32_t maxSendDataSize() const;
    void setMaxSendDataSize(uint32_t _maxSendDataSize);

    uint32_t maxMsgCountSendOneTime() const;
    void setMaxSendMsgCount(uint32_t _maxSendMsgCount);
    // NodeIDType:
    // h512(true == m_smSSL)
    // h2048(false == m_smSSL)
    template <typename NodeIDType>
    bool isNodeIDOk(const std::string& _nodeID)
    {
        try
        {
            const std::size_t nodeIDLength = NodeIDType::SIZE * 2;
            const std::size_t nodeIDWithPrefixLength = nodeIDLength + 2;

            // check node id length
            if (_nodeID.length() != nodeIDWithPrefixLength && _nodeID.length() != nodeIDLength)
            {
                return false;
            }
            // if the length of the node id is nodeIDWithPrefixLength, must be start with 0x
            if (_nodeID.length() == nodeIDWithPrefixLength && _nodeID.compare(0, 2, "0x") != 0)
            {
                return false;
            }
            NodeIDType nodeID = NodeIDType(_nodeID);
            return NodeIDType() != nodeID;
        }
        catch (...)
        {
            return false;
        }
    }

    bool enableSSLVerify() const;

    bcos::crypto::Hash::Ptr const& hashImpl() const;
    std::string calculateShortNodeID(std::string const& rawNodeID) const
    {
        bcos::crypto::HashType p2pIDHash = m_hashImpl->hash(
            bcos::bytesConstRef((bcos::byte const*)rawNodeID.data(), rawNodeID.size()));
        // the p2pID
        return std::string(p2pIDHash.begin(), p2pIDHash.end());
    }

private:
    bcos::crypto::Hash::Ptr m_hashImpl;
    // The maximum size of message that is allowed to send or receive
    uint32_t m_allowMaxMsgSize = MAX_MESSAGE_LENGTH;
    // p2p session read buffer size, default: 128k
    uint32_t m_sessionRecvBufferSize{128 * 1024};
    uint32_t m_maxReadDataSize = 40 * 1024;
    uint32_t m_maxSendDataSize = 1024 * 1024;
    uint32_t m_maxSendMsgCount = 10;
    //
    std::string m_uuid;
    // if SM SSL connection or not
    bool m_smSSL;
    // default verify mode for server mode
    uint8_t m_ssl_server_mode =
        boost::asio::ssl::context_base::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert;
    // default verify mode for client mode
    uint8_t m_ssl_client_mode =
        boost::asio::ssl::context_base::verify_peer | boost::asio::ssl::verify_fail_if_no_peer_cert;

    // enable ssl verify or not, default is true
    bool m_enableSSLVerify = true;
    // p2p network listen IP
    std::string m_listenIP;
    // p2p network listen Port
    uint16_t m_listenPort;
    // threadPool size
    uint32_t m_threadPoolSize{8};
    // p2p connected nodes host list
    std::set<NodeIPEndpoint> m_connectedNodes;
    // peer black list
    mutable bcos::Mutex x_certBlacklist;
    bool m_enableBlacklist{false};
    std::set<std::string> m_certBlacklist;
    // peer white list
    mutable bcos::Mutex x_certWhitelist;
    bool m_enableWhitelist{false};
    // enable rip protocol
    bool m_enableRIPProtocol{true};
    // enable compress
    bool m_enableCompress{true};
    std::set<std::string> m_certWhitelist;
    // cert config for ssl connection
    CertConfig m_certConfig;
    SMCertConfig m_smCertConfig;

    RateLimiterConfig m_rateLimiterConfig;
    RedisConfig m_redisConfig;

    std::string m_certPath;
    std::string m_nodePath;
    std::string m_nodeFileName;
    std::string m_configFile;

    bool m_readonly = false;
};

}  // namespace bcos::gateway
