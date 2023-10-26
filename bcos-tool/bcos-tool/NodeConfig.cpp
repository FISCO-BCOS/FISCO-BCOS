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
 * @author: ancelmo
 * @date 2023-10-26
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
#include <utility>

#define MAX_BLOCK_LIMIT 5000

using namespace bcos;
using namespace bcos::crypto;
using namespace bcos::tool;
using namespace bcos::consensus;
using namespace bcos::ledger;
using namespace bcos::protocol;
using namespace std::string_literals;

NodeConfig::NodeConfig(KeyFactory::Ptr _keyFactory)
  : m_keyFactory(std::move(_keyFactory)), m_ledgerConfig(std::make_shared<LedgerConfig>())
{}

void NodeConfig::loadConfig(
    toml::table const& _pt, bool _enforceMemberID, bool _enforceChainConfig, bool _enforceGroupId)
{
    // if version < 3.1.0, config.ini include chainConfig
    if (_enforceChainConfig ||
        (m_compatibilityVersion < (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION &&
            m_compatibilityVersion >= (uint32_t)bcos::protocol::BlockVersion::MIN_VERSION))
    {
        loadChainConfig(_pt, _enforceGroupId);
    }
    loadCertConfig(_pt);
    loadRpcConfig(_pt);
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

void NodeConfig::loadGenesisConfigFromString(std::string const& _content)
{
    loadGenesisConfig(toml::parse(_content));
}

void bcos::tool::NodeConfig::loadGenesisConfig(std::string const& _genesisConfigPath)
{
    loadGenesisConfig(toml::parse_file(_genesisConfigPath));
}

void NodeConfig::loadGenesisConfig(toml::table const& genesis)
{
    // if version >= 3.1.0, genesisBlock include chainConfig
    m_compatibilityVersionStr = genesis["version"]["compatibility_version"].value_or<std::string>(
        std::string(bcos::protocol::RC4_VERSION_STR));
    m_compatibilityVersion = toVersionNumber(m_compatibilityVersionStr);
    if (m_compatibilityVersion >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
    {
        loadChainConfig(genesis, true);
    }
    loadLedgerConfig(genesis);
    loadExecutorConfig(genesis);
    generateGenesisData();
}

std::string NodeConfig::getServiceName(toml::table const& _pt, std::string const& _configSection,
    std::string const& _objName, std::string const& _defaultValue, bool _require)
{
    auto serviceName = _pt[_configSection].value_or(_defaultValue);
    if (!_require)
    {
        return serviceName;
    }
    checkService(_configSection, serviceName);
    return getPrxDesc(serviceName, _objName);
}

void NodeConfig::loadRpcServiceConfig(toml::table const& _pt)
{
    // rpc service name
    m_rpcServiceName = getServiceName(_pt, "service.rpc", RPC_SERVANT_NAME);
    NodeConfig_LOG(INFO) << LOG_DESC("loadServiceConfig")
                         << LOG_KV("rpcServiceName", m_rpcServiceName);
}

void NodeConfig::loadGatewayServiceConfig(toml::table const& _pt)
{
    // gateway service name
    m_gatewayServiceName = getServiceName(_pt, "service.gateway", GATEWAY_SERVANT_NAME);
    NodeConfig_LOG(INFO) << LOG_DESC("loadServiceConfig")
                         << LOG_KV("gatewayServiceName", m_gatewayServiceName);
}
void NodeConfig::loadServiceConfig(toml::table const& _pt)
{
    loadGatewayServiceConfig(_pt);
    loadRpcServiceConfig(_pt);

    /*
    [service]
        without_tars_framework = true
        tars_proxy_conf = tars_proxy.ini
     */

    auto withoutTarsFramework = _pt["service"]["without_tars_framework"].value_or(false);
    m_withoutTarsFramework = withoutTarsFramework;

    NodeConfig_LOG(INFO) << LOG_DESC("loadServiceConfig")
                         << LOG_KV("withoutTarsFramework", m_withoutTarsFramework);

    if (m_withoutTarsFramework)
    {
        auto tarsProxyConf = _pt["service"]["tars_proxy_conf"].value_or("./tars_proxy.ini"s);
        loadTarsProxyConfig(tarsProxyConf);
    }
}

void NodeConfig::loadWithoutTarsFrameworkConfig(toml::table const& _pt)
{
    /*
        [service]
            without_tars_framework = true
            tars_proxy_conf = conf/tars_proxy.ini
         */

    auto withoutTarsFramework = _pt["service"]["without_tars_framework"].value_or(false);
    m_withoutTarsFramework = withoutTarsFramework;

    NodeConfig_LOG(INFO) << LOG_DESC("loadWithoutTarsFrameworkConfig")
                         << LOG_KV("withoutTarsFramework", m_withoutTarsFramework);
}

void NodeConfig::loadNodeServiceConfig(
    std::string const& _nodeID, toml::table const& _pt, bool _require)
{
    auto nodeName = _pt["service"]["node_name"].value_or(""s);
    if (nodeName.empty())
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

    auto withoutTarsFramework = _pt["service"]["without_tars_framework"].value_or(false);
    m_withoutTarsFramework = withoutTarsFramework;

    NodeConfig_LOG(INFO) << LOG_DESC("loadNodeServiceConfig")
                         << LOG_KV("withoutTarsFramework", m_withoutTarsFramework);

    if (m_withoutTarsFramework)
    {
        auto tarsProxyConf = _pt["service"]["tars_proxy_conf"].value_or("conf/tars_proxy.ini"s);
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

    try
    {
        auto pt = toml::parse_file(_tarsProxyConf);

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

void NodeConfig::loadServiceTarsProxyConfig(const std::string& _serviceName, toml::table const& _pt)
{
    if (auto nodeTable = _pt[_serviceName].as_table())
    {
        for (auto& node : *nodeTable)
        {
            if (!node.first.str().starts_with("proxy."))
            {
                continue;
            }

            auto data = node.second.value<std::string>();

            // string to endpoint
            tars::TC_Endpoint endpoint = bcostars::string2TarsEndPoint(*data);
            m_tarsSN2EndPoints[_serviceName].push_back(endpoint);

            NodeConfig_LOG(INFO) << LOG_BADGE("loadTarsProxyConfig") << LOG_DESC("add element")
                                 << LOG_KV("serviceName", _serviceName)
                                 << LOG_KV("endpoint", endpoint.toString());
        }
    }
    else
    {
        NodeConfig_LOG(WARNING) << LOG_BADGE("loadServiceTarsProxyConfig")
                                << LOG_DESC("service name not exist")
                                << LOG_KV("serviceName", _serviceName);
        return;
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

void NodeConfig::loadRpcConfig(toml::table const& _pt)
{
    /*
    [rpc]
        listen_ip=0.0.0.0
        listen_port=30300
        thread_count=16
        sm_ssl=false
        disable_ssl=false
    */
    auto listenIP = _pt["rpc"]["listen_ip"].value_or("0.0.0.0"s);
    auto listenPort = _pt["rpc"]["listen_port"].value_or(20200);
    auto threadCount = _pt["rpc"]["thread_count"].value_or(8);
    auto smSsl = _pt["rpc"]["sm_ssl"].value_or(false);
    auto disableSsl = _pt["rpc"]["disable_ssl"].value_or(false);
    auto needRetInput = _pt["rpc"]["return_input_params"].value_or(true);

    m_rpcListenIP = listenIP;
    m_rpcListenPort = listenPort;
    m_rpcThreadPoolSize = threadCount;
    m_rpcDisableSsl = disableSsl;
    m_rpcSmSsl = smSsl;
    g_BCOSConfig.setNeedRetInput(needRetInput);

    NodeConfig_LOG(INFO) << LOG_DESC("loadRpcConfig") << LOG_KV("listenIP", listenIP)
                         << LOG_KV("listenPort", listenPort) << LOG_KV("listenPort", listenPort)
                         << LOG_KV("smSsl", smSsl) << LOG_KV("disableSsl", disableSsl)
                         << LOG_KV("needRetInput", needRetInput);
}

void NodeConfig::loadGatewayConfig(toml::table const& _pt)
{
    /*
    [p2p]
    listen_ip=0.0.0.0
    listen_port=30300
    sm_ssl=false
    nodes_path=./
    nodes_file=nodes.json
    */

    auto listenIP = _pt["p2p"]["listen_ip"].value_or("0.0.0.0"s);
    auto listenPort = _pt["p2p"]["listen_port"].value_or(30300);
    auto nodesDir = _pt["p2p"]["nodes_path"].value_or("./"s);
    auto nodesFile = _pt["p2p"]["nodes_file"].value_or("nodes.json"s);
    auto smSsl = _pt["p2p"]["sm_ssl"].value_or(false);

    m_p2pListenIP = listenIP;
    m_p2pListenPort = listenPort;
    m_p2pNodeDir = nodesDir;
    m_p2pSmSsl = smSsl;
    m_p2pNodeFileName = nodesFile;

    NodeConfig_LOG(INFO) << LOG_DESC("loadGatewayConfig") << LOG_KV("listenIP", listenIP)
                         << LOG_KV("listenPort", listenPort) << LOG_KV("listenPort", listenPort)
                         << LOG_KV("smSsl", smSsl) << LOG_KV("nodesFile", nodesFile);
}

void NodeConfig::loadCertConfig(toml::table const& _pt)
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
    m_certPath = _pt["cert"]["ca_path"].value_or("./"s);
    auto smCaCertFile = _pt["cert"]["sm_ca_cert"].value_or("sm_ca.crt"s);
    auto smNodeCertFile = _pt["cert"]["sm_node_cert"].value_or("sm_ssl.crt"s);
    auto smNodeKeyFile = _pt["cert"]["sm_node_key"].value_or("sm_ssl.key"s);
    auto smEnNodeCertFile = _pt["cert"]["sm_ennode_cert"].value_or("sm_enssl.crt"s);
    auto smEnNodeKeyFile = _pt["cert"]["sm_ennode_key"].value_or("sm_enssl.key"s);

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
    auto caCertFile = _pt["cert"]["ca_cert"].value_or("ca.crt"s);
    auto nodeCertFile = _pt["cert"]["node_cert"].value_or("ssl.crt"s);
    auto nodeKeyFile = m_certPath + "/" + _pt["cert"]["node_key"].value_or("ssl.key"s);

    m_caCert = caCertFile;
    m_nodeCert = nodeCertFile;
    m_nodeKey = nodeKeyFile;

    NodeConfig_LOG(INFO) << LOG_DESC("loadCertConfig") << LOG_KV("ca_path", m_certPath)
                         << LOG_KV("ca_cert", caCertFile) << LOG_KV("node_cert", nodeCertFile)
                         << LOG_KV("node_key", nodeKeyFile);
}

// load the txpool related params
void NodeConfig::loadTxPoolConfig(toml::table const& _pt)
{
    m_txpoolLimit = _pt["txpool"]["limit"].value_or(15000);
    if (m_txpoolLimit <= 0)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Please set txpool.limit to positive !"));
    }
    m_txpoolLimit = _pt["txpool"]["notify_worker_num"].value_or(2);
    if (m_notifyWorkerNum <= 0)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set txpool.notify_worker_num to positive !"));
    }
    m_verifierWorkerNum = _pt["txpool"]["verify_worker_num"].value_or(
        std::min(8U, std::thread::hardware_concurrency()));
    if (m_verifierWorkerNum <= 0)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set txpool.verify_worker_num to positive !"));
    }
    // the txs expiration time, in second
    auto txsExpirationTime = _pt["txpool"]["txs_expiration_time"].value_or(600L);
    if (txsExpirationTime * 1000 <= DEFAULT_MIN_CONSENSUS_TIME_MS) [[unlikely]]
    {
        NodeConfig_LOG(WARNING) << LOG_DESC(
                                       "loadTxPoolConfig: the configured txs_expiration_time "
                                       "is smaller than default "
                                       "consensus time, reset to the consensus time")
                                << LOG_KV("txsExpirationTime(seconds)", txsExpirationTime)
                                << LOG_KV("defaultConsTime", DEFAULT_MIN_CONSENSUS_TIME_MS);
    }
    m_txsExpirationTime = std::max<int64_t>(
        {txsExpirationTime * 1000, (int64_t)DEFAULT_MIN_CONSENSUS_TIME_MS, (int64_t)m_minSealTime});

    NodeConfig_LOG(INFO) << LOG_DESC("loadTxPoolConfig") << LOG_KV("txpoolLimit", m_txpoolLimit)
                         << LOG_KV("notifierWorkers", m_notifyWorkerNum)
                         << LOG_KV("verifierWorkers", m_verifierWorkerNum)
                         << LOG_KV("txsExpirationTime(ms)", m_txsExpirationTime);
}

void NodeConfig::loadChainConfig(toml::table const& genesis, bool _enforceGroupId)
{
    try
    {
        m_smCryptoType = genesis["chain"]["sm_crypto"].value_or(false);
        if (_enforceGroupId)
        {
            m_groupId = genesis["chain"]["group_id"].value_or("group"s);
        }
        m_chainId = genesis["chain"]["chain_id"].value_or("chain"s);
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "config.genesis chain.sm_crypto/chain.group_id/chain.chain_id is "
                                  "null, please set it!"));
    }
    if (!isalNumStr(m_chainId))
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("The chainId must be number or digit"));
    }
    m_blockLimit = genesis["chain"]["block_limit"].value_or(1000LU);
    if (m_blockLimit <= 0 || m_blockLimit > MAX_BLOCK_LIMIT)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set chain.block_limit to positive and less than " +
                                  std::to_string(MAX_BLOCK_LIMIT) + " !"));
    }
    NodeConfig_LOG(INFO) << METRIC << LOG_DESC("loadChainConfig")
                         << LOG_KV("smCrypto", m_smCryptoType) << LOG_KV("chainId", m_chainId)
                         << LOG_KV("groupId", m_groupId) << LOG_KV("blockLimit", m_blockLimit);
}

