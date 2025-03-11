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
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/security/CloudKmsType.h"
#include "bcos-framework/security/KeyEncryptionType.h"
#include "bcos-framework/security/StorageEncryptionType.h"
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/Common.h>
#include <bcos-framework/protocol/Protocol.h>
#include <util/tc_clientsocket.h>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <cstddef>
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
    NodeConfig();

    NodeConfig(const NodeConfig&) = default;
    NodeConfig(NodeConfig&&) = default;
    NodeConfig& operator=(const NodeConfig&) = default;
    NodeConfig& operator=(NodeConfig&&) = default;
    explicit NodeConfig(bcos::crypto::KeyFactory::Ptr _keyFactory);
    virtual ~NodeConfig() = default;

    virtual void loadConfig(std::string const& _configPath, bool _enforceMemberID = true,
        bool enforceChainConfig = false, bool enforceGroupId = true);
    virtual void loadServiceConfig(boost::property_tree::ptree const& _pt);
    virtual void loadRpcServiceConfig(boost::property_tree::ptree const& _pt);
    virtual void loadGatewayServiceConfig(boost::property_tree::ptree const& _pt);

    virtual void loadWithoutTarsFrameworkConfig(boost::property_tree::ptree const& _pt);

    virtual void loadNodeServiceConfig(
        std::string const& _nodeID, boost::property_tree::ptree const& _pt, bool _require = false);

    virtual void loadTarsProxyConfig(const std::string& _tarsProxyConf);

    virtual void loadServiceTarsProxyConfig(
        const std::string& _serviceSectionName, boost::property_tree::ptree const& _pt);

    virtual void loadGenesisConfig(std::string const& _genesisConfigPath);

    virtual void loadConfigFromString(std::string const& _content);

    virtual void loadGenesisConfigFromString(std::string const& _content);

    virtual void loadConfig(boost::property_tree::ptree const& _pt, bool _enforceMemberID = true,
        bool _enforceChainConfig = false, bool _enforceGroupId = true);
    virtual void loadGenesisConfig(boost::property_tree::ptree const& _genesisConfig);

    // the txpool configurations
    size_t txpoolLimit() const;
    size_t notifyWorkerNum() const;
    size_t verifierWorkerNum() const;
    int64_t txsExpirationTime() const;
    bool checkBlockLimit() const;

    bool smCryptoType() const;
    std::string const& chainId() const;
    std::string const& groupId() const;
    size_t blockLimit() const;

    std::string const& privateKeyPath() const;
    std::string const& hsmLibPath() const;
    int const& keyIndex() const;
    int const& encKeyIndex() const;
    std::string const& password() const;

    size_t minSealTime() const;
    bool allowFreeNodeSync() const;
    size_t checkPointTimeoutInterval() const;
    size_t pipelineSize() const;

    std::string const& storagePath() const;
    std::string const& stateDBPath() const;
    std::string const& blockDBPath() const;
    std::string const& storageType() const;
    size_t keyPageSize() const;
    int maxWriteBufferNumber() const;
    bool enableStatistics() const;
    int maxBackgroundJobs() const;
    size_t writeBufferSize() const;
    int minWriteBufferNumberToMerge() const;
    size_t blockCacheSize() const;
    bool enableRocksDBBlob() const;
    std::vector<std::string> const& pdAddrs() const;
    std::string const& pdCaPath() const;
    std::string const& pdCertPath() const;
    std::string const& pdKeyPath() const;
    std::string const& storageDBName() const;
    std::string const& stateDBName() const;
    bool enableArchive() const;
    bool syncArchivedBlocks() const;
    bool enableSeparateBlockAndState() const;
    std::string const& archiveListenIP() const;
    uint16_t archiveListenPort() const;

    bcos::crypto::KeyFactory::Ptr keyFactory();

    bcos::ledger::LedgerConfig::Ptr ledgerConfig();

    std::string const& consensusType() const;
    size_t txGasLimit() const;
    std::string const& genesisData() const;

    std::int64_t epochSealerNum() const;
    std::int64_t epochBlockNum() const;

    bool isWasm() const;
    bool isAuthCheck() const;
    bool isSerialExecute() const;
    size_t vmCacheSize() const;

    std::string const& authAdminAddress() const;

    std::string const& rpcServiceName() const;
    std::string const& gatewayServiceName() const;

    std::string const& schedulerServiceName() const;
    std::string const& executorServiceName() const;
    std::string const& txpoolServiceName() const;

    std::string const& nodeName() const;

    std::string getDefaultServiceName(
        std::string const& _nodeName, std::string const& _serviceName) const;

    // the rpc configurations
    const std::string& rpcListenIP() const;
    uint16_t rpcListenPort() const;
    uint32_t rpcThreadPoolSize() const;
    uint32_t rpcFilterTimeout() const;
    uint32_t rpcMaxProcessBlock() const;
    bool rpcSmSsl() const;
    bool rpcDisableSsl() const;

    // the web3 rpc configurations
    bool enableWeb3Rpc() const;
    const std::string& web3RpcListenIP() const;
    uint16_t web3RpcListenPort() const;
    uint32_t web3RpcThreadSize() const;
    uint32_t web3FilterTimeout() const;
    uint32_t web3MaxProcessBlock() const;

    // the gateway configurations
    const std::string& p2pListenIP() const;
    uint16_t p2pListenPort() const;
    bool p2pSmSsl() const;
    const std::string& p2pNodeDir() const;
    const std::string& p2pNodeFileName() const;

    // config for cert
    const std::string& certPath();
    void setCertPath(const std::string& _certPath);

    const std::string& caCert();
    void setCaCert(const std::string& _caCert);

    const std::string& nodeCert();
    void setNodeCert(const std::string& _nodeCert);

    const std::string& nodeKey();
    void setNodeKey(const std::string& _nodeKey);

    const std::string& smCaCert() const;
    void setSmCaCert(const std::string& _smCaCert);

    const std::string& smNodeCert() const;
    void setSmNodeCert(const std::string& _smNodeCert);

    const std::string& smNodeKey() const;
    void setSmNodeKey(const std::string& _smNodeKey);

    const std::string& enSmNodeCert() const;
    void setEnSmNodeCert(const std::string& _enSmNodeCert);

    const std::string& enSmNodeKey() const;
    void setEnSmNodeKey(const std::string& _enSmNodeKey);

    bool enableLRUCacheStorage() const;
    ssize_t cacheSize() const;

    uint32_t compatibilityVersion() const;
    std::string compatibilityVersionStr() const;

    std::string const& memberID() const;
    unsigned leaseTTL() const;
    bool enableFailOver() const;
    std::string const& failOverClusterUrl() const;

    bool storageSecurityEnable() const;
    std::string storageSecuirtyKeyCenterUrl() const;
    std::string storageSecurityCipherDataKey() const;

    security::KeyEncryptionType keyEncryptionType() const;
    security::StorageEncryptionType storageEncryptionType() const;
    security::CloudKmsType cloudKmsType() const;
    std::string bcosKmsKeySecurityCipherDataKey() const;
    std::string keyEncryptionUrl() const;

    bool enableSendBlockStatusByTree() const;
    bool enableSendTxByTree() const;
    std::int64_t treeWidth() const;

    int sendTxTimeout() const;

    bool withoutTarsFramework() const;
    void setWithoutTarsFramework(bool _withoutTarsFramework);
    void getTarsClientProxyEndpoints(
        const std::string& _clientPrx, std::vector<tars::TC_Endpoint>& _endPoints);

    struct BaselineSchedulerConfig
    {
        bool parallel = false;
        int grainSize = 0;
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

    bool checkTransactionSignature() const;
    bool checkParallelConflict() const;

    ledger::GenesisConfig const& genesisConfig() const;

    bool isValidPort(int port);

    bool enableTxsFromFreeNode() const { return m_enableTxsFromFreeNode; }

    int executorVersion() const;

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
    void loadAlloc(boost::property_tree::ptree const& ptree);

    bcos::consensus::ConsensusNodeList parseConsensusNodeList(
        boost::property_tree::ptree const& _pt, std::string const& _sectionName,
        std::string const& _subSectionName);

    virtual int64_t checkAndGetValue(boost::property_tree::ptree const& _pt,
        std::string const& _value, std::string const& _defaultValue);

    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    // txpool related configuration
    size_t m_txpoolLimit{};
    size_t m_notifyWorkerNum{};
    size_t m_verifierWorkerNum{};
    int64_t m_txsExpirationTime{};
    bool m_checkBlockLimit = true;
    // permit txs from free node or not
    bool m_enableTxsFromFreeNode = false;
    // TODO: the block sync module need some configurations?

    // chain configuration
    size_t m_blockLimit{};

    // sealer configuration
    size_t m_minSealTime = 0;
    bool m_allowFreeNode = false;
    size_t m_checkPointTimeoutInterval{};
    size_t m_pipelineSize = 50;

    // for security
    std::string m_privateKeyPath;

    std::string m_hsmLibPath;
    int m_keyIndex{};
    int m_encKeyIndex{};
    std::string m_password;

    // for security cloudkms bcoskms hsm configuration
    security::KeyEncryptionType m_keyEncryptionType{};
    security::StorageEncryptionType m_storageEncryptionType{};
    // key url
    std::string m_KeyEncryptionUrl;
    // cloude kms type, 0: AWS, 1: Aliyun...
    security::CloudKmsType m_cloudKmsType{};
    // bcos kms data key
    std::string m_bcosKmsKeySecurityCipherDataKey;

    // storage security configuration
    bool m_storageSecurityEnable{};
    std::string m_storageSecurityUrl;
    // unsigned short m_storageSecurityKeyCenterPort{};
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
    uint16_t m_rpcListenPort{};
    uint32_t m_rpcThreadPoolSize{};
    uint32_t m_rpcFilterTimeout{};
    uint32_t m_rpcMaxProcessBlock{};
    bool m_rpcSmSsl{};
    bool m_rpcDisableSsl = false;

    // config fro web3 rpc
    bool m_enableWeb3Rpc = false;
    std::string m_web3RpcListenIP;
    uint16_t m_web3RpcListenPort{};
    uint32_t m_web3RpcThreadSize{};
    uint32_t m_web3FilterTimeout{};
    uint32_t m_web3MaxProcessBlock{};

    // config for gateway
    std::string m_p2pListenIP;
    uint16_t m_p2pListenPort{};
    bool m_p2pSmSsl{};
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

    // experimental
    bool m_checkTransactionSignature = true;
    bool m_checkParallelConflict = true;
};

std::string generateGenesisData(
    ledger::GenesisConfig const& genesisConfig, ledger::LedgerConfig const& ledgerConfig);

}  // namespace bcos::tool
