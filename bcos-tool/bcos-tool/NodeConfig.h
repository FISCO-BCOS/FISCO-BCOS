/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief configuration for the node
 * @file NodeConfig.h
 * @author: yujiechen
 * @date 2021-06-10
 */
#pragma once
#include "Exceptions.h"
#include "bcos-framework/consensus/ConsensusNodeInterface.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-tool/VersionConverter.h"
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/Common.h>
#include <bcos-framework/protocol/Protocol.h>
#include <util/tc_clientsocket.h>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <cstddef>
#include <optional>
#include <unordered_map>

#define NodeConfig_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("NodeConfig")
namespace bcos::tool
{
class NodeConfig
{
public:
    constexpr static ssize_t DEFAULT_CACHE_SIZE = 32 * 1024 * 1024;
    constexpr static ssize_t DEFAULT_MIN_CONSENSUS_TIME_MS = 3000;
    constexpr static ssize_t DEFAULT_MIN_LEASE_TTL_SECONDS = 3;
    constexpr static ssize_t DEFAULT_MAX_SEAL_TIME_MS = 600000;
    constexpr static ssize_t DEFAULT_PIPELINE_SIZE = 50;

    using Ptr = std::shared_ptr<NodeConfig>;
    NodeConfig() : m_ledgerConfig(std::make_shared<bcos::ledger::LedgerConfig>()) {}

    explicit NodeConfig(bcos::crypto::KeyFactory::Ptr _keyFactory);
    virtual ~NodeConfig() = default;

    virtual void loadConfig(std::string const& _configPath, bool _enforceMemberID = true,
        bool enforceChainConfig = false, bool enforceGroupId = true)
    {
        boost::property_tree::ptree iniConfig;
        boost::property_tree::read_ini(_configPath, iniConfig);
        loadConfig(iniConfig, _enforceMemberID, enforceChainConfig, enforceGroupId);
    }
    virtual void loadServiceConfig(boost::property_tree::ptree const& _pt);
    virtual void loadRpcServiceConfig(boost::property_tree::ptree const& _pt);
    virtual void loadGatewayServiceConfig(boost::property_tree::ptree const& _pt);

    virtual void loadWithoutTarsFrameworkConfig(boost::property_tree::ptree const& _pt);

    virtual void loadNodeServiceConfig(
        std::string const& _nodeID, boost::property_tree::ptree const& _pt, bool _require = false);

    virtual void loadTarsProxyConfig(const std::string& _tarsProxyConf);

    virtual void loadServiceTarsProxyConfig(
        const std::string& _serviceSectionName, boost::property_tree::ptree const& _pt);

    virtual void loadGenesisConfig(std::string const& _genesisConfigPath)
    {
        boost::property_tree::ptree genesisConfig;
        boost::property_tree::read_ini(_genesisConfigPath, genesisConfig);
        loadGenesisConfig(genesisConfig);
    }

    virtual void loadConfigFromString(std::string const& _content)
    {
        boost::property_tree::ptree iniConfig;
        std::stringstream contentStream(_content);
        boost::property_tree::read_ini(contentStream, iniConfig);
        loadConfig(iniConfig);
    }

    virtual void loadGenesisConfigFromString(std::string const& _content)
    {
        boost::property_tree::ptree genesisConfig;
        std::stringstream contentStream(_content);
        boost::property_tree::read_ini(contentStream, genesisConfig);
        loadGenesisConfig(genesisConfig);
    }

    virtual void loadConfig(boost::property_tree::ptree const& _pt, bool _enforceMemberID = true,
        bool _enforceChainConfig = false, bool _enforceGroupId = true);
    virtual void loadGenesisConfig(boost::property_tree::ptree const& _genesisConfig);

    // the txpool configurations
    size_t txpoolLimit() const { return m_txpoolLimit; }
    size_t notifyWorkerNum() const { return m_notifyWorkerNum; }
    size_t verifierWorkerNum() const { return m_verifierWorkerNum; }
    int64_t txsExpirationTime() const { return m_txsExpirationTime; }
    bool checkBlockLimit() const { return m_checkBlockLimit; }

    bool smCryptoType() const { return m_genesisConfig.m_smCrypto; }
    std::string const& chainId() const { return m_genesisConfig.m_chainID; }
    std::string const& groupId() const { return m_genesisConfig.m_groupID; }
    size_t blockLimit() const { return m_blockLimit; }

