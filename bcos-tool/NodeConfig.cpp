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
 * @file NodeConfig.cpp
 * @author: yujiechen
 * @date 2021-06-10
 */
#include "NodeConfig.h"
#include "VersionConverter.h"
#include "bcos-framework/interfaces/consensus/ConsensusNode.h"
#include "bcos-framework/interfaces/ledger/LedgerTypeDef.h"
#include "bcos-framework/interfaces/protocol/ServiceDesc.h"
#include <bcos-framework/interfaces/protocol/GlobalConfig.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>

#define MAX_BLOCK_LIMIT 5000

using namespace bcos;
using namespace bcos::crypto;
using namespace bcos::tool;
using namespace bcos::consensus;
using namespace bcos::ledger;
using namespace bcos::protocol;

NodeConfig::NodeConfig(KeyFactory::Ptr _keyFactory)
  : m_keyFactory(_keyFactory), m_ledgerConfig(std::make_shared<LedgerConfig>())
{}

void NodeConfig::loadConfig(boost::property_tree::ptree const& _pt)
{
    loadChainConfig(_pt);
    loadCertConfig(_pt);
    loadRpcConfig(_pt);
    loadGatewayConfig(_pt);
    loadTxPoolConfig(_pt);

    loadSecurityConfig(_pt);
    loadSealerConfig(_pt);
    loadConsensusConfig(_pt);
    loadStorageConfig(_pt);
    loadExecutorConfig(_pt);
}

void NodeConfig::loadGenesisConfig(boost::property_tree::ptree const& _genesisConfig)
{
    loadLedgerConfig(_genesisConfig);
}

std::string NodeConfig::getServiceName(boost::property_tree::ptree const& _pt,
    std::string const& _configSection, std::string const& _objName,
    std::string const& _defaultValue, bool _require)
{
    auto serviceName = _pt.get<std::string>(_configSection, _defaultValue);
    if (!_require)
    {
        return serviceName;
    }
    checkService(_configSection, serviceName);
    return getPrxDesc(serviceName, _objName);
}

void NodeConfig::loadRpcServiceConfig(boost::property_tree::ptree const& _pt)
{
    // rpc service name
    m_rpcServiceName = getServiceName(_pt, "service.rpc", RPC_SERVANT_NAME);
    NodeConfig_LOG(INFO) << LOG_DESC("loadServiceConfig")
                         << LOG_KV("rpcServiceName", m_rpcServiceName);
}

void NodeConfig::loadGatewayServiceConfig(boost::property_tree::ptree const& _pt)
{
    // gateway service name
    m_gatewayServiceName = getServiceName(_pt, "service.gateway", GATEWAY_SERVANT_NAME);
    NodeConfig_LOG(INFO) << LOG_DESC("loadServiceConfig")
                         << LOG_KV("gatewayServiceName", m_gatewayServiceName);
}
void NodeConfig::loadServiceConfig(boost::property_tree::ptree const& _pt)
{
    loadGatewayServiceConfig(_pt);
    loadRpcServiceConfig(_pt);
}

void NodeConfig::loadNodeServiceConfig(
    std::string const& _nodeID, boost::property_tree::ptree const& _pt)
{
    auto nodeName = _pt.get<std::string>("service.node_name", "");
    if (nodeName.size() == 0)
    {
        nodeName = _nodeID;
    }
    if (!isalNumStr(nodeName))
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("The node name must be number or digit"));
    }
    m_nodeName = nodeName;
    m_schedulerServiceName = getServiceName(_pt, "service.scheduler", SCHEDULER_SERVANT_NAME,
        getDefaultServiceName(nodeName, SCHEDULER_SERVICE_NAME), false);
    m_txpoolServiceName = getServiceName(_pt, "service.txpool", TXPOOL_SERVANT_NAME,
        getDefaultServiceName(nodeName, TXPOOL_SERVICE_NAME), false);
    m_consensusServiceName = getServiceName(_pt, "service.consensus", CONSENSUS_SERVANT_NAME,
        getDefaultServiceName(nodeName, CONSENSUS_SERVICE_NAME), false);
    m_frontServiceName = getServiceName(_pt, "service.front", FRONT_SERVANT_NAME,
        getDefaultServiceName(nodeName, FRONT_SERVICE_NAME), false);
    m_executorServiceName = getServiceName(_pt, "service.executor", EXECUTOR_SERVANT_NAME,
        getDefaultServiceName(nodeName, EXECUTOR_SERVICE_NAME), false);

    NodeConfig_LOG(INFO) << LOG_DESC("load node service") << LOG_KV("nodeName", m_nodeName)
                         << LOG_KV("schedulerServiceName", m_schedulerServiceName)
                         << LOG_KV("txpoolServiceName", m_txpoolServiceName)
                         << LOG_KV("consensusServiceName", m_consensusServiceName)
                         << LOG_KV("frontServiceName", m_frontServiceName)
                         << LOG_KV("executorServiceName", m_executorServiceName);
}
void NodeConfig::checkService(std::string const& _serviceType, std::string const& _serviceName)
{
    if (_serviceName.size() == 0)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Must set service name for " + _serviceType + "!"));
    }
    std::vector<std::string> serviceNameList;
    boost::split(serviceNameList, _serviceName, boost::is_any_of("."));
    std::string errorMsg =
        "Must set service name in format of application_name.server_name with only include letters "
        "and numbers for " +
        _serviceType + ", invalid config now is:" + _serviceName;
    if (serviceNameList.size() != 2)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(errorMsg));
    }
    for (auto serviceName : serviceNameList)
    {
        if (!isalNumStr(serviceName))
        {
            BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(errorMsg));
        }
    }
}