void NodeConfig::loadSecurityConfig(toml::table const& _pt)
{
    m_privateKeyPath = _pt["security"]["private_key_path"].value_or("node.pem"s);
    m_enableHsm = _pt["security"]["enable_hsm"].value_or(false);
    if (m_enableHsm)
    {
        m_hsmLibPath = _pt["security"]["hsm_lib_path"].value_or("/usr/local/lib/libgmt0018.so"s);
        m_keyIndex = _pt["security"]["key_index"].value_or(0);
        m_password = _pt["security"]["password"].value_or(""s);
        NodeConfig_LOG(INFO) << LOG_DESC("loadSecurityConfig HSM")
                             << LOG_KV("lib_path", m_hsmLibPath) << LOG_KV("key_index", m_keyIndex)
                             << LOG_KV("password", m_password);
    }

    NodeConfig_LOG(INFO) << LOG_DESC("loadSecurityConfig") << LOG_KV("enable_hsm", m_enableHsm)
                         << LOG_KV("privateKeyPath", m_privateKeyPath);
}

void NodeConfig::loadSealerConfig(toml::table const& _pt)
{
    m_minSealTime = _pt["consensus"]["min_seal_time"].value_or(500);
    if (m_minSealTime <= 0 || m_minSealTime > DEFAULT_MAX_SEAL_TIME_MS)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set consensus.min_seal_time between 1 and 600000!"));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadSealerConfig") << LOG_KV("minSealTime", m_minSealTime);
}

