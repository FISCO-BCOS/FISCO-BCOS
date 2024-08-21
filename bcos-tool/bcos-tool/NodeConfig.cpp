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
#include "bcos-framework/bcos-framework/protocol/Protocol.h"
#include "bcos-framework/consensus/ConsensusNode.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/ServiceDesc.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/FileUtility.h"
#include "fisco-bcos-tars-service/Common/TarsUtils.h"
#include <bcos-framework/ledger/GenesisConfig.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <json/forwards.h>
#include <json/reader.h>
#include <json/value.h>
#include <servant/RemoteLogger.h>
#include <util/tc_clientsocket.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/throw_exception.hpp>
#include <thread>

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

void NodeConfig::loadConfig(boost::property_tree::ptree const& _pt, bool _enforceMemberID,
    bool _enforceChainConfig, bool _enforceGroupId)
{
    // if version < 3.1.0, config.ini include chainConfig
    if (_enforceChainConfig || (m_genesisConfig.m_compatibilityVersion <
                                       (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION &&
                                   m_genesisConfig.m_compatibilityVersion >=
                                       (uint32_t)bcos::protocol::BlockVersion::MIN_VERSION))
    {
        loadChainConfig(_pt, _enforceGroupId);
    }
    loadCertConfig(_pt);
    loadRpcConfig(_pt);
    loadWeb3RpcConfig(_pt);
    loadGatewayConfig(_pt);
    loadSealerConfig(_pt);
    loadTxPoolConfig(_pt);
    // loadSecurityConfig before loadStorageSecurityConfig for deciding whether to use HSM
    loadSecurityConfig(_pt);
    loadStorageSecurityConfig(_pt);
    loadExecutorNormalConfig(_pt);

    loadFailOverConfig(_pt, _enforceMemberID);
    loadStorageConfig(_pt);
    loadConsensusConfig(_pt);
    loadSyncConfig(_pt);
    loadOthersConfig(_pt);
}

void NodeConfig::loadGenesisConfig(boost::property_tree::ptree const& _genesisConfig)
{
    // if version >= 3.1.0, genesisBlock include chainConfig
    auto compatibilityVersion = _genesisConfig.get<std::string>(
        "version.compatibility_version", bcos::protocol::RC4_VERSION_STR);
    m_genesisConfig.m_compatibilityVersion = toVersionNumber(compatibilityVersion);
    if (m_genesisConfig.m_compatibilityVersion >=
        (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
    {
        loadChainConfig(_genesisConfig, true);
    }
    loadWeb3ChainConfig(_genesisConfig);
    loadGenesisFeatures(_genesisConfig);

    loadLedgerConfig(_genesisConfig);
    loadExecutorConfig(_genesisConfig);
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

    /*
    [service]
        without_tars_framework = true
        tars_proxy_conf = tars_proxy.ini
     */

    auto withoutTarsFramework = _pt.get<bool>("service.without_tars_framework", false);
    m_withoutTarsFramework = withoutTarsFramework;

    NodeConfig_LOG(INFO) << LOG_DESC("loadServiceConfig")
                         << LOG_KV("withoutTarsFramework", m_withoutTarsFramework);

    if (m_withoutTarsFramework)
    {
        std::string tarsProxyConf =
            _pt.get<std::string>("service.tars_proxy_conf", "./tars_proxy.ini");
        loadTarsProxyConfig(tarsProxyConf);
    }
}

void NodeConfig::loadWithoutTarsFrameworkConfig(boost::property_tree::ptree const& _pt)
{
    /*
        [service]
            without_tars_framework = true
            tars_proxy_conf = conf/tars_proxy.ini
         */

    auto withoutTarsFramework = _pt.get<bool>("service.without_tars_framework", false);
    m_withoutTarsFramework = withoutTarsFramework;

    NodeConfig_LOG(INFO) << LOG_DESC("loadWithoutTarsFrameworkConfig")
                         << LOG_KV("withoutTarsFramework", m_withoutTarsFramework);
}

void NodeConfig::loadNodeServiceConfig(
    std::string const& _nodeID, boost::property_tree::ptree const& _pt, bool _require)
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

    /*
    [service]
        without_tars_framework = true
        tars_proxy_conf = conf/tars_proxy.ini
     */

    auto withoutTarsFramework = _pt.get<bool>("service.without_tars_framework", false);
    m_withoutTarsFramework = withoutTarsFramework;

    NodeConfig_LOG(INFO) << LOG_DESC("loadNodeServiceConfig")
                         << LOG_KV("withoutTarsFramework", m_withoutTarsFramework);

    if (m_withoutTarsFramework)
    {
        std::string tarsProxyConf =
            _pt.get<std::string>("service.tars_proxy_conf", "conf/tars_proxy.ini");
        loadTarsProxyConfig(tarsProxyConf);
    }

    m_nodeName = nodeName;
    m_schedulerServiceName = getServiceName(_pt, "service.scheduler", SCHEDULER_SERVANT_NAME,
        getDefaultServiceName(nodeName, SCHEDULER_SERVICE_NAME), _require);
    m_executorServiceName = getServiceName(_pt, "service.executor", EXECUTOR_SERVANT_NAME,
        getDefaultServiceName(nodeName, EXECUTOR_SERVICE_NAME), _require);
    m_txpoolServiceName = getServiceName(_pt, "service.txpool", TXPOOL_SERVANT_NAME,
        getDefaultServiceName(nodeName, TXPOOL_SERVICE_NAME), _require);

    NodeConfig_LOG(INFO) << LOG_DESC("load node service") << LOG_KV("nodeName", m_nodeName)
                         << LOG_KV("withoutTarsFramework", m_withoutTarsFramework)
                         << LOG_KV("schedulerServiceName", m_schedulerServiceName)
                         << LOG_KV("executorServiceName", m_executorServiceName);
}

void NodeConfig::loadTarsProxyConfig(const std::string& _tarsProxyConf)
{
    if (!m_tarsSN2EndPoints.empty())
    {
        NodeConfig_LOG(INFO) << LOG_BADGE("loadTarsProxyConfig")
                             << LOG_DESC("tars proxy config has been loaded");
        return;
    }

    boost::property_tree::ptree pt;
    try
    {
        boost::property_tree::read_ini(_tarsProxyConf, pt);

        loadServiceTarsProxyConfig("front", pt);
        loadServiceTarsProxyConfig("rpc", pt);
        loadServiceTarsProxyConfig("gateway", pt);
        loadServiceTarsProxyConfig("executor", pt);
        loadServiceTarsProxyConfig("txpool", pt);
        loadServiceTarsProxyConfig("scheduler", pt);
        loadServiceTarsProxyConfig("pbft", pt);
        loadServiceTarsProxyConfig("ledger", pt);

        NodeConfig_LOG(INFO) << LOG_BADGE("loadTarsProxyConfig")
                             << LOG_KV("tars service endpoints size", m_tarsSN2EndPoints.size());
    }
    catch (const std::exception& e)
    {
        NodeConfig_LOG(ERROR) << LOG_BADGE("loadTarsProxyConfig")
                              << LOG_DESC("load tars proxy config failed") << LOG_KV("e", e.what())
                              << LOG_KV("tarsProxyConf", _tarsProxyConf);

        BOOST_THROW_EXCEPTION(InvalidParameter() << errinfo_comment(
                                  "Load tars proxy config failed, e: " + std::string(e.what())));
    }
}

void NodeConfig::loadServiceTarsProxyConfig(
    const std::string& _serviceName, boost::property_tree::ptree const& _pt)
{
    if (!_pt.get_child_optional(_serviceName))
    {
        NodeConfig_LOG(WARNING) << LOG_BADGE("loadServiceTarsProxyConfig")
                                << LOG_DESC("service name not exist")
                                << LOG_KV("serviceName", _serviceName);
        return;
    }

    for (auto const& it : _pt.get_child(_serviceName))
    {
        if (it.first.find("proxy.") != 0)
        {
            continue;
        }

        std::string data = it.second.data();

        // string to endpoint
        tars::TC_Endpoint endpoint = bcostars::string2TarsEndPoint(data);
        m_tarsSN2EndPoints[_serviceName].push_back(endpoint);

        NodeConfig_LOG(INFO) << LOG_BADGE("loadTarsProxyConfig") << LOG_DESC("add element")
                             << LOG_KV("serviceName", _serviceName)
                             << LOG_KV("endpoint", endpoint.toString());
    }

    NodeConfig_LOG(INFO) << LOG_BADGE("loadTarsProxyConfig") << LOG_KV("serviceName", _serviceName)
                         << LOG_KV("endpoints size", m_tarsSN2EndPoints[_serviceName].size());
}

//
void NodeConfig::getTarsClientProxyEndpoints(
    const std::string& _clientPrx, std::vector<tars::TC_Endpoint>& _endpoints)
{
    if (!m_withoutTarsFramework)
    {
        NodeConfig_LOG(TRACE) << LOG_BADGE("getTarsClientProxyEndpoints")
                              << "not work with tars rpc"
                              << LOG_KV("withoutTarsFramework", m_withoutTarsFramework);
        return;
    }

    _endpoints.clear();

    auto it = m_tarsSN2EndPoints.find(boost::to_lower_copy(_clientPrx));
    if (it != m_tarsSN2EndPoints.end())
    {
        _endpoints = it->second;

        NodeConfig_LOG(DEBUG) << LOG_BADGE("getTarsClientProxyEndpoints")
                              << LOG_DESC("find tars client proxy endpoints")
                              << LOG_KV("serviceName", _clientPrx)
                              << LOG_KV("endpoints size", _endpoints.size());
    }

    if (_endpoints.empty())
    {
        NodeConfig_LOG(WARNING) << LOG_BADGE("getTarsClientProxyEndpoints")
                                << LOG_DESC("can not find tars client proxy endpoints")
                                << LOG_KV("serviceName", _clientPrx);

        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment(
                ("Can't find tars client proxy endpoints, serviceName : " + _clientPrx)));
    }
}

void NodeConfig::checkService(std::string const& _serviceType, std::string const& _serviceName)
{
    if (_serviceName.empty())
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
    for (const auto& serviceName : serviceNameList)
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
        ; 300s
        filter_timeout=300
        filter_max_process_block=10
    */
    std::string listenIP = _pt.get<std::string>("rpc.listen_ip", "0.0.0.0");
    int listenPort = _pt.get<int>("rpc.listen_port", 20200);
    int threadCount = _pt.get<int>("rpc.thread_count", 8);
    int filterTimeout = _pt.get<int>("rpc.filter_timeout", 300);
    int maxProcessBlock = _pt.get<int>("rpc.filter_max_process_block", 10);
    bool smSsl = _pt.get<bool>("rpc.sm_ssl", false);
    bool disableSsl = _pt.get<bool>("rpc.disable_ssl", false);
    bool needRetInput = _pt.get<bool>("rpc.return_input_params", true);

    m_rpcListenIP = listenIP;
    m_rpcListenPort = listenPort;
    m_rpcThreadPoolSize = threadCount;
    m_rpcDisableSsl = disableSsl;
    m_rpcSmSsl = smSsl;
    m_rpcFilterTimeout = filterTimeout;
    m_rpcMaxProcessBlock = maxProcessBlock;
    g_BCOSConfig.setNeedRetInput(needRetInput);

    NodeConfig_LOG(INFO) << LOG_DESC("loadRpcConfig") << LOG_KV("listenIP", listenIP)
                         << LOG_KV("listenPort", listenPort) << LOG_KV("listenPort", listenPort)
                         << LOG_KV("smSsl", smSsl) << LOG_KV("disableSsl", disableSsl)
                         << LOG_KV("needRetInput", needRetInput);
}

void NodeConfig::loadWeb3RpcConfig(boost::property_tree::ptree const& _pt)
{
    /*
    [web3_rpc]
        enable=false
        listen_ip=127.0.0.1
        listen_port=8545
        thread_count=16
        ; 300s
        filter_timeout=300
        filter_max_process_block=10
    */
    const std::string listenIP = _pt.get<std::string>("web3_rpc.listen_ip", "127.0.0.1");
    const int listenPort = _pt.get<int>("web3_rpc.listen_port", 8545);
    const int threadCount = _pt.get<int>("web3_rpc.thread_count", 8);
    const int filterTimeout = _pt.get<int>("web3_rpc.filter_timeout", 300);
    const int maxProcessBlock = _pt.get<int>("web3_rpc.filter_max_process_block", 10);
    const bool enableWeb3Rpc = _pt.get<bool>("web3_rpc.enable", false);

    m_web3RpcListenIP = listenIP;
    m_web3RpcListenPort = listenPort;
    m_web3RpcThreadSize = threadCount;
    m_enableWeb3Rpc = enableWeb3Rpc;
    m_web3FilterTimeout = filterTimeout;
    m_web3MaxProcessBlock = maxProcessBlock;

    NodeConfig_LOG(INFO) << LOG_DESC("loadWeb3RpcConfig") << LOG_KV("enableWeb3Rpc", enableWeb3Rpc)
                         << LOG_KV("listenIP", listenIP) << LOG_KV("listenPort", listenPort)
                         << LOG_KV("listenPort", listenPort);
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
    m_verifierWorkerNum = checkAndGetValue(_pt, "txpool.verify_worker_num",
        std::to_string(std::min(8U, std::thread::hardware_concurrency())));
    if (m_verifierWorkerNum <= 0)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set txpool.verify_worker_num to positive !"));
    }
    // the txs expiration time, in second
    auto txsExpirationTime = checkAndGetValue(_pt, "txpool.txs_expiration_time", "600");
    if (txsExpirationTime * 1000 <= DEFAULT_MIN_CONSENSUS_TIME_MS) [[unlikely]]
    {
        NodeConfig_LOG(WARNING) << LOG_DESC(
                                       "loadTxPoolConfig: the configured txs_expiration_time "
                                       "is smaller than default "
                                       "consensus time, reset to the consensus time")
                                << LOG_KV("txsExpirationTime(seconds)", txsExpirationTime)
                                << LOG_KV("defaultConsTime", DEFAULT_MIN_CONSENSUS_TIME_MS);
    }
    m_txsExpirationTime = std::max(
        {txsExpirationTime * 1000, (int64_t)DEFAULT_MIN_CONSENSUS_TIME_MS, (int64_t)m_minSealTime});

    m_checkBlockLimit = _pt.get<bool>("txpool.check_block_limit", true);
    NodeConfig_LOG(INFO) << LOG_DESC("loadTxPoolConfig") << LOG_KV("txpoolLimit", m_txpoolLimit)
                         << LOG_KV("notifierWorkers", m_notifyWorkerNum)
                         << LOG_KV("verifierWorkers", m_verifierWorkerNum)
                         << LOG_KV("checkBlockLimit", m_checkBlockLimit)
                         << LOG_KV("txsExpirationTime(ms)", m_txsExpirationTime);
}