void NodeConfig::loadRpcConfig(boost::property_tree::ptree const& _pt)
{
    /*
    [rpc]
        listen_ip=0.0.0.0
        listen_port=30300
        thread_count=16
        sm_ssl=false
        disable_ssl=false
    */
    std::string listenIP = _pt.get<std::string>("rpc.listen_ip", "0.0.0.0");
    int listenPort = _pt.get<int>("rpc.listen_port", 20200);
    int threadCount = _pt.get<int>("rpc.thread_count", 8);
    bool smSsl = _pt.get<bool>("rpc.sm_ssl", false);
    bool disableSsl = _pt.get<bool>("rpc.disable_ssl", false);

    m_rpcListenIP = listenIP;
    m_rpcListenPort = listenPort;
    m_rpcThreadPoolSize = threadCount;
    m_rpcDisableSsl = disableSsl;
    m_rpcSmSsl = smSsl;

    NodeConfig_LOG(INFO) << LOG_DESC("loadRpcConfig") << LOG_KV("listenIP", listenIP)
                         << LOG_KV("listenPort", listenPort) << LOG_KV("listenPort", listenPort)
                         << LOG_KV("smSsl", smSsl) << LOG_KV("disableSsl", disableSsl);
}

void NodeConfig::loadGatewayConfig(boost::property_tree::ptree const& _pt)
{
    /*
    [p2p]
    listen_ip=0.0.0.0
    listen_port=30300
    sm_ssl=false
    nodes_path=./
    nodes_file=nodes.json
    */
    std::string listenIP = _pt.get<std::string>("p2p.listen_ip", "0.0.0.0");
    int listenPort = _pt.get<int>("p2p.listen_port", 30300);
    std::string nodesDir = _pt.get<std::string>("p2p.nodes_path", "./");
    std::string nodesFile = _pt.get<std::string>("p2p.nodes_file", "nodes.json");
    bool smSsl = _pt.get<bool>("p2p.sm_ssl", false);

    m_p2pListenIP = listenIP;
    m_p2pListenPort = listenPort;
    m_p2pNodeDir = nodesDir;
    m_p2pSmSsl = smSsl;
    m_p2pNodeFileName = nodesFile;

    NodeConfig_LOG(INFO) << LOG_DESC("loadGatewayConfig") << LOG_KV("listenIP", listenIP)
                         << LOG_KV("listenPort", listenPort) << LOG_KV("listenPort", listenPort)
                         << LOG_KV("smSsl", smSsl) << LOG_KV("nodesFile", nodesFile);
}

