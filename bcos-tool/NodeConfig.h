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
#include "bcos-framework/interfaces/consensus/ConsensusNodeInterface.h"
#include "bcos-framework/interfaces/crypto/KeyFactory.h"
#include "bcos-framework/interfaces/ledger/LedgerConfig.h"
#include "Exceptions.h"
#include "bcos-utilities/Log.h"
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#define NodeConfig_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("NodeConfig")
namespace bcos
{
namespace tool
{
class NodeConfig
{
public:
    constexpr static ssize_t DEFAULT_CACHE_SIZE = 32 * 1024 * 1024;

    using Ptr = std::shared_ptr<NodeConfig>;
    NodeConfig() : m_ledgerConfig(std::make_shared<bcos::ledger::LedgerConfig>()) {}

    explicit NodeConfig(bcos::crypto::KeyFactory::Ptr _keyFactory);
    virtual ~NodeConfig() {}

    virtual void loadConfig(std::string const& _configPath)
    {
        boost::property_tree::ptree iniConfig;
        boost::property_tree::read_ini(_configPath, iniConfig);
        loadConfig(iniConfig);
    }
    virtual void loadServiceConfig(boost::property_tree::ptree const& _pt);
    virtual void loadRpcServiceConfig(boost::property_tree::ptree const& _pt);
    virtual void loadGatewayServiceConfig(boost::property_tree::ptree const& _pt);

    virtual void loadNodeServiceConfig(
        std::string const& _nodeID, boost::property_tree::ptree const& _pt);

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

    virtual void loadConfig(boost::property_tree::ptree const& _pt);
    virtual void loadGenesisConfig(boost::property_tree::ptree const& _genesisConfig);

    size_t txpoolLimit() const { return m_txpoolLimit; }
    size_t notifyWorkerNum() const { return m_notifyWorkerNum; }
    size_t verifierWorkerNum() const { return m_verifierWorkerNum; }

    bool smCryptoType() const { return m_smCryptoType; }
    std::string const& chainId() const { return m_chainId; }
    std::string const& groupId() const { return m_groupId; }
    size_t blockLimit() const { return m_blockLimit; }

    std::string const& privateKeyPath() const { return m_privateKeyPath; }

    size_t minSealTime() const { return m_minSealTime; }
    size_t checkPointTimeoutInterval() const { return m_checkPointTimeoutInterval; }

    std::string const& storagePath() const { return m_storagePath; }
    std::string const& storageDBName() const { return m_storageDBName; }
    std::string const& stateDBName() const { return m_stateDBName; }

    bcos::crypto::KeyFactory::Ptr keyFactory() { return m_keyFactory; }

    bcos::ledger::LedgerConfig::Ptr ledgerConfig() { return m_ledgerConfig; }

    std::string const& consensusType() const { return m_consensusType; }
    size_t txGasLimit() const { return m_txGasLimit; }
    std::string const& genesisData() const { return m_genesisData; }

    bool isWasm() const { return m_isWasm; }
    bool isAuthCheck() const { return m_isAuthCheck; }
    std::string const& authAdminAddress() const { return m_authAdminAddress; }

    std::string const& rpcServiceName() const { return m_rpcServiceName; }
    std::string const& gatewayServiceName() const { return m_gatewayServiceName; }

    std::string const& schedulerServiceName() const { return m_schedulerServiceName; }
    std::string const& txpoolServiceName() const { return m_txpoolServiceName; }
    std::string const& consensusServiceName() const { return m_consensusServiceName; }
    std::string const& frontServiceName() const { return m_frontServiceName; }
    std::string const& executorServiceName() const { return m_executorServiceName; }
    std::string const& nodeName() const { return m_nodeName; }

    std::string getDefaultServiceName(std::string const& _nodeName, std::string const& _serviceName)
    {
        return m_chainId + "." + _nodeName + _serviceName;
    }

    // rpc
    const std::string& rpcListenIP() const { return m_rpcListenIP; }
    uint16_t rpcListenPort() const { return m_rpcListenPort; }
    uint32_t rpcThreadPoolSize() const { return m_rpcThreadPoolSize; }
    bool rpcSmSsl() const { return m_rpcSmSsl; }
    bool rpcDisableSsl() const { return m_rpcDisableSsl; }

    // gateway
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

protected:
    virtual void loadChainConfig(boost::property_tree::ptree const& _pt);
    virtual void loadRpcConfig(boost::property_tree::ptree const& _pt);
    virtual void loadGatewayConfig(boost::property_tree::ptree const& _pt);
    virtual void loadCertConfig(boost::property_tree::ptree const& _pt);
    virtual void loadTxPoolConfig(boost::property_tree::ptree const& _pt);
    virtual void loadSecurityConfig(boost::property_tree::ptree const& _pt);
    virtual void loadSealerConfig(boost::property_tree::ptree const& _pt);

    virtual void loadStorageConfig(boost::property_tree::ptree const& _pt);
    virtual void loadConsensusConfig(boost::property_tree::ptree const& _pt);

    virtual void loadLedgerConfig(boost::property_tree::ptree const& _genesisConfig);

    void loadExecutorConfig(boost::property_tree::ptree const& _pt);

    std::string getServiceName(boost::property_tree::ptree const& _pt,
        std::string const& _configSection, std::string const& _objName,
        std::string const& _defaultValue = "", bool _require = true);
    void checkService(std::string const& _serviceType, std::string const& _serviceName);

private:
    bcos::consensus::ConsensusNodeListPtr parseConsensusNodeList(
        boost::property_tree::ptree const& _pt, std::string const& _sectionName,
        std::string const& _subSectionName);

    void generateGenesisData();
    virtual int64_t checkAndGetValue(boost::property_tree::ptree const& _pt,
        std::string const& _value, std::string const& _defaultValue);

private:
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    // txpool related configuration
    size_t m_txpoolLimit;
    size_t m_notifyWorkerNum;
    size_t m_verifierWorkerNum;
    // TODO: the block sync module need some configurations?

    // chain configuration
    bool m_smCryptoType;
    std::string m_chainId;
    std::string m_groupId;
    size_t m_blockLimit;

    // sealer configuration
    size_t m_minSealTime = 0;
    size_t m_checkPointTimeoutInterval;
    // for security
    std::string m_privateKeyPath;

    // ledger configuration
    std::string m_consensusType;
    bcos::ledger::LedgerConfig::Ptr m_ledgerConfig;
    size_t m_txGasLimit;
    std::string m_genesisData;

    // storage configuration
    std::string m_storagePath;
    std::string m_storageDBName = "storage";
    std::string m_stateDBName = "state";

    // executor config
    bool m_isWasm = false;
    bool m_isAuthCheck = false;
    std::string m_authAdminAddress;

    std::string m_rpcServiceName;
    std::string m_gatewayServiceName;

    // the serviceName of other modules
    std::string m_schedulerServiceName;
    std::string m_txpoolServiceName;
    std::string m_consensusServiceName;
    std::string m_frontServiceName;
    std::string m_executorServiceName;
    std::string m_nodeName;

    // config for rpc
    std::string m_rpcListenIP;
    uint16_t m_rpcListenPort;
    uint32_t m_rpcThreadPoolSize;
    bool m_rpcSmSsl;
    bool m_rpcDisableSsl = false;

    // config for gateway
    std::string m_p2pListenIP;
    uint16_t m_p2pListenPort;
    bool m_p2pSmSsl;
    std::string m_p2pNodeDir;
    std::string m_p2pNodeFileName;

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
};
}  // namespace tool
}  // namespace bcos