void NodeConfig::loadChainConfig(boost::property_tree::ptree const& _pt, bool _enforceGroupId)
{
    try
    {
        m_genesisConfig.m_smCrypto = _pt.get<bool>("chain.sm_crypto", false);
        if (_enforceGroupId)
        {
            m_genesisConfig.m_groupID = _pt.get<std::string>("chain.group_id", "group");
        }
        m_genesisConfig.m_chainID = _pt.get<std::string>("chain.chain_id", "chain");
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "chain.sm_crypto/chain.group_id/chain.chain_id is null, please set it,"
                " if compatibility_version in genesis block >= 3.1.0,"
                " 'chain' config should appear in config.genesis, else in config.ini."));
    }
    if (!isalNumStr(m_genesisConfig.m_chainID))
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
    NodeConfig_LOG(INFO) << METRIC << LOG_DESC("loadChainConfig")
                         << LOG_KV("smCrypto", m_genesisConfig.m_smCrypto)
                         << LOG_KV("chainId", m_genesisConfig.m_chainID)
                         << LOG_KV("groupId", m_genesisConfig.m_groupID)
                         << LOG_KV("blockLimit", m_blockLimit);
}

void NodeConfig::NodeConfig::loadWeb3ChainConfig(boost::property_tree::ptree const& _pt)
{
    m_genesisConfig.m_web3ChainID = _pt.get<std::string>("web3.chain_id", "0");
    if (!isNumStr(m_genesisConfig.m_web3ChainID))
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("The web3ChainId must be number string"));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadWeb3ChainConfig")
                         << LOG_KV("web3ChainID", m_genesisConfig.m_web3ChainID);
}