void NodeConfig::loadCertConfig(boost::property_tree::ptree const& _pt)
{
    /*
    [cert]
      ; directory the certificates located in
      ca_path=./
      ; the ca certificate file
      ca_cert=ca.crt
      ; the node private key file
      node_key=ssl.key
      ; the node certificate file
      node_cert=ssl.crt

    or

    [cert]
    ; directory the certificates located in
    ca_path=./
    ; the ca certificate file
    sm_ca_cert=sm_ca.crt
    ; the node private key file
    sm_node_key=sm_ssl.key
    ; the node certificate file
    sm_node_cert=sm_ssl.crt
    ; the node private key file
    sm_ennode_key=sm_enssl.key
    ; the node certificate file
    sm_ennode_cert=sm_enssl.crt
    */

    // load sm cert
    m_certPath = _pt.get<std::string>("cert.ca_path", "./");

    std::string smCaCertFile =
        m_certPath + "/" + _pt.get<std::string>("cert.sm_ca_cert", "sm_ca.crt");
    std::string smNodeCertFile =
        m_certPath + "/" + _pt.get<std::string>("cert.sm_node_cert", "sm_ssl.crt");
    std::string smNodeKeyFile =
        m_certPath + "/" + _pt.get<std::string>("cert.sm_node_key", "sm_ssl.key");
    std::string smEnNodeCertFile =
        m_certPath + "/" + _pt.get<std::string>("cert.sm_ennode_cert", "sm_enssl.crt");
    std::string smEnNodeKeyFile =
        m_certPath + "/" + _pt.get<std::string>("cert.sm_ennode_key", "sm_enssl.key");

    m_smCaCert = smCaCertFile;
    m_smNodeCert = smNodeCertFile;
    m_smNodeKey = smNodeKeyFile;
    m_enSmNodeCert = smEnNodeCertFile;
    m_enSmNodeKey = smEnNodeKeyFile;

    NodeConfig_LOG(INFO) << LOG_DESC("loadCertConfig") << LOG_KV("ca_path", m_certPath)
                         << LOG_KV("sm_ca_cert", smCaCertFile)
                         << LOG_KV("sm_node_cert", smNodeCertFile)
                         << LOG_KV("sm_node_key", smNodeKeyFile)
                         << LOG_KV("sm_ennode_cert", smEnNodeCertFile)
                         << LOG_KV("sm_ennode_key", smEnNodeKeyFile);

    // load cert
    std::string caCertFile = m_certPath + "/" + _pt.get<std::string>("cert.ca_cert", "ca.crt");
    std::string nodeCertFile = m_certPath + "/" + _pt.get<std::string>("cert.node_cert", "ssl.crt");
    std::string nodeKeyFile = m_certPath + "/" + _pt.get<std::string>("cert.node_key", "ssl.key");

    m_caCert = caCertFile;
    m_nodeCert = nodeCertFile;
    m_nodeKey = nodeKeyFile;

    NodeConfig_LOG(INFO) << LOG_DESC("loadCertConfig") << LOG_KV("ca_path", m_certPath)
                         << LOG_KV("ca_cert", caCertFile) << LOG_KV("node_cert", nodeCertFile)
                         << LOG_KV("node_key", nodeKeyFile);
}

// load the txpool related params
void NodeConfig::loadTxPoolConfig(boost::property_tree::ptree const& _pt)
{
    m_txpoolLimit = checkAndGetValue(_pt, "txpool.limit", "15000");
    if (m_txpoolLimit <= 0)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Please set txpool.limit to positive !"));
    }
    m_notifyWorkerNum = checkAndGetValue(_pt, "txpool.notify_worker_num", "2");
    if (m_notifyWorkerNum <= 0)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set txpool.notify_worker_num to positive !"));
    }

    m_verifierWorkerNum = checkAndGetValue(_pt, "txpool.verify_worker_num", "2");
    if (m_verifierWorkerNum <= 0)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set txpool.verify_worker_num to positive !"));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadTxPoolConfig") << LOG_KV("txpoolLimit", m_txpoolLimit)
                         << LOG_KV("notifierWorkers", m_notifyWorkerNum)
                         << LOG_KV("verifierWorkers", m_verifierWorkerNum);
}

void NodeConfig::loadChainConfig(boost::property_tree::ptree const& _pt)
{
    m_smCryptoType = _pt.get<bool>("chain.sm_crypto", false);
    m_groupId = _pt.get<std::string>("chain.group_id", "group");
    m_chainId = _pt.get<std::string>("chain.chain_id", "chain");
    if (!isalNumStr(m_chainId))
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("The chainId must be number or digit"));
    }
    m_blockLimit = checkAndGetValue(_pt, "chain.block_limit", "1000");
    if (m_blockLimit <= 0 || m_blockLimit > MAX_BLOCK_LIMIT)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set chain.block_limit to positive and less than " +
                                  std::to_string(MAX_BLOCK_LIMIT) + " !"));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadChainConfig") << LOG_KV("smCrypto", m_smCryptoType)
                         << LOG_KV("chainId", m_chainId) << LOG_KV("groupId", m_groupId)
                         << LOG_KV("blockLimit", m_blockLimit);
}

void NodeConfig::loadSecurityConfig(boost::property_tree::ptree const& _pt)
{
    m_privateKeyPath = _pt.get<std::string>("security.private_key_path", "node.pem");
    NodeConfig_LOG(INFO) << LOG_DESC("loadSecurityConfig")
                         << LOG_KV("privateKeyPath", m_privateKeyPath);
}

