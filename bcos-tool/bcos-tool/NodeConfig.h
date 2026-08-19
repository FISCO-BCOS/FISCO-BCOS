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

    // setters still expose the legacy members until the cutover phase.
    // ================================================================

    struct BaselineSchedulerConfig
    {
        bool parallel = false;
        int grainSize = 0;
    };

    struct TarsRPCConfig
    {
        std::string host;
        uint16_t port = 0;
    } tarsRPC;

    struct ChainConfig
    {
        size_t blockLimit = 1000;
    } chain;

    struct TxPoolConfig
    {
        size_t limit = 15000;
        int64_t txsExpirationTime = 600'000;
        bool checkBlockLimit = true;
        bool enableTxsFromFreeNode = false;
        bool preStoreBackpressureEnabled = true;
        size_t preStoreMaxInflight = 1024;
    } txpool;

    struct SealerConfig
    {
        size_t minSealTime = 500;
        bool allowFreeNode = false;
    } sealer;

    struct ConsensusConfig
    {
        size_t checkPointTimeoutInterval = DEFAULT_MIN_CONSENSUS_TIME_MS;
        size_t pipelineSize = DEFAULT_PIPELINE_SIZE;
        bool pipelineAdmissionEnabled = true;
        size_t pipelinePerPeerCapacity = 64;
        size_t pipelineLruCapacity = 256;
        size_t pipelineMaxPeers = 1024;
    } consensus;

    struct SecurityConfig
    {
        std::string privateKeyPath = "node.pem";
        std::string hsmLibPath;
        int keyIndex{};
        std::string password;
        security::KeyEncryptionType keyEncryptionType = security::KeyEncryptionType::LEGACY;
        security::CloudKmsType cloudKmsType = security::CloudKmsType::AWS;
        std::string keyEncryptionUrl;
        std::string bcosKmsKeySecurityCipherDataKey;
    } security;

    struct StorageSecurityConfig
    {
        bool enable = false;
        security::StorageEncryptionType encryptionType = security::StorageEncryptionType::LEGACY;
        std::string keyCenterUrl;
        std::string cipherDataKey;
    } storageSecurity;

    struct StorageConfig
    {
        std::string dataPath;
        std::string type = "RocksDB";
        size_t keyPageSize = 10240;
        std::vector<std::string> pdAddrs;
        std::string pdCaPath;
        std::string pdCertPath;
        std::string pdKeyPath;
        bool enableStatistics = false;
        // Effective defaults are 4/4/64MB/1 (matching load()); the struct is the
        // single source and load() falls back to these members.
        int maxWriteBufferNumber = 4;
        int maxBackgroundJobs = 4;
        size_t writeBufferSize = 64 << 20;
        int minWriteBufferNumberToMerge = 1;
        size_t blockCacheSize = 128 << 20;
        bool enableRocksDBBlob = false;
        bool enableArchive = false;
        bool syncArchivedBlocks = false;
        bool enableSeparateBlockAndState = false;
        std::string stateDBPath;
        std::string blockDBPath;
        std::string archiveListenIP;
        uint16_t archiveListenPort = 0;
        std::string dbName = "storage";
        std::string stateDBName = "state";
        bool enableLRUCacheStorage = true;
        ssize_t cacheSize = DEFAULT_CACHE_SIZE;
    } storage;

    struct ExecutorConfig
    {
        size_t vmCacheSize = 1024;
        BaselineSchedulerConfig baselineScheduler;
    } executor;

    struct RpcConfig
    {
        std::string listenIP = "0.0.0.0";
        uint16_t listenPort = 20200;
        uint32_t filterTimeout = 300'000;  // ms
        uint32_t maxProcessBlock = 10;
        bool smSsl = false;
        bool disableSsl = false;
    } rpc;

    struct Web3RpcConfig
    {
        bool enable = false;
        std::string listenIP = "127.0.0.1";
        uint16_t listenPort = 8545;
        uint32_t filterTimeout = 300'000;  // ms
        uint32_t maxProcessBlock = 10;
        uint32_t batchRequestSizeLimit = 8;
        uint32_t httpBodySizeLimit = 10'240'000;
        bool enableCors = true;
        std::string corsAllowedOrigins = "*";
        std::string corsAllowedMethods = "GET, POST, OPTIONS";
        std::string corsAllowedHeaders = "Content-Type, Authorization, X-Requested-With";
        int32_t corsMaxAge = 86400;
        bool corsAllowCredentials = true;
        bool syncTransaction = false;
        // "safe"/"finalized" point latest - depth blocks behind; 0 = latest
        uint32_t safeBlockDepth = 0;
        uint32_t finalizedBlockDepth = 0;
    } web3Rpc;

    struct OpEngineRpcConfig
    {
        bool enable = false;
        std::string listenIP = "127.0.0.1";
        uint16_t listenPort = 8551;
        uint32_t httpBodySizeLimit = 10'485'760;
        uint32_t batchRequestSizeLimit = 8;
        std::string jwtSecretFile = "conf/op-engine/jwt.hex";
        int32_t clockSkewSecs = 60;
        // test-only escape hatch, see Initializer's executor-version guard
        bool allowV1Executor = false;
    } opEngineRpc;

    struct SingleNodeConsensusConfig
    {
        bool enable = false;
        uint64_t blockInterval = 1000;
        bool produceEmptyBlocks = true;
        std::string feeRecipient = "0x0000000000000000000000000000000000000000";
        std::string prevRandao;
        uint64_t fixedTimestamp = 0;
    } singleNodeConsensus;

    struct GatewayConfig
    {
        std::string listenIP;
        uint16_t listenPort{};
        bool smSsl = false;
        std::string nodeDir = "./";
        std::string nodeFileName = "nodes.json";
    } gateway;

    struct SyncConfig
    {
        bool enableSendBlockStatusByTree = false;
        bool enableSendTxByTree = false;
        uint32_t treeWidth = 3;
    } sync;

    struct CertConfig
    {
        std::string path = "./";
        std::string caCert;
        std::string nodeCert;
        std::string nodeKey;
        std::string smCaCert;
        std::string smNodeCert;
        std::string smNodeKey;
        std::string enSmNodeCert;
        std::string enSmNodeKey;
    } cert;

    struct FailOverConfig
    {
        bool enable = false;
        std::string clusterUrl = "127.0.0.1:2379";
        std::string memberID;
        unsigned leaseTTL = 0;
    } failOver;

    struct ThreadPoolConfig
    {
        size_t ioThreadCount{};
        size_t tbbThreadCount{};
    } threadPool;

    struct OthersConfig
    {
        int sendTxTimeout = -1;
        bool checkTransactionSignature = true;
        bool checkParallelConflict = true;
        bool singlePointConsensus = false;
        bytes forceSender;
    } others;

    struct ServiceConfig
    {
        bool withoutTarsFramework = false;
        std::unordered_map<std::string, std::vector<tars::TC_Endpoint>> tarsSN2EndPoints;
        std::string rpcServiceName;
        std::string gatewayServiceName;
        std::string schedulerServiceName;
        std::string executorServiceName;
        std::string txpoolServiceName;
        std::string nodeName;
    } service;

    // core objects (genesis-derived state, key factory, ledger config)
    bcos::crypto::KeyFactory::Ptr keyFactory;
    bcos::ledger::LedgerConfig::Ptr ledgerConfig;
    ledger::GenesisConfig genesisConfig;

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
    virtual void loadOpEngineRpcConfig(boost::property_tree::ptree const& _pt);

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

    // derived helpers (not plain field access)
    std::string getDefaultServiceName(
        std::string const& _nodeName, std::string const& _serviceName) const;
    void getTarsClientProxyEndpoints(
        const std::string& _clientPrx, std::vector<tars::TC_Endpoint>& _endPoints);
    std::string compatibilityVersionStr() const;
    // true when block production is driven through the EngineService — by the built-in
    // single-node driver or by an external op-node over [op_engine_rpc] — and the legacy
    // txpool/PBFT pipeline must therefore stay dormant (sole-producer discipline)
    bool engineDrivenBlockProduction() const;
    bool isValidPort(int port);

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
    virtual void loadSingleNodeConsensusConfig(boost::property_tree::ptree const& _pt);
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

    // A6.5: L2 genesis alloc parsing (L2 mode gated by feature_l2_ethereum_compat)
    void loadAllocs(boost::property_tree::ptree const& _genesisConfig);
    void loadEthGenesisHeader(boost::property_tree::ptree const& _genesisConfig);
    void validateL2Invariants();

    bcos::consensus::ConsensusNodeList parseConsensusNodeList(
        boost::property_tree::ptree const& _pt, std::string const& _sectionName,
        std::string const& _subSectionName);
};

std::string generateGenesisData(
    ledger::GenesisConfig const& genesisConfig, ledger::LedgerConfig const& ledgerConfig);

}  // namespace bcos::tool