    std::string const& privateKeyPath() const { return m_privateKeyPath; }
    bool const& enableHsm() const { return m_enableHsm; }
    std::string const& hsmLibPath() const { return m_hsmLibPath; }
    int const& keyIndex() const { return m_keyIndex; }
    int const& encKeyIndex() const { return m_encKeyIndex; }
    std::string const& password() const { return m_password; }

    size_t minSealTime() const { return m_minSealTime; }
    bool allowFreeNodeSync() const { return m_allowFreeNode; }
    size_t checkPointTimeoutInterval() const { return m_checkPointTimeoutInterval; }
    size_t pipelineSize() const { return m_pipelineSize; }

    std::string const& storagePath() const { return m_storagePath; }
    std::string const& stateDBPath() const { return m_stateDBPath; }
    std::string const& blockDBPath() const { return m_blockDBPath; }
    std::string const& storageType() const { return m_storageType; }
    size_t keyPageSize() const { return m_keyPageSize; }
    int maxWriteBufferNumber() const { return m_maxWriteBufferNumber; }
    bool enableStatistics() const { return m_enableDBStatistics; }
    int maxBackgroundJobs() const { return m_maxBackgroundJobs; }
    size_t writeBufferSize() const { return m_writeBufferSize; }
    int minWriteBufferNumberToMerge() const { return m_minWriteBufferNumberToMerge; }
    size_t blockCacheSize() const { return m_blockCacheSize; }
    bool enableRocksDBBlob() const { return m_enableRocksDBBlob; }
    std::vector<std::string> const& pdAddrs() const { return m_pd_addrs; }
    std::string const& pdCaPath() const { return m_pdCaPath; }
    std::string const& pdCertPath() const { return m_pdCertPath; }
    std::string const& pdKeyPath() const { return m_pdKeyPath; }
    std::string const& storageDBName() const { return m_storageDBName; }
    std::string const& stateDBName() const { return m_stateDBName; }
    bool enableArchive() const { return m_enableArchive; }
    bool syncArchivedBlocks() const { return m_syncArchivedBlocks; }
    bool enableSeparateBlockAndState() const { return m_enableSeparateBlockAndState; }
    std::string const& archiveListenIP() const { return m_archiveListenIP; }
    uint16_t archiveListenPort() const { return m_archiveListenPort; }

    bcos::crypto::KeyFactory::Ptr keyFactory() { return m_keyFactory; }

    bcos::ledger::LedgerConfig::Ptr ledgerConfig() { return m_ledgerConfig; }

    std::string const& consensusType() const { return m_genesisConfig.m_consensusType; }
    size_t txGasLimit() const { return m_genesisConfig.m_txGasLimit; }
    std::string const& genesisData() const { return m_genesisData; }

    std::int64_t epochSealerNum() const { return m_genesisConfig.m_epochSealerNum; }
    std::int64_t epochBlockNum() const { return m_genesisConfig.m_epochBlockNum; }

    bool isWasm() const { return m_genesisConfig.m_isWasm; }
    bool isAuthCheck() const { return m_genesisConfig.m_isAuthCheck; }
    bool isSerialExecute() const { return m_genesisConfig.m_isSerialExecute; }
    size_t vmCacheSize() const { return m_vmCacheSize; }

    std::string const& authAdminAddress() const { return m_genesisConfig.m_authAdminAccount; }

    std::string const& rpcServiceName() const { return m_rpcServiceName; }
    std::string const& gatewayServiceName() const { return m_gatewayServiceName; }

    std::string const& schedulerServiceName() const { return m_schedulerServiceName; }
    std::string const& executorServiceName() const { return m_executorServiceName; }
    std::string const& txpoolServiceName() const { return m_txpoolServiceName; }

    std::string const& nodeName() const { return m_nodeName; }

    std::string getDefaultServiceName(
        std::string const& _nodeName, std::string const& _serviceName) const;

    // the rpc configurations
    const std::string& rpcListenIP() const { return m_rpcListenIP; }
    uint16_t rpcListenPort() const { return m_rpcListenPort; }
    uint32_t rpcThreadPoolSize() const { return m_rpcThreadPoolSize; }
    uint32_t rpcFilterTimeout() const { return m_rpcFilterTimeout; }
    uint32_t rpcMaxProcessBlock() const { return m_rpcMaxProcessBlock; }
    bool rpcSmSsl() const { return m_rpcSmSsl; }
    bool rpcDisableSsl() const { return m_rpcDisableSsl; }