void NodeConfig::loadSecurityConfig(boost::property_tree::ptree const& _pt)
{
    m_privateKeyPath = _pt.get<std::string>("security.private_key_path", "node.pem");
    m_enableHsm = _pt.get<bool>("security.enable_hsm", false);
    if (m_enableHsm)
    {
        m_hsmLibPath =
            _pt.get<std::string>("security.hsm_lib_path", "/usr/local/lib/libgmt0018.so");
        m_keyIndex = _pt.get<int>("security.key_index");
        m_password = _pt.get<std::string>("security.password", "");
        NodeConfig_LOG(INFO) << LOG_DESC("loadSecurityConfig HSM")
                             << LOG_KV("lib_path", m_hsmLibPath) << LOG_KV("key_index", m_keyIndex)
                             << LOG_KV("password", m_password);
    }

    NodeConfig_LOG(INFO) << LOG_DESC("loadSecurityConfig") << LOG_KV("enable_hsm", m_enableHsm)
                         << LOG_KV("privateKeyPath", m_privateKeyPath);
}

void NodeConfig::loadSealerConfig(boost::property_tree::ptree const& _pt)
{
    m_minSealTime = checkAndGetValue(_pt, "consensus.min_seal_time", "500");
    m_allowFreeNode = _pt.get<bool>("sync.allow_free_node", false);
    if (m_minSealTime <= 0 || m_minSealTime > DEFAULT_MAX_SEAL_TIME_MS)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set consensus.min_seal_time between 1 and 600000!"));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadSealerConfig") << LOG_KV("minSealTime", m_minSealTime);
}