void NodeConfig::loadSealerConfig(boost::property_tree::ptree const& _pt)
{
    m_minSealTime = checkAndGetValue(_pt, "consensus.min_seal_time", "500");
    if (m_minSealTime <= 0 || m_minSealTime > 3000)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set consensus.min_seal_time between 1 and 3000!"));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadSealerConfig") << LOG_KV("minSealTime", m_minSealTime);
}

void NodeConfig::loadStorageConfig(boost::property_tree::ptree const& _pt)
{
    m_storagePath = _pt.get<std::string>("storage.data_path", "data/" + m_groupId);
    m_storageType = _pt.get<std::string>("storage.type", "RocksDB");
    auto pd_addrs = _pt.get<std::string>("storage.pd_addrs", "127.0.0.1:2379");
    boost::split(m_pd_addrs, pd_addrs, boost::is_any_of(","));
    m_enableLRUCacheStorage = _pt.get<bool>("storage.enable_cache", true);
    m_cacheSize = _pt.get<ssize_t>("storage.cache_size", DEFAULT_CACHE_SIZE);
    NodeConfig_LOG(INFO) << LOG_DESC("loadStorageConfig") << LOG_KV("storagePath", m_storagePath)
                         << LOG_KV("storageType", m_storageType) << LOG_KV("pd_addrs", pd_addrs)
                         << LOG_KV("enableLRUCacheStorage", m_enableLRUCacheStorage);
}

void NodeConfig::loadConsensusConfig(boost::property_tree::ptree const& _pt)
{
    m_checkPointTimeoutInterval = checkAndGetValue(_pt, "consensus.checkpoint_timeout", "3000");
    if (m_checkPointTimeoutInterval < 3000)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set consensus.checkpoint_timeout to no less than " +
                                  std::to_string(3000) + "ms!"));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadConsensusConfig")
                         << LOG_KV("checkPointTimeoutInterval", m_checkPointTimeoutInterval);
}

void NodeConfig::loadLedgerConfig(boost::property_tree::ptree const& _genesisConfig)
{
    // consensus type
    m_consensusType = _genesisConfig.get<std::string>("consensus.consensus_type", "pbft");
    // blockTxCountLimit
    auto blockTxCountLimit =
        checkAndGetValue(_genesisConfig, "consensus.block_tx_count_limit", "1000");
    if (blockTxCountLimit <= 0)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set consensus.block_tx_count_limit to positive!"));
    }
    m_ledgerConfig->setBlockTxCountLimit(blockTxCountLimit);
    // txGasLimit
    auto txGasLimit = checkAndGetValue(_genesisConfig, "tx.gas_limit", "3000000000");
    if (txGasLimit <= TX_GAS_LIMIT_MIN)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "Please set tx.gas_limit to more than " + std::to_string(TX_GAS_LIMIT_MIN) + " !"));
    }

    m_txGasLimit = txGasLimit;
    // the compatibility version
    m_version = _genesisConfig.get<std::string>(
        "version.compatibility_version", bcos::protocol::RC3_VERSION_STR);
    // must call here to check the compatibility_version
    m_compatibilityVersion = toVersionNumber(m_version);
    g_BCOSConfig.setVersion(m_compatibilityVersion);

    // sealerList
    auto consensusNodeList = parseConsensusNodeList(_genesisConfig, "consensus", "node.");
    if (!consensusNodeList || consensusNodeList->empty())
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment("Must set sealerList!"));
    }
    m_ledgerConfig->setConsensusNodeList(*consensusNodeList);

    // leaderSwitchPeriod
    auto consensusLeaderPeriod = checkAndGetValue(_genesisConfig, "consensus.leader_period", "1");
    if (consensusLeaderPeriod <= 0)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Please set consensus.leader_period to positive!"));
    }
    m_ledgerConfig->setLeaderSwitchPeriod(consensusLeaderPeriod);
    NodeConfig_LOG(INFO) << LOG_DESC("loadLedgerConfig")
                         << LOG_KV("consensus_type", m_consensusType)
                         << LOG_KV("block_tx_count_limit", m_ledgerConfig->blockTxCountLimit())
                         << LOG_KV("gas_limit", m_txGasLimit)
                         << LOG_KV("leader_period", m_ledgerConfig->leaderSwitchPeriod())
                         << LOG_KV("minSealTime", m_minSealTime)
                         << LOG_KV("compatibilityVersion", m_compatibilityVersion);
    generateGenesisData();
}

