/** @file GatewayConfig.h
 *  @author octopus
 *  @date 2021-05-19
 */

#pragma once

#include "bcos-utilities/ObjectCounter.h"
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-gateway/Common.h>
#include <bcos-gateway/libnetwork/Common.h>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <array>

namespace bcos::gateway
{
class GatewayConfig : public bcos::ObjectCounter<GatewayConfig>
{
public:
    using Ptr = std::shared_ptr<GatewayConfig>;

    // cert for ssl connection
    struct CertConfig
    {
        std::string caCert;
        std::string nodeKey;
        std::string nodeCert;
        std::string multiCaPath;
    };

    // cert for sm ssl connection
    struct SMCertConfig
    {
        std::string caCert;
        std::string nodeCert;
        std::string nodeKey;
        std::string enNodeCert;
        std::string enNodeKey;
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
        bool enableOutRateLimit() const
        {
            if (totalOutgoingBwLimit > 0 || connOutgoingBwLimit > 0 || groupOutgoingBwLimit > 0)
            {
                return true;
            }

            if (!group2BwLimit.empty() || !ip2BwLimit.empty())
            {
                return true;
            }

            return false;
        }

        bool enableOutGroupRateLimit() const
        {
            return groupOutgoingBwLimit > 0 || !group2BwLimit.empty();
        }

        bool enableOutConnRateLimit() const
        {
            return connOutgoingBwLimit > 0 || !ip2BwLimit.empty();
        }

        bool enableInRateLimit() const
        {
            if (p2pBasicMsgQPS > 0 || p2pModuleMsgQPS > 0)
            {
                return true;
            }

            if (moduleMsg2QPSSize > 0)
            {
                return true;
            }

            return false;
        }


        bool enableInP2pBasicMsgLimit() const { return p2pBasicMsgQPS > 0; }

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

    void setCertPath(std::string const& _certPath) { m_certPath = _certPath; }
    void setNodePath(std::string const& _nodePath) { m_nodePath = _nodePath; }
    void setNodeFileName(const std::string& _nodeFileName) { m_nodeFileName = _nodeFileName; }
    void setConfigFile(const std::string& _configFile) { m_configFile = _configFile; }

    std::string const& certPath() const { return m_certPath; }
    std::string const& nodePath() const { return m_nodePath; }
    std::string const& nodeFileName() const { return m_nodeFileName; }
    std::string const& configFile() const { return m_configFile; }

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
    void checkFileExist(const std::string& _path);
    // load p2p connected peers
    void loadP2pConnectedNodes();

    std::string listenIP() const { return m_listenIP; }
    uint16_t listenPort() const { return m_listenPort; }
    uint32_t threadPoolSize() const { return m_threadPoolSize; }
    bool smSSL() const { return m_smSSL; }

    CertConfig certConfig() const { return m_certConfig; }
    SMCertConfig smCertConfig() const { return m_smCertConfig; }
    RateLimiterConfig rateLimiterConfig() const { return m_rateLimiterConfig; }
    RedisConfig redisConfig() const { return m_redisConfig; }

    const std::set<NodeIPEndpoint>& connectedNodes() const { return m_connectedNodes; }

    bool enableBlacklist() const
    {
        bcos::Guard l(x_certBlacklist);
        return m_enableBlacklist;
    }
    const std::set<std::string>& peerBlacklist() const
    {
        bcos::Guard l(x_certBlacklist);
        return m_certBlacklist;
    }
    bool enableWhitelist() const
    {
        bcos::Guard l(x_certWhitelist);
        return m_enableWhitelist;
    }
    const std::set<std::string>& peerWhitelist() const
    {
        bcos::Guard l(x_certWhitelist);
        return m_certWhitelist;
    }

    void loadPeerBlacklist();
    void loadPeerWhitelist();

    std::string const& uuid() const { return m_uuid; }
    void setUUID(std::string const& _uuid) { m_uuid = _uuid; }

    bool readonly() const { return m_readonly; }
    void setEnableRIPProtocol(bool _enableRIPProtocol) { m_enableRIPProtocol = _enableRIPProtocol; }
    bool enableRIPProtocol() const { return m_enableRIPProtocol; }

    void setEnableCompress(bool _enableCompress) { m_enableCompress = _enableCompress; }
    bool enableCompress() const { return m_enableCompress; }

    uint32_t allowMaxMsgSize() const { return m_allowMaxMsgSize; }
    void setAllowMaxMsgSize(uint32_t _allowMaxMsgSize) { m_allowMaxMsgSize = _allowMaxMsgSize; }

    uint32_t sessionRecvBufferSize() const { return m_sessionRecvBufferSize; }
    void setSessionRecvBufferSize(uint32_t _sessionRecvBufferSize)
    {
        m_sessionRecvBufferSize = _sessionRecvBufferSize;
    }

    uint32_t maxReadDataSize() const { return m_maxReadDataSize; }
    void setMaxReadDataSize(uint32_t _maxReadDataSize) { m_maxReadDataSize = _maxReadDataSize; }

    uint32_t maxSendDataSize() const { return m_maxSendDataSize; }
    void setMaxSendDataSize(uint32_t _maxSendDataSize) { m_maxSendDataSize = _maxSendDataSize; }

    uint32_t maxMsgCountSendOneTime() const { return m_maxSendMsgCount; }
    void setMaxSendMsgCount(uint32_t _maxSendMsgCount) { m_maxSendMsgCount = _maxSendMsgCount; }
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

private:
    // The maximum size of message that is allowed to send or receive
    uint32_t m_allowMaxMsgSize = 32 * 1024 * 1024;
    // p2p session read buffer size, default: 128k
    uint32_t m_sessionRecvBufferSize{128 * 1024};
    uint32_t m_maxReadDataSize = 40 * 1024;
    uint32_t m_maxSendDataSize = 1024 * 1024;
    uint32_t m_maxSendMsgCount = 10;
    //
    std::string m_uuid;
    // if SM SSL connection or not
    bool m_smSSL;
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

}  // namespace bcos