void NodeConfig::loadStorageSecurityConfig(boost::property_tree::ptree const& _pt)
{
    m_storageSecurityEnable = _pt.get<bool>("storage_security.enable", false);
    if (!m_storageSecurityEnable)
    {
        return;
    }

    std::string storageSecurityKeyCenterUrl =
        _pt.get<std::string>("storage_security.key_center_url", "");

    std::vector<std::string> values;
    boost::split(
        values, storageSecurityKeyCenterUrl, boost::is_any_of(":"), boost::token_compress_on);
    if (2 != values.size())
    {
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment(
                "initGlobalConfig storage_security failed! Invalid key_center_url!"));
    }

    m_storageSecurityKeyCenterIp = values[0];
    m_storageSecurityKeyCenterPort = boost::lexical_cast<unsigned short>(values[1]);
    if (!isValidPort(m_storageSecurityKeyCenterPort))
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "initGlobalConfig storage_security failed! Invalid key_manange_port!"));
    }

    m_storageSecurityCipherDataKey = _pt.get<std::string>("storage_security.cipher_data_key", "");
    if (m_storageSecurityCipherDataKey.empty())
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Please provide cipher_data_key!"));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadStorageSecurityConfig")
                         << LOG_KV("keyCenterUrl", storageSecurityKeyCenterUrl);
}