void NodeConfig::loadStorageSecurityConfig(toml::table const& _pt)
{
    m_storageSecurityEnable = _pt["storage_security"]["enable"].value_or(false);
    if (!m_storageSecurityEnable)
    {
        return;
    }
    auto storageSecurityKeyCenterUrl = _pt["storage_security"]["key_center_url"].value_or(""s);

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

    m_storageSecurityCipherDataKey = _pt["storage_security"]["cipher_data_key"].value_or(""s);
    if (m_storageSecurityCipherDataKey.empty())
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Please provide cipher_data_key!"));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadStorageSecurityConfig")
                         << LOG_KV("keyCenterUrl", storageSecurityKeyCenterUrl);
}

void NodeConfig::loadSyncConfig(const toml::table& _pt)
{
    m_enableSendBlockStatusByTree = _pt["sync"]["sync_block_by_tree"].value_or(false);
    m_enableSendTxByTree = _pt["sync"]["send_txs_by_tree"].value_or(false);
    m_treeWidth = _pt["sync"]["tree_width"].value_or(3);
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

void NodeConfig::loadStorageConfig(toml::table const& _pt)
{
    m_storagePath = _pt["storage"]["data_path"].value_or("data/"s + m_groupId);
    m_storageType = _pt["storage"]["type"].value_or("RocksDB"s);
    m_keyPageSize = _pt["storage"]["key_page_size"].value_or(10240);
    m_maxWriteBufferNumber = _pt["storage"]["max_write_buffer_number"].value_or(4);
    m_maxBackgroundJobs = _pt["storage"]["max_background_jobs"].value_or(4);
    m_writeBufferSize = _pt["storage"]["write_buffer_size"].value_or(64 << 20);
    m_minWriteBufferNumberToMerge = _pt["storage"]["min_write_buffer_number_to_merge"].value_or(1);
    m_blockCacheSize = _pt["storage"]["block_cache_size"].value_or(128 << 20);
    m_enableDBStatistics = _pt["storage"]["enable_statistics"].value_or(false);
    m_pdCaPath = _pt["storage"]["pd_ssl_ca_path"].value_or(""s);
    m_pdCertPath = _pt["storage"]["pd_ssl_cert_path"].value_or(""s);
    m_pdKeyPath = _pt["storage"]["pd_ssl_key_path"].value_or(""s);

    m_enableArchive = _pt["storage"]["enable_archive"].value_or(false);
    if (m_enableArchive)
    {
        m_archiveListenIP = _pt["storage"]["archive_ip"].value_or(""s);
        m_archiveListenPort = _pt["storage"]["archive_port"].value_or(0);
    }

    // if (m_keyPageSize < 4096 || m_keyPageSize > (1 << 25))
    // {
    //     BOOST_THROW_EXCEPTION(
    //         InvalidConfig() << errinfo_comment("Please set storage.key_page_size in 4K~32M"));
    // }

    auto pd_addrs = _pt["storage"]["pd_addrs"].value_or("127.0.0.1:2379"s);
    boost::split(m_pd_addrs, pd_addrs, boost::is_any_of(","));
    m_enableLRUCacheStorage = _pt["storage"]["enable_cache"].value_or(true);
    m_cacheSize = _pt["storage"]["cache_size"].value_or(DEFAULT_CACHE_SIZE);
    g_BCOSConfig.setStorageType(m_storageType);  // Set storageType to global
    NodeConfig_LOG(INFO) << LOG_DESC("loadStorageConfig") << LOG_KV("storagePath", m_storagePath)
                         << LOG_KV("KeyPage", m_keyPageSize) << LOG_KV("storageType", m_storageType)
                         << LOG_KV("pdAddrs", pd_addrs) << LOG_KV("pdCaPath", m_pdCaPath)
                         << LOG_KV("enableArchive", m_enableArchive)
                         << LOG_KV("archiveListenIP", m_archiveListenIP)
                         << LOG_KV("archiveListenPort", m_archiveListenPort)
                         << LOG_KV("enableLRUCacheStorage", m_enableLRUCacheStorage);
}

// Note: In components that do not require failover, do not need to set member_id
void NodeConfig::loadFailOverConfig(toml::table const& _pt, bool _enforceMemberID)
{
    // only enable leaderElection when using tikv

    m_enableFailOver = _pt["failover"]["enable"].value_or(false);
    if (!m_enableFailOver)
    {
        return;
    }

    m_failOverClusterUrl = _pt["failover"]["cluster_url"].value_or("127.0.0.1:2379"s);

    m_memberID = _pt["failover"]["member_id"].value_or(""s);
    if (m_memberID.size() == 0 && _enforceMemberID)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Please set failover.member_id must be non-empty "));
    }

    m_leaseTTL = _pt["failover"]["lease_ttl"].value_or(DEFAULT_MIN_LEASE_TTL_SECONDS);
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

void NodeConfig::loadOthersConfig(toml::table const& _pt)
{
    m_sendTxTimeout = _pt["others"]["send_tx_timeout"].value_or(-1);

    m_vmCacheSize = _pt["executor"]["vm_cache_size"].value_or(1024);

    m_enableBaselineScheduler = _pt["executor"]["baseline_scheduler"].value_or(false);

    m_baselineSchedulerConfig.chunkSize =
        _pt["executor"]["baseline_scheduler_chunksize"].value_or(100);
    m_baselineSchedulerConfig.maxThread =
        _pt["executor"]["baseline_scheduler_maxthread"].value_or(16);
    m_baselineSchedulerConfig.parallel = _pt["executor"]["baseline_scheduler_parallel"].value_or(0);

    m_tarsRPCConfig.host = _pt["rpc"]["tars_rpc_host"].value_or("127.0.0.1"s);
    m_tarsRPCConfig.port = _pt["rpc"]["tars_rpc_port"].value_or(0);
    m_tarsRPCConfig.threadCount =
        _pt["rpc"]["tars_rpc_thread_count"].value_or(std::thread::hardware_concurrency());

    NodeConfig_LOG(INFO) << LOG_DESC("loadOthersConfig") << LOG_KV("sendTxTimeout", m_sendTxTimeout)
                         << LOG_KV("vmCacheSize", m_vmCacheSize);
}

void NodeConfig::loadConsensusConfig(toml::table const& _pt)
{
    m_checkPointTimeoutInterval =
        _pt["consensus"]["checkpoint_timeout"].value_or(DEFAULT_MIN_CONSENSUS_TIME_MS);

    m_pipelineSize = _pt["consensus"]["pipeline_size"].value_or(DEFAULT_PIPELINE_SIZE);
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

void NodeConfig::loadLedgerConfig(toml::table const& genesis)
{
    // consensus type
    try
    {
        m_consensusType = genesis["consensus"]["consensus_type"].value_or("pbft"s);
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("consensus.consensus_type is null, please set it!"));
    }
    if (m_consensusType != bcos::ledger::PBFT_CONSENSUS_TYPE &&
        m_consensusType != bcos::ledger::RPBFT_CONSENSUS_TYPE)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "consensus.consensus_type is illegal, it must be pbft or rpbft!"));
    }
    // blockTxCountLimit
    auto blockTxCountLimit = genesis["consensus"]["block_tx_count_limit"].value_or(1000LU);
    if (blockTxCountLimit <= 0)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set consensus.block_tx_count_limit to positive!"));
    }
    m_ledgerConfig->setBlockTxCountLimit(blockTxCountLimit);
    // txGasLimit
    auto txGasLimit = genesis["tx"]["gas_limit"].value_or(3000000000LU);
    if (txGasLimit <= TX_GAS_LIMIT_MIN)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "Please set tx.gas_limit to more than " + std::to_string(TX_GAS_LIMIT_MIN) + " !"));
    }

    m_txGasLimit = txGasLimit;
    // the compatibility version
    m_compatibilityVersionStr = genesis["version"]["compatibility_version"].value_or(
        std::string(bcos::protocol::RC4_VERSION_STR));
    // must call here to check the compatibility_version
    m_compatibilityVersion = toVersionNumber(m_compatibilityVersionStr);
    // sealerList
    auto consensusNodeList = parseConsensusNodeList(genesis, "consensus", "node.");
    if (!consensusNodeList || consensusNodeList->empty())
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment("Must set sealerList!"));
    }
    m_ledgerConfig->setConsensusNodeList(*consensusNodeList);

    // rpbft
    if (m_consensusType == RPBFT_CONSENSUS_TYPE)
    {
        m_epochSealerNum = genesis["consensus"]["epoch_sealer_num"].value_or(4);
        m_epochBlockNum = genesis["consensus"]["epoch_block_num"].value_or(1000);
    }

    // leaderSwitchPeriod
    auto consensusLeaderPeriod = genesis["consensus"]["leader_period"].value_or(1);
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
                         << LOG_KV("compatibilityVersion",
                                (bcos::protocol::BlockVersion)m_compatibilityVersion);
}