    // the web3 rpc configurations
    bool enableWeb3Rpc() const { return m_enableWeb3Rpc; }
    const std::string& web3RpcListenIP() const { return m_web3RpcListenIP; }
    uint16_t web3RpcListenPort() const { return m_web3RpcListenPort; }
    uint32_t web3RpcThreadSize() const { return m_web3RpcThreadSize; }
    uint32_t web3FilterTimeout() const { return m_web3FilterTimeout; }
    uint32_t web3MaxProcessBlock() const { return m_web3MaxProcessBlock; }

    // the gateway configurations
    const std::string& p2pListenIP() const { return m_p2pListenIP; }
    uint16_t p2pListenPort() const { return m_p2pListenPort; }
    bool p2pSmSsl() const { return m_p2pSmSsl; }
    const std::string& p2pNodeDir() const { return m_p2pNodeDir; }
    const std::string& p2pNodeFileName() const { return m_p2pNodeFileName; }

    // config for cert
    const std::string& certPath() { return m_certPath; }
    void setCertPath(const std::string& _certPath) { m_certPath = _certPath; }

    const std::string& caCert() { return m_caCert; }
    void setCaCert(const std::string& _caCert) { m_caCert = _caCert; }

    const std::string& nodeCert() { return m_nodeCert; }
    void setNodeCert(const std::string& _nodeCert) { m_nodeCert = _nodeCert; }

    const std::string& nodeKey() { return m_nodeKey; }
    void setNodeKey(const std::string& _nodeKey) { m_nodeKey = _nodeKey; }

    const std::string& smCaCert() const { return m_smCaCert; }
    void setSmCaCert(const std::string& _smCaCert) { m_smCaCert = _smCaCert; }

    const std::string& smNodeCert() const { return m_smNodeCert; }
    void setSmNodeCert(const std::string& _smNodeCert) { m_smNodeCert = _smNodeCert; }

    const std::string& smNodeKey() const { return m_smNodeKey; }
    void setSmNodeKey(const std::string& _smNodeKey) { m_smNodeKey = _smNodeKey; }

    const std::string& enSmNodeCert() const { return m_enSmNodeCert; }
    void setEnSmNodeCert(const std::string& _enSmNodeCert) { m_enSmNodeCert = _enSmNodeCert; }

    const std::string& enSmNodeKey() const { return m_enSmNodeKey; }
    void setEnSmNodeKey(const std::string& _enSmNodeKey) { m_enSmNodeKey = _enSmNodeKey; }

    bool enableLRUCacheStorage() const { return m_enableLRUCacheStorage; }
    ssize_t cacheSize() const { return m_cacheSize; }

    uint32_t compatibilityVersion() const { return m_genesisConfig.m_compatibilityVersion; }
    std::string compatibilityVersionStr() const
    {
        std::stringstream ss;
        ss << (bcos::protocol::BlockVersion)m_genesisConfig.m_compatibilityVersion;
        return ss.str();
    }

    std::string const& memberID() const { return m_memberID; }
    unsigned leaseTTL() const { return m_leaseTTL; }
    bool enableFailOver() const { return m_enableFailOver; }
    std::string const& failOverClusterUrl() const { return m_failOverClusterUrl; }

    bool storageSecurityEnable() const { return m_storageSecurityEnable; }
    std::string storageSecurityKeyCenterIp() const { return m_storageSecurityKeyCenterIp; }
    unsigned short storageSecurityKeyCenterPort() const { return m_storageSecurityKeyCenterPort; }
    std::string storageSecurityCipherDataKey() const { return m_storageSecurityCipherDataKey; }

    bool enableSendBlockStatusByTree() const { return m_enableSendBlockStatusByTree; }
    bool enableSendTxByTree() const { return m_enableSendTxByTree; }
    std::int64_t treeWidth() const { return m_treeWidth; }

    int sendTxTimeout() const { return m_sendTxTimeout; }

    bool withoutTarsFramework() const { return m_withoutTarsFramework; }
    void setWithoutTarsFramework(bool _withoutTarsFramework)
    {
        m_withoutTarsFramework = _withoutTarsFramework;
    }
    void getTarsClientProxyEndpoints(
        const std::string& _clientPrx, std::vector<tars::TC_Endpoint>& _endPoints);