void NodeConfig::loadSyncConfig(const boost::property_tree::ptree& _pt)
{
    m_enableSendBlockStatusByTree = _pt.get<bool>("sync.sync_block_by_tree", false);
    m_enableSendTxByTree = _pt.get<bool>("sync.send_txs_by_tree", false);
    m_treeWidth = _pt.get<std::uint32_t>("sync.tree_width", 3);
    if (m_treeWidth == 0 || m_treeWidth > UINT16_MAX)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Please set sync.tree_width in 1~65535"));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadSyncConfig")
                         << LOG_KV("sync_block_by_tree", m_enableSendBlockStatusByTree)
                         << LOG_KV("send_txs_by_tree", m_enableSendTxByTree)
                         << LOG_KV("tree_width", m_treeWidth);
}

void NodeConfig::loadStorageConfig(boost::property_tree::ptree const& _pt)
{
    m_storagePath = _pt.get<std::string>("storage.data_path", "data/" + m_genesisConfig.m_groupID);
    m_storageType = _pt.get<std::string>("storage.type", "RocksDB");
    m_keyPageSize = _pt.get<int32_t>("storage.key_page_size", 10240);
    m_maxWriteBufferNumber = _pt.get<int32_t>("storage.max_write_buffer_number", 4);
    m_maxBackgroundJobs = _pt.get<int32_t>("storage.max_background_jobs", 4);
    m_writeBufferSize = _pt.get<size_t>("storage.write_buffer_size", 64 << 20);
    m_minWriteBufferNumberToMerge = _pt.get<int32_t>("storage.min_write_buffer_number_to_merge", 1);
    m_blockCacheSize = _pt.get<size_t>("storage.block_cache_size", 128 << 20);
    m_enableDBStatistics = _pt.get<bool>("storage.enable_statistics", false);
    m_enableRocksDBBlob = _pt.get<bool>("storage.enable_rocksdb_blob", false);
    m_pdCaPath = _pt.get<std::string>("storage.pd_ssl_ca_path", "");
    m_pdCertPath = _pt.get<std::string>("storage.pd_ssl_cert_path", "");
    m_pdKeyPath = _pt.get<std::string>("storage.pd_ssl_key_path", "");
    m_enableArchive = _pt.get<bool>("storage.enable_archive", false);
    m_syncArchivedBlocks = _pt.get<bool>("storage.sync_archived_blocks", false);
    m_enableSeparateBlockAndState = _pt.get<bool>("storage.enable_separate_block_state", false);
    if (boost::iequals(m_storageType, bcos::storage::TiKV))
    {
        m_enableSeparateBlockAndState = false;
        NodeConfig_LOG(INFO) << LOG_DESC("Only rocksDB support separate block and state")
                             << LOG_KV("separateBlockAndState", m_enableSeparateBlockAndState)
                             << LOG_KV("storageType", m_storageType);
    }
    m_stateDBPath = m_storagePath;
    m_stateDBPath = m_storagePath + "/state";
    m_blockDBPath = m_storagePath + "/block";

    if (m_enableArchive)
    {
        m_archiveListenIP = _pt.get<std::string>("storage.archive_ip");
        m_archiveListenPort = _pt.get<uint16_t>("storage.archive_port");
    }

    // if (m_keyPageSize < 4096 || m_keyPageSize > (1 << 25))
    // {
    //     BOOST_THROW_EXCEPTION(
    //         InvalidConfig() << errinfo_comment("Please set storage.key_page_size in 4K~32M"));
    // }
    auto pd_addrs = _pt.get<std::string>("storage.pd_addrs", "127.0.0.1:2379");
    boost::split(m_pd_addrs, pd_addrs, boost::is_any_of(","));
    m_enableLRUCacheStorage = _pt.get<bool>("storage.enable_cache", true);
    m_cacheSize = _pt.get<ssize_t>("storage.cache_size", DEFAULT_CACHE_SIZE);
    g_BCOSConfig.setStorageType(m_storageType);  // Set storageType to global
    NodeConfig_LOG(INFO) << LOG_DESC("loadStorageConfig") << LOG_KV("storagePath", m_storagePath)
                         << LOG_KV("KeyPage", m_keyPageSize) << LOG_KV("storageType", m_storageType)
                         << LOG_KV("pdAddrs", pd_addrs) << LOG_KV("pdCaPath", m_pdCaPath)
                         << LOG_KV("enableArchive", m_enableArchive)
                         << LOG_KV("enableSeparateBlockAndState", m_enableSeparateBlockAndState)
                         << LOG_KV("archiveListenIP", m_archiveListenIP)
                         << LOG_KV("archiveListenPort", m_archiveListenPort)
                         << LOG_KV("enable_rocksdb_blob", m_enableRocksDBBlob)
                         << LOG_KV("enableLRUCacheStorage", m_enableLRUCacheStorage);
}