ConsensusNodeListPtr NodeConfig::parseConsensusNodeList(boost::property_tree::ptree const& _pt,
    std::string const& _sectionName, std::string const& _subSectionName)
{
    if (!_pt.get_child_optional(_sectionName))
    {
        NodeConfig_LOG(DEBUG) << LOG_DESC("parseConsensusNodeList return for empty config")
                              << LOG_KV("sectionName", _sectionName);
        return nullptr;
    }
    auto nodeList = std::make_shared<ConsensusNodeList>();
    for (auto const& it : _pt.get_child(_sectionName))
    {
        if (it.first.find(_subSectionName) != 0)
        {
            continue;
        }
        std::string data = it.second.data();
        std::vector<std::string> nodeInfo;
        boost::split(nodeInfo, data, boost::is_any_of(":"));
        if (nodeInfo.size() == 0)
        {
            BOOST_THROW_EXCEPTION(
                InvalidConfig() << errinfo_comment(
                    "Uninitialized nodeInfo, key: " + it.first + ", value: " + data));
        }
        std::string nodeId = nodeInfo[0];
        boost::to_lower(nodeId);
        int64_t weight = 1;
        if (nodeInfo.size() == 2)
        {
            auto& weightInfoStr = nodeInfo[1];
            boost::trim(weightInfoStr);
            weight = boost::lexical_cast<int64_t>(weightInfoStr);
        }
        if (weight <= 0)
        {
            BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                      "Please set weight for " + nodeId + " to positive!"));
        }
        auto consensusNode = std::make_shared<ConsensusNode>(
            m_keyFactory->createKey(*fromHexString(nodeId)), weight);
        NodeConfig_LOG(INFO) << LOG_BADGE("parseConsensusNodeList")
                             << LOG_KV("sectionName", _sectionName) << LOG_KV("nodeId", nodeId)
                             << LOG_KV("weight", weight);
        nodeList->push_back(consensusNode);
    }
    // only sort nodeList after rc3 version
    if (g_BCOSConfig.version() > bcos::protocol::Version::RC3_VERSION)
    {
        std::sort(nodeList->begin(), nodeList->end(), bcos::consensus::ConsensusNodeComparator());
    }
    NodeConfig_LOG(INFO) << LOG_BADGE("parseConsensusNodeList")
                         << LOG_KV("totalNodesSize", nodeList->size());
    return nodeList;
}

void NodeConfig::generateGenesisData()
{
    std::string versionData = "";
    if (m_compatibilityVersion >= bcos::protocol::Version::RC4_VERSION)
    {
        versionData = m_version + "-";
    }
    std::stringstream s;
    s << m_ledgerConfig->blockTxCountLimit() << "-" << m_ledgerConfig->leaderSwitchPeriod() << "-"
      << m_txGasLimit << "-" << versionData;
    for (auto node : m_ledgerConfig->consensusNodeList())
    {
        s << *toHexString(node->nodeID()->data()) << "," << node->weight() << ";";
    }
    m_genesisData = s.str();
    NodeConfig_LOG(INFO) << LOG_BADGE("generateGenesisData")
                         << LOG_KV("genesisData", m_genesisData);
}

void NodeConfig::loadExecutorConfig(boost::property_tree::ptree const& _pt)
{
    m_isWasm = _pt.get<bool>("executor.is_wasm", false);
    m_isAuthCheck = _pt.get<bool>("executor.is_auth_check", false);
    m_authAdminAddress = _pt.get<std::string>("executor.auth_admin_account", "");
    NodeConfig_LOG(INFO) << LOG_DESC("loadExecutorConfig") << LOG_KV("isWasm", m_isWasm);
    NodeConfig_LOG(INFO) << LOG_DESC("loadExecutorConfig") << LOG_KV("isAuthCheck", m_isAuthCheck);
    NodeConfig_LOG(INFO) << LOG_DESC("loadExecutorConfig")
                         << LOG_KV("authAdminAccount", m_authAdminAddress);
}

// Note: make sure the consensus param checker is consistent with the precompiled param checker
int64_t NodeConfig::checkAndGetValue(boost::property_tree::ptree const& _pt,
    std::string const& _key, std::string const& _defaultValue)
{
    auto value = _pt.get<std::string>(_key, _defaultValue);
    try
    {
        return boost::lexical_cast<int64_t>(value);
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Invalid value " + value + " for configuration " + _key +
                                  ", please set the value with a valid number"));
    }
}