    bool enableBaselineScheduler() const { return m_enableBaselineScheduler; }
    struct BaselineSchedulerConfig
    {
        bool parallel = false;
        int chunkSize = 0;
        int maxThread = 0;
    };
    BaselineSchedulerConfig const& baselineSchedulerConfig() const
    {
        return m_baselineSchedulerConfig;
    }

    struct TarsRPCConfig
    {
        std::string host;
        uint16_t port = 0;
        uint32_t threadCount = 0;
    };
    TarsRPCConfig const& tarsRPCConfig() const { return m_tarsRPCConfig; }

    ledger::GenesisConfig const& genesisConfig() const;

protected:
    virtual void loadChainConfig(boost::property_tree::ptree const& _pt, bool _enforceGroupId);
    virtual void loadWeb3ChainConfig(boost::property_tree::ptree const& _pt);
    virtual void loadRpcConfig(boost::property_tree::ptree const& _pt);
    virtual void loadWeb3RpcConfig(boost::property_tree::ptree const& _pt);
    virtual void loadGatewayConfig(boost::property_tree::ptree const& _pt);
    virtual void loadCertConfig(boost::property_tree::ptree const& _pt);
    virtual void loadTxPoolConfig(boost::property_tree::ptree const& _pt);
    virtual void loadSecurityConfig(boost::property_tree::ptree const& _pt);
    virtual void loadSealerConfig(boost::property_tree::ptree const& _pt);
    virtual void loadStorageSecurityConfig(boost::property_tree::ptree const& _pt);
    virtual void loadSyncConfig(boost::property_tree::ptree const& _pt);

    virtual void loadStorageConfig(boost::property_tree::ptree const& _pt);
    virtual void loadConsensusConfig(boost::property_tree::ptree const& _pt);

    virtual void loadFailOverConfig(
        boost::property_tree::ptree const& _pt, bool _enforceMemberID = true);
    virtual void loadOthersConfig(boost::property_tree::ptree const& _pt);

    virtual void loadLedgerConfig(boost::property_tree::ptree const& _genesisConfig);

    // load config.genesis
    void loadExecutorConfig(boost::property_tree::ptree const& _pt);

    // load config.ini
    void loadExecutorNormalConfig(boost::property_tree::ptree const& _pt);

    std::string getServiceName(boost::property_tree::ptree const& _pt,
        std::string const& _configSection, std::string const& _objName,
        std::string const& _defaultValue = "", bool _require = true);
    void checkService(std::string const& _serviceType, std::string const& _serviceName);

private:
    void loadGenesisFeatures(boost::property_tree::ptree const& ptree);
    void loadAlloc(boost::property_tree::ptree const& ptree)
    {
        if (auto node = ptree.get_child_optional("alloc"))
        {
            for (const auto& it : *node)
            {
                auto flag = it.first;
                auto enableNumber = it.second.get_value<bool>();
                m_genesisConfig.m_features.emplace_back(
                    ledger::FeatureSet{.flag = ledger::Features::string2Flag(flag),
                        .enable = static_cast<int>(enableNumber)});
            }
        }
    }

    bcos::consensus::ConsensusNodeListPtr parseConsensusNodeList(
        boost::property_tree::ptree const& _pt, std::string const& _sectionName,
        std::string const& _subSectionName);

    virtual int64_t checkAndGetValue(boost::property_tree::ptree const& _pt,
        std::string const& _value, std::string const& _defaultValue);

    bool isValidPort(int port);

    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    // txpool related configuration
    size_t m_txpoolLimit;
    size_t m_notifyWorkerNum;
    size_t m_verifierWorkerNum;
    int64_t m_txsExpirationTime;
    bool m_checkBlockLimit = true;
    // TODO: the block sync module need some configurations?

    // chain configuration
    size_t m_blockLimit;

    // sealer configuration
    size_t m_minSealTime = 0;
    bool m_allowFreeNode = false;
    size_t m_checkPointTimeoutInterval;
    size_t m_pipelineSize = 50;

    // for security
    std::string m_privateKeyPath;
    bool m_enableHsm;
    std::string m_hsmLibPath;
    int m_keyIndex;
    int m_encKeyIndex;
    std::string m_password;