// Note: In components that do not require failover, do not need to set member_id
void NodeConfig::loadFailOverConfig(boost::property_tree::ptree const& _pt, bool _enforceMemberID)
{
    // only enable leaderElection when using tikv
    m_enableFailOver = _pt.get("failover.enable", false);
    if (!m_enableFailOver)
    {
        return;
    }
    m_failOverClusterUrl = _pt.get<std::string>("failover.cluster_url", "127.0.0.1:2379");
    m_memberID = _pt.get("failover.member_id", "");
    if (m_memberID.size() == 0 && _enforceMemberID)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Please set failover.member_id must be non-empty "));
    }
    m_leaseTTL =
        checkAndGetValue(_pt, "failover.lease_ttl", std::to_string(DEFAULT_MIN_LEASE_TTL_SECONDS));
    if (m_leaseTTL < DEFAULT_MIN_LEASE_TTL_SECONDS)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set failover.lease_ttl to no less than " +
                                  std::to_string(DEFAULT_MIN_LEASE_TTL_SECONDS) + " seconds!"));
    }

    NodeConfig_LOG(INFO) << LOG_DESC("loadFailOverConfig")
                         << LOG_KV("failOverClusterUrl", m_failOverClusterUrl)
                         << LOG_KV("memberID", m_memberID.size() > 0 ? m_memberID : "not-set")
                         << LOG_KV("leaseTTL", m_leaseTTL)
                         << LOG_KV("enableFailOver", m_enableFailOver);
}

void NodeConfig::loadOthersConfig(boost::property_tree::ptree const& _pt)
{
    m_sendTxTimeout = _pt.get<int>("others.send_tx_timeout", -1);
    m_vmCacheSize = _pt.get<int>("executor.vm_cache_size", 1024);
    m_enableBaselineScheduler = _pt.get<bool>("executor.baseline_scheduler", false);
    m_baselineSchedulerConfig.chunkSize =
        _pt.get<int>("executor.baseline_scheduler_chunksize", 100);
    m_baselineSchedulerConfig.maxThread = _pt.get<int>("executor.baseline_scheduler_maxthread", 16);
    m_baselineSchedulerConfig.parallel =
        _pt.get<bool>("executor.baseline_scheduler_parallel", false);

    m_tarsRPCConfig.host = _pt.get<std::string>("rpc.tars_rpc_host", "127.0.0.1");
    m_tarsRPCConfig.port = _pt.get<int>("rpc.tars_rpc_port", 0);
    m_tarsRPCConfig.threadCount = _pt.get<int>("rpc.tars_rpc_thread_count", 8);

    NodeConfig_LOG(INFO) << LOG_DESC("loadOthersConfig") << LOG_KV("sendTxTimeout", m_sendTxTimeout)
                         << LOG_KV("vmCacheSize", m_vmCacheSize);
}

void NodeConfig::loadConsensusConfig(boost::property_tree::ptree const& _pt)
{
    m_checkPointTimeoutInterval = checkAndGetValue(
        _pt, "consensus.checkpoint_timeout", std::to_string(DEFAULT_MIN_CONSENSUS_TIME_MS));
    m_pipelineSize =
        checkAndGetValue(_pt, "consensus.pipeline_size", std::to_string(DEFAULT_PIPELINE_SIZE));
    if (m_checkPointTimeoutInterval < DEFAULT_MIN_CONSENSUS_TIME_MS)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set consensus.checkpoint_timeout to no less than " +
                                  std::to_string(DEFAULT_MIN_CONSENSUS_TIME_MS) + "ms!"));
    }
    if (m_pipelineSize < DEFAULT_PIPELINE_SIZE)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set consensus.pipeline_size to no less than " +
                                  std::to_string(DEFAULT_PIPELINE_SIZE)));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadConsensusConfig")
                         << LOG_KV("checkPointTimeoutInterval", m_checkPointTimeoutInterval)
                         << LOG_KV("pipeline_size", m_pipelineSize);
}