ConsensusNodeListPtr NodeConfig::parseConsensusNodeList(
    toml::table const& genesis, std::string const& _sectionName, std::string const& _subSectionName)
{
    std::shared_ptr<ConsensusNodeList> nodeList;
    if (const auto* nodeTable = genesis[_sectionName].as_table())
    {
        nodeList = std::make_shared<ConsensusNodeList>();
        for (auto& node : *nodeTable)
        {
            if (!node.first.str().starts_with(_subSectionName))
            {
                continue;
            }

            auto nodeString = node.second.value<std::string>();
            std::vector<std::string> nodeInfo;
            boost::split(nodeInfo, *nodeString, boost::is_any_of(":"));
            if (nodeInfo.empty())
            {
                BOOST_THROW_EXCEPTION(
                    InvalidConfig()
                    << errinfo_comment("Uninitialized nodeInfo, key: "s +
                                       std::string(node.first.str()) + ", value: " + *nodeString));
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
    }
    else
    {
        NodeConfig_LOG(DEBUG) << LOG_DESC("parseConsensusNodeList return for empty config")
                              << LOG_KV("sectionName", _sectionName);
        return nullptr;
    }

    // only sort nodeList after rc3 version
    std::sort(nodeList->begin(), nodeList->end(), bcos::consensus::ConsensusNodeComparator());
    NodeConfig_LOG(INFO) << LOG_BADGE("parseConsensusNodeList")
                         << LOG_KV("totalNodesSize", nodeList->size());
    return nodeList;
}

void NodeConfig::generateGenesisData()
{
    std::string versionData;
    std::string executorConfig;
    if (m_compatibilityVersion >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
    {
        auto genesisData = std::make_shared<bcos::ledger::GenesisConfig>(m_smCryptoType, m_chainId,
            m_groupId, m_consensusType, m_ledgerConfig->blockTxCountLimit(),
            m_ledgerConfig->leaderSwitchPeriod(), m_compatibilityVersionStr, m_txGasLimit, m_isWasm,
            m_isAuthCheck, m_authAdminAddress, m_isSerialExecute, m_epochSealerNum,
            m_epochBlockNum);
        auto genesisdata = genesisData->genesisDataOutPut();
        size_t j = 0;
        for (const auto& node : m_ledgerConfig->consensusNodeList())
        {
            genesisdata += "node." + boost::lexical_cast<std::string>(j) + ":" +
                           *toHexString(node->nodeID()->data()) + "," +
                           std::to_string(node->weight()) + "\n";
            ++j;
        }
        NodeConfig_LOG(INFO) << LOG_BADGE("generateGenesisData")
                             << LOG_KV("genesisData", genesisdata);
        m_genesisData = std::move(genesisdata);
        return;
    }

    versionData = m_compatibilityVersionStr + "-";
    std::stringstream ss;
    ss << m_isWasm << "-" << m_isAuthCheck << "-" << m_authAdminAddress << "-" << m_isSerialExecute;
    executorConfig = ss.str();

    ss.str("");
    ss << m_ledgerConfig->blockTxCountLimit() << "-" << m_ledgerConfig->leaderSwitchPeriod() << "-"
       << m_txGasLimit << "-" << versionData << executorConfig;
    for (const auto& node : m_ledgerConfig->consensusNodeList())
    {
        ss << *toHexString(node->nodeID()->data()) << "," << node->weight() << ";";
    }
    m_genesisData = ss.str();
    NodeConfig_LOG(INFO) << LOG_BADGE("generateGenesisData")
                         << LOG_KV("genesisData", m_genesisData);
}

void NodeConfig::loadExecutorConfig(toml::table const& _genesisConfig)
{
    try
    {
        m_isWasm = _genesisConfig["executor"]["is_wasm"].value_or(false);
        m_isAuthCheck = _genesisConfig["executor"]["is_auth_check"].value_or(false);
        m_isSerialExecute = _genesisConfig["executor"]["is_serial_execute"].value_or(false);
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "executor.is_wasm/executor.is_auth_check/"
                                  "executor.is_serial_execute is null, please set it!"));
    }
    if (m_isWasm && !m_isSerialExecute)
    {
        if (m_compatibilityVersion >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                      "loadExecutorConfig wasm only support serial executing, "
                                      "please set is_serial_execute to true"));
        }
        NodeConfig_LOG(WARNING)
            << METRIC
            << LOG_DESC("loadExecutorConfig wasm with serial executing is not recommended");
    }
    if (m_isWasm && m_isAuthCheck)
    {
        if (m_compatibilityVersion >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
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
        m_authAdminAddress = _genesisConfig["executor"]["auth_admin_account"].value_or(""s);
        if (m_authAdminAddress.empty() &&
            (m_isAuthCheck || m_compatibilityVersion >= BlockVersion::V3_3_VERSION)) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(
                InvalidConfig() << errinfo_comment("executor.auth_admin_account is empty, "
                                                   "please set correct auth_admin_account"));
        }
    }
    catch (std::exception const& e)
    {
        if (m_isAuthCheck || m_compatibilityVersion >= BlockVersion::V3_3_VERSION)
        {
            BOOST_THROW_EXCEPTION(
                InvalidConfig() << errinfo_comment("executor.auth_admin_account is null, "
                                                   "please set correct auth_admin_account"));
        }
    }
    NodeConfig_LOG(INFO) << METRIC << LOG_DESC("loadExecutorConfig") << LOG_KV("isWasm", m_isWasm)
                         << LOG_KV("isAuthCheck", m_isAuthCheck)
                         << LOG_KV("authAdminAccount", m_authAdminAddress)
                         << LOG_KV("ismSerialExecute", m_isSerialExecute);
}

// load config.ini
void NodeConfig::loadExecutorNormalConfig(toml::table const& _configIni)
{
    bool enableDag = _configIni["executor"]["enable_dag"].value_or(true);
    g_BCOSConfig.setEnableDAG(enableDag);
    NodeConfig_LOG(INFO) << METRIC << LOG_DESC("loadExecutorNormalConfig: config.ini")
                         << LOG_KV("enableDag", enableDag);
}

bool NodeConfig::isValidPort(int port)
{
    if (port <= 1024 || port > 65535)
        return false;
    return true;
}