    // storage security configuration
    bool m_storageSecurityEnable;
    std::string m_storageSecurityKeyCenterIp;
    unsigned short m_storageSecurityKeyCenterPort;
    std::string m_storageSecurityCipherDataKey;

    // ledger configuration
    bcos::ledger::LedgerConfig::Ptr m_ledgerConfig;
    std::string m_genesisData;

    // Genesis config
    ledger::GenesisConfig m_genesisConfig;

    // storage configuration
    std::string m_storagePath;
    std::string m_storageType = "RocksDB";
    size_t m_keyPageSize = 10240;
    std::vector<std::string> m_pd_addrs;
    std::string m_pdCaPath;
    std::string m_pdCertPath;
    std::string m_pdKeyPath;
    bool m_enableDBStatistics = false;
    int m_maxWriteBufferNumber = 3;
    int m_maxBackgroundJobs = 3;
    size_t m_writeBufferSize = 64 << 21;
    int m_minWriteBufferNumberToMerge = 2;
    size_t m_blockCacheSize = 128 << 20;
    bool m_enableRocksDBBlob = false;

    bool m_enableArchive = false;
    bool m_syncArchivedBlocks = false;
    bool m_enableSeparateBlockAndState = false;
    std::string m_stateDBPath;
    std::string m_blockDBPath;
    std::string m_archiveListenIP;
    uint16_t m_archiveListenPort = 0;

    std::string m_storageDBName = "storage";
    std::string m_stateDBName = "state";

    // executor config
    size_t m_vmCacheSize = 1024;
    bool m_enableBaselineScheduler = false;
    BaselineSchedulerConfig m_baselineSchedulerConfig;
    TarsRPCConfig m_tarsRPCConfig;

    // Pro and Max versions run do not apply to tars admin site
    bool m_withoutTarsFramework = {false};

    // service name to tars endpoints
    std::unordered_map<std::string, std::vector<tars::TC_Endpoint>> m_tarsSN2EndPoints;

    std::string m_rpcServiceName;
    std::string m_gatewayServiceName;

    // the serviceName of other modules
    std::string m_schedulerServiceName;
    std::string m_executorServiceName;
    std::string m_txpoolServiceName;
    std::string m_nodeName;

    // config for rpc
    std::string m_rpcListenIP;
    uint16_t m_rpcListenPort;
    uint32_t m_rpcThreadPoolSize;
    uint32_t m_rpcFilterTimeout;
    uint32_t m_rpcMaxProcessBlock;
    bool m_rpcSmSsl;
    bool m_rpcDisableSsl = false;

    // config fro web3 rpc
    bool m_enableWeb3Rpc = false;
    std::string m_web3RpcListenIP;
    uint16_t m_web3RpcListenPort;
    uint32_t m_web3RpcThreadSize;
    uint32_t m_web3FilterTimeout;
    uint32_t m_web3MaxProcessBlock;

    // config for gateway
    std::string m_p2pListenIP;
    uint16_t m_p2pListenPort;
    bool m_p2pSmSsl;
    std::string m_p2pNodeDir;
    std::string m_p2pNodeFileName;

    // config for sync
    bool m_enableSendBlockStatusByTree = false;
    bool m_enableSendTxByTree = false;
    std::uint32_t m_treeWidth = 3;

    // config for cert
    std::string m_certPath;

    std::string m_caCert;
    std::string m_nodeCert;
    std::string m_nodeKey;

    std::string m_smCaCert;
    std::string m_smNodeCert;
    std::string m_smNodeKey;
    std::string m_enSmNodeCert;
    std::string m_enSmNodeKey;

    bool m_enableLRUCacheStorage = true;
    ssize_t m_cacheSize = DEFAULT_CACHE_SIZE;  // 32MB for default

    // failover config
    std::string m_memberID;
    unsigned m_leaseTTL = 0;
    bool m_enableFailOver = false;
    // etcd/zookeeper/consual url
    std::string m_failOverClusterUrl;

    // others config
    int m_sendTxTimeout = -1;
    int64_t checkAndGetValue(const boost::property_tree::ptree& _pt, const std::string& _key);
};

std::string generateGenesisData(
    ledger::GenesisConfig const& genesisConfig, ledger::LedgerConfig const& ledgerConfig);

}  // namespace bcos::tool