void NodeConfig::loadLedgerConfig(boost::property_tree::ptree const& _genesisConfig)
{
    // consensus type
    try
    {
        m_genesisConfig.m_consensusType =
            _genesisConfig.get<std::string>("consensus.consensus_type", "pbft");
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("consensus.consensus_type is null, please set it!"));
    }
    if (m_genesisConfig.m_consensusType != bcos::ledger::PBFT_CONSENSUS_TYPE &&
        m_genesisConfig.m_consensusType != bcos::ledger::RPBFT_CONSENSUS_TYPE)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "consensus.consensus_type is illegal, it must be pbft or rpbft!"));
    }
    // blockTxCountLimit
    auto blockTxCountLimit =
        checkAndGetValue(_genesisConfig, "consensus.block_tx_count_limit", "1000");
    if (blockTxCountLimit <= 0)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set consensus.block_tx_count_limit to positive!"));
    }
    m_ledgerConfig->setBlockTxCountLimit(blockTxCountLimit);
    m_genesisConfig.m_txCountLimit = blockTxCountLimit;

    // txGasLimit
    auto txGasLimit = checkAndGetValue(_genesisConfig, "tx.gas_limit", "3000000000");
    if (txGasLimit <= TX_GAS_LIMIT_MIN)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "Please set tx.gas_limit to more than " + std::to_string(TX_GAS_LIMIT_MIN) + " !"));
    }
    m_genesisConfig.m_txGasLimit = txGasLimit;
    // the compatibility version
    auto compatibilityVersion = _genesisConfig.get<std::string>(
        "version.compatibility_version", bcos::protocol::RC4_VERSION_STR);
    // must call here to check the compatibility_version
    m_genesisConfig.m_compatibilityVersion = toVersionNumber(compatibilityVersion);
    // sealerList
    auto consensusNodeList = parseConsensusNodeList(_genesisConfig, "consensus", "node.");
    if (!consensusNodeList || consensusNodeList->empty())
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment("Must set sealerList!"));
    }
    m_ledgerConfig->setConsensusNodeList(*consensusNodeList);

    // rpbft
    if (m_genesisConfig.m_consensusType == RPBFT_CONSENSUS_TYPE)
    {
        m_genesisConfig.m_epochSealerNum =
            _genesisConfig.get<std::uint32_t>("consensus.epoch_sealer_num", 4);
        m_genesisConfig.m_epochBlockNum =
            _genesisConfig.get<std::uint32_t>("consensus.epoch_block_num", 1000);
    }

    // leaderSwitchPeriod
    auto consensusLeaderPeriod = checkAndGetValue(_genesisConfig, "consensus.leader_period", "1");
    if (consensusLeaderPeriod <= 0)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Please set consensus.leader_period to positive!"));
    }
    m_ledgerConfig->setLeaderSwitchPeriod(consensusLeaderPeriod);
    NodeConfig_LOG(INFO)
        << LOG_DESC("loadLedgerConfig") << LOG_KV("consensus_type", m_genesisConfig.m_consensusType)
        << LOG_KV("block_tx_count_limit", m_ledgerConfig->blockTxCountLimit())
        << LOG_KV("gas_limit", m_genesisConfig.m_txGasLimit)
        << LOG_KV("leader_period", m_ledgerConfig->leaderSwitchPeriod())
        << LOG_KV("minSealTime", m_minSealTime)
        << LOG_KV("compatibilityVersion",
               (bcos::protocol::BlockVersion)m_genesisConfig.m_compatibilityVersion);
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
    std::sort(nodeList->begin(), nodeList->end(), bcos::consensus::ConsensusNodeComparator());
    NodeConfig_LOG(INFO) << LOG_BADGE("parseConsensusNodeList")
                         << LOG_KV("totalNodesSize", nodeList->size());
    return nodeList;
}

void NodeConfig::loadExecutorConfig(boost::property_tree::ptree const& _genesisConfig)
{
    try
    {
        m_genesisConfig.m_isWasm = _genesisConfig.get<bool>("executor.is_wasm", false);
        m_genesisConfig.m_isAuthCheck = _genesisConfig.get<bool>("executor.is_auth_check", false);
        m_genesisConfig.m_isSerialExecute =
            _genesisConfig.get<bool>("executor.is_serial_execute", false);
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "executor.is_wasm/executor.is_auth_check/"
                                  "executor.is_serial_execute is null, please set it!"));
    }
    if (m_genesisConfig.m_isWasm && !m_genesisConfig.m_isSerialExecute)
    {
        if (m_genesisConfig.m_compatibilityVersion >=
            (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                      "loadExecutorConfig wasm only support serial executing, "
                                      "please set is_serial_execute to true"));
        }
        NodeConfig_LOG(WARNING)
            << METRIC
            << LOG_DESC("loadExecutorConfig wasm with serial executing is not recommended");
    }
    if (m_genesisConfig.m_isWasm && m_genesisConfig.m_isAuthCheck)
    {
        if (m_genesisConfig.m_compatibilityVersion >=
            (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                      "loadExecutorConfig auth only support solidity, "
                                      "please set is_auth_check to false or set is_wasm to false"));
        }
        NodeConfig_LOG(WARNING) << METRIC
                                << LOG_DESC(
                                       "loadExecutorConfig wasm auth is not supported for now");
    }
    try
    {
        m_genesisConfig.m_authAdminAccount =
            _genesisConfig.get<std::string>("executor.auth_admin_account", "");
        if (m_genesisConfig.m_authAdminAccount.empty() &&
            (m_genesisConfig.m_isAuthCheck ||
                m_genesisConfig.m_compatibilityVersion >= BlockVersion::V3_3_VERSION)) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(
                InvalidConfig() << errinfo_comment("executor.auth_admin_account is empty, "
                                                   "please set correct auth_admin_account"));
        }
    }
    catch (std::exception const& e)
    {
        if (m_genesisConfig.m_isAuthCheck ||
            m_genesisConfig.m_compatibilityVersion >= BlockVersion::V3_3_VERSION)
        {
            BOOST_THROW_EXCEPTION(
                InvalidConfig() << errinfo_comment("executor.auth_admin_account is null, "
                                                   "please set correct auth_admin_account"));
        }
    }
    NodeConfig_LOG(INFO) << METRIC << LOG_DESC("loadExecutorConfig")
                         << LOG_KV("isWasm", m_genesisConfig.m_isWasm)
                         << LOG_KV("isAuthCheck", m_genesisConfig.m_isAuthCheck)
                         << LOG_KV("authAdminAccount", m_genesisConfig.m_authAdminAccount)
                         << LOG_KV("ismSerialExecute", m_genesisConfig.m_isSerialExecute);
}

// load config.ini
void NodeConfig::loadExecutorNormalConfig(boost::property_tree::ptree const& _configIni)
{
    bool enableDag = _configIni.get<bool>("executor.enable_dag", true);
    g_BCOSConfig.setEnableDAG(enableDag);
    NodeConfig_LOG(INFO) << METRIC << LOG_DESC("loadExecutorNormalConfig: config.ini")
                         << LOG_KV("enableDag", enableDag);
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

int64_t NodeConfig::checkAndGetValue(
    boost::property_tree::ptree const& _pt, std::string const& _key)
{
    try
    {
        auto value = _pt.get<std::string>(_key);
        return boost::lexical_cast<int64_t>(value);
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Invalid value for configuration " + _key +
                                               ", please set the value with a valid number"));
    }
}

bool NodeConfig::isValidPort(int port)
{
    return !(port <= 1024 || port > 65535);
}

void bcos::tool::NodeConfig::loadGenesisFeatures(boost::property_tree::ptree const& ptree)
{
    if (auto node = ptree.get_child_optional("features"))
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

std::string bcos::tool::NodeConfig::getDefaultServiceName(
    std::string const& _nodeName, std::string const& _serviceName) const
{
    return m_genesisConfig.m_chainID + "." + _nodeName + _serviceName;
}

std::string bcos::tool::generateGenesisData(
    ledger::GenesisConfig const& genesisConfig, ledger::LedgerConfig const& ledgerConfig)
{
    if (genesisConfig.m_compatibilityVersion >=
        (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
    {
        std::stringstream ss;
        ss << "[chain]" << '\n'
           << "sm_crypto:" << genesisConfig.m_smCrypto << '\n'
           << "chainID: " << genesisConfig.m_chainID << '\n'
           << "grouID: " << genesisConfig.m_groupID << '\n'
           << "[consensys]" << '\n'
           << "consensus_type: " << genesisConfig.m_consensusType << '\n'
           << "block_tx_count_limit:" << genesisConfig.m_txCountLimit << '\n'
           << "leader_period:" << genesisConfig.m_leaderSwitchPeriod << '\n'
           << "[version]" << '\n'
           << "compatibility_version:"
           << bcos::protocol::BlockVersion(genesisConfig.m_compatibilityVersion) << '\n'
           << "[tx]" << '\n'
           << "gaslimit:" << genesisConfig.m_txGasLimit << '\n'
           << "[executor]" << '\n'
           << "iswasm: " << genesisConfig.m_isWasm << '\n'
           << "isAuthCheck:" << genesisConfig.m_isAuthCheck << '\n'
           << "authAdminAccount:" << genesisConfig.m_authAdminAccount << '\n'
           << "isSerialExecute:" << genesisConfig.m_isSerialExecute << '\n';
        if (genesisConfig.m_compatibilityVersion >=
            (uint32_t)bcos::protocol::BlockVersion::V3_5_VERSION)
        {
            ss << "epochSealerNum:" << genesisConfig.m_epochSealerNum << '\n'
               << "epochBlockNum:" << genesisConfig.m_epochBlockNum << '\n';
        }
        if (!genesisConfig.m_features.empty())  // TODO: Need version check?
        {
            ss << "[features]" << '\n';
            for (const auto& feature : genesisConfig.m_features)
            {
                ss << feature.flag << ":" << feature.enable << '\n';
            }
        }

        size_t j = 0;
        for (const auto& node : ledgerConfig.consensusNodeList())
        {
            ss << "node." + boost::lexical_cast<std::string>(j) + ":" +
                      *toHexString(node->nodeID()->data()) + "," + std::to_string(node->weight()) +
                      "\n";
            ++j;
        }
        std::string genesisdata = ss.str();
        NodeConfig_LOG(INFO) << LOG_BADGE("generateGenesisData")
                             << LOG_KV("genesisData", genesisdata);

        return genesisdata;
    }

    std::stringstream executorStream;
    executorStream << genesisConfig.m_isWasm << "-" << genesisConfig.m_isAuthCheck << "-"
                   << genesisConfig.m_authAdminAccount << "-" << genesisConfig.m_isSerialExecute;

    std::stringstream ss;
    ss << ledgerConfig.blockTxCountLimit() << "-" << ledgerConfig.leaderSwitchPeriod() << "-"
       << genesisConfig.m_txGasLimit << "-"
       << protocol::BlockVersion(genesisConfig.m_compatibilityVersion) << "-"
       << executorStream.str();
    for (const auto& node : ledgerConfig.consensusNodeList())
    {
        ss << *toHexString(node->nodeID()->data()) << "," << node->weight() << ";";
    }
    auto genesisdata = ss.str();
    NodeConfig_LOG(INFO) << LOG_BADGE("generateGenesisData") << LOG_KV("genesisData", genesisdata);

    return genesisdata;
}
bcos::ledger::GenesisConfig const& bcos::tool::NodeConfig::genesisConfig() const
{
    return m_genesisConfig;
}
