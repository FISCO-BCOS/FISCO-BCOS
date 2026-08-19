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
#include "bcos-framework/security/KeyEncryptionType.h"
#include "bcos-framework/security/StorageEncryptionType.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include "fisco-bcos-tars-service/Common/TarsUtils.h"
#include <bcos-framework/ledger/GenesisConfig.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <util/tc_clientsocket.h>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <set>
#include <string_view>
#include <thread>
#include <utility>

constexpr static auto MAX_BLOCK_LIMIT = 5000;

using namespace bcos;
using namespace bcos::crypto;
using namespace bcos::tool;
using namespace bcos::consensus;
using namespace bcos::ledger;
using namespace bcos::protocol;

namespace
{
// Trust-boundary helpers for [alloc.N] parsing. Functions are camelCase; no
// members so no m_ prefix.
bool isHex(std::string_view sv)
{
    return !sv.empty() &&
           std::all_of(sv.begin(), sv.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

// Require that `value` (the raw config value of `field` in `section`) is a
// 0x-prefixed hex string. If expectedLen > 0, the hex body (after 0x) must be
// exactly that many chars; otherwise it must merely be valid hex of even
// length when `evenLength` is set. Throws InvalidConfig naming section+field.
void requireHexField(std::string const& section, std::string const& field, std::string const& value,
    size_t expectedLen, bool evenLength)
{
    if (value.rfind("0x", 0) != 0)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "[" + section + "]." + field + " must be 0x-prefixed: " + value));
    }
    std::string_view body{value};
    body.remove_prefix(2);
    if (expectedLen > 0 && body.size() != expectedLen)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "[" + section + "]." + field + " must be " +
                                  std::to_string(expectedLen) + " hex chars: " + value));
    }
    if (!isHex(body))
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "[" + section + "]." + field + " is not valid hex: " + value));
    }
    if (evenLength && (body.size() % 2) != 0)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "[" + section + "]." + field + " must be even-length hex: " + value));
    }
}

void requireDecimalField(
    std::string const& section, std::string const& field, std::string const& value)
{
    if (value.empty() || !std::all_of(value.begin(), value.end(),
                             [](unsigned char c) { return std::isdigit(c) != 0; }))
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "[" + section + "]." + field + " must be decimal digits: " + value));
    }
}

// Parses a config value as int64 with a default; throws InvalidConfig on a
// non-numeric value.
int64_t checkAndGetValue(boost::property_tree::ptree const& _pt, std::string const& _key,
    std::string const& _defaultValue)
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
}  // namespace

NodeConfig::NodeConfig(KeyFactory::Ptr _keyFactory)
  : keyFactory(std::move(_keyFactory)), ledgerConfig(std::make_shared<LedgerConfig>())
{}

NodeConfig::NodeConfig() : ledgerConfig(std::make_shared<LedgerConfig>()) {}

void NodeConfig::loadConfig(std::string const& _configPath, bool _enforceMemberID,
    bool enforceChainConfig, bool enforceGroupId)
{
    boost::property_tree::ptree iniConfig;
    boost::property_tree::read_ini(_configPath, iniConfig);
    loadConfig(iniConfig, _enforceMemberID, enforceChainConfig, enforceGroupId);
}

void NodeConfig::loadGenesisConfig(std::string const& _genesisConfigPath)
{
    boost::property_tree::ptree genesisConfig;
    boost::property_tree::read_ini(_genesisConfigPath, genesisConfig);
    loadGenesisConfig(genesisConfig);
}

void NodeConfig::loadConfigFromString(std::string const& _content)
{
    boost::property_tree::ptree iniConfig;
    std::stringstream contentStream(_content);
    boost::property_tree::read_ini(contentStream, iniConfig);
    loadConfig(iniConfig);
}

void NodeConfig::loadGenesisConfigFromString(std::string const& _content)
{
    boost::property_tree::ptree genesisConfig;
    std::stringstream contentStream(_content);
    boost::property_tree::read_ini(contentStream, genesisConfig);
    loadGenesisConfig(genesisConfig);
}

void NodeConfig::loadConfig(boost::property_tree::ptree const& _pt, bool _enforceMemberID,
    bool _enforceChainConfig, bool _enforceGroupId)
{
    // if version < 3.1.0, config.ini include chainConfig
    if (_enforceChainConfig || (genesisConfig.m_compatibilityVersion <
                                       (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION &&
                                   genesisConfig.m_compatibilityVersion >=
                                       (uint32_t)bcos::protocol::BlockVersion::MIN_VERSION))
    {
        loadChainConfig(_pt, _enforceGroupId);
    }
    loadCertConfig(_pt);
    loadRpcConfig(_pt);
    loadWeb3RpcConfig(_pt);
    loadOpEngineRpcConfig(_pt);
    loadGatewayConfig(_pt);
    loadSealerConfig(_pt);
    loadSingleNodeConsensusConfig(_pt);
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
    genesisConfig.m_compatibilityVersion = toVersionNumber(compatibilityVersion);
    if (genesisConfig.m_compatibilityVersion >=
        (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
    {
        loadChainConfig(_genesisConfig, true);
    }
    loadWeb3ChainConfig(_genesisConfig);
    loadGenesisFeatures(_genesisConfig);

    loadLedgerConfig(_genesisConfig);
    loadExecutorConfig(_genesisConfig);

    // === A6.5: L2 genesis allocs; L2 mode is gated by feature_l2_ethereum_compat ===
    loadAllocs(_genesisConfig);
    // === A3: B0 full Ethereum genesis header from the merged genesis artifact ===
    loadEthGenesisHeader(_genesisConfig);
    validateL2Invariants();
}

void NodeConfig::loadAllocs(boost::property_tree::ptree const& _genesisConfig)
{
    // NOTE on boost read_ini representation (verified empirically):
    // [alloc.0] becomes a FLAT top-level ptree key literally named "alloc.0"
    // (NOT nested alloc->0), and [alloc.0.storage] is a separate flat key
    // "alloc.0.storage". Because get_child treats '.' as a path separator,
    // the storage sub-tree must be fetched with a NUL ('\0') path separator
    // so the literal dotted key is matched.
    genesisConfig.m_allocs.clear();
    std::set<std::string> seen;
    for (auto const& kv : _genesisConfig)
    {
        if (kv.first.rfind("alloc.", 0) != 0)
        {
            continue;
        }
        if (kv.first.find(".storage") != std::string::npos)
        {
            continue;
        }
        ledger::Alloc alloc;
        try
        {
            alloc.address = kv.second.get<std::string>("address");
            std::transform(alloc.address.begin(), alloc.address.end(), alloc.address.begin(),
                [](unsigned char c) { return std::tolower(c); });
            requireHexField(kv.first, "address", alloc.address, 40, false);
            if (!seen.insert(alloc.address).second)
            {
                BOOST_THROW_EXCEPTION(
                    InvalidConfig()
                    << errinfo_comment("[" + kv.first + "].address duplicate: " + alloc.address));
            }
            auto balance = kv.second.get<std::string>("balance", "0");
            requireDecimalField(kv.first, "balance", balance);
            alloc.balance = u256(balance);
            alloc.nonce = kv.second.get<std::string>("nonce", "0");
            requireDecimalField(kv.first, "nonce", alloc.nonce);
            // nonce is serialized into the genesis allocs hash as a uint64
            // big-endian field; reject anything that does not fit uint64 here so
            // the operator gets a clear error instead of a silent truncation /
            // parse failure at hashing time. requireDecimalField already pinned
            // the value to decimal digits, so the only remaining failure is
            // overflow, which lexical_cast reports via bad_lexical_cast.
            try
            {
                boost::lexical_cast<uint64_t>(alloc.nonce);
            }
            catch (boost::bad_lexical_cast const&)
            {
                BOOST_THROW_EXCEPTION(
                    InvalidConfig() << errinfo_comment(
                        "[" + kv.first + "].nonce must fit in uint64: " + alloc.nonce));
            }
            alloc.code = kv.second.get<std::string>("code", "");
            // An EOA alloc (no deployed code) is represented by an empty `code`.
            // importGenesisState / computeGenesisStateTrie already treat an empty
            // code as "no code", so only a non-empty value is validated here.
            if (!alloc.code.empty())
            {
                requireHexField(kv.first, "code", alloc.code, 0, true);
            }
            // storage section is a sibling flat key "<alloc.N>.storage"; '\0'
            // separator avoids boost interpreting the dots as a nested path.
            boost::property_tree::ptree::path_type storagePath(kv.first + ".storage", '\0');
            if (auto storageNode = _genesisConfig.get_child_optional(storagePath))
            {
                for (auto const& slot : *storageNode)
                {
                    requireHexField(kv.first + ".storage", "key", slot.first, 64, false);
                    requireHexField(kv.first + ".storage", "value", slot.second.data(), 64, false);
                    alloc.storage.emplace_back(slot.first, slot.second.data());
                }
            }
        }
        catch (InvalidConfig const&)
        {
            // already names the offending section + field; surface as-is.
            throw;
        }
        catch (std::exception const& e)
        {
            // boost ptree (missing key, bad data) and u256 parse failures land
            // here; name the failing [alloc.N] section so the operator can find it.
            BOOST_THROW_EXCEPTION(
                InvalidConfig() << errinfo_comment("[" + kv.first + "] malformed: " + e.what()));
        }
        NodeConfig_LOG(INFO) << LOG_DESC("loadAllocs") << LOG_KV("section", kv.first)
                             << LOG_KV("address", alloc.address)
                             << LOG_KV("storageSlots", alloc.storage.size());
        genesisConfig.m_allocs.push_back(std::move(alloc));
    }
}

void NodeConfig::loadEthGenesisHeader(boost::property_tree::ptree const& _genesisConfig)
{
    genesisConfig.m_ethGenesisHeader.reset();
    auto section = _genesisConfig.get_child_optional("eth_genesis_header");
    if (!section)
    {
        return;  // legacy / non-L2 chain: no Ethereum genesis header
    }

    constexpr std::string_view sectionName = "eth_genesis_header";
    // Every field of the artifact header is REQUIRED — a missing key means the
    // artifact-to-config conversion is broken, and a defaulted field would
    // silently change the genesis hash. Fail on the first missing key.
    auto requireField = [&](std::string const& key) -> std::string {
        auto value = section->get_optional<std::string>(key);
        if (!value || value->empty())
        {
            BOOST_THROW_EXCEPTION(
                InvalidConfig() << errinfo_comment("[" + std::string(sectionName) + "]." + key +
                                                   " is required (all 22 eth genesis header "
                                                   "fields must come from the genesis artifact)"));
        }
        return *value;
    };
    auto hashField = [&](std::string const& key) -> crypto::HashType {
        auto value = requireField(key);
        requireHexField(std::string(sectionName), key, value, 64, false);
        return crypto::HashType(value);
    };
    auto quantityField = [&](std::string const& key) -> u256 {
        auto value = requireField(key);
        requireHexField(std::string(sectionName), key, value, 0, false);
        // u256's fixed-width backend silently truncates over-wide input; cap
        // the hex body at 64 chars (32 bytes) so an oversized quantity is
        // named here instead of surfacing as a baffling hash mismatch.
        if (value.size() > 2 + 64)
        {
            BOOST_THROW_EXCEPTION(
                InvalidConfig() << errinfo_comment(
                    "[" + std::string(sectionName) + "]." + key + " exceeds 32 bytes: " + value));
        }
        return u256(value);
    };

    ledger::EthGenesisHeader header;
    header.m_parentHash = hashField("parent_hash");
    header.m_sha3Uncles = hashField("sha3_uncles");
    auto miner = requireField("miner");
    requireHexField(std::string(sectionName), "miner", miner, 40, false);
    header.m_miner = Address(miner);
    header.m_stateRoot = hashField("state_root");
    header.m_transactionsRoot = hashField("transactions_root");
    header.m_receiptsRoot = hashField("receipts_root");
    auto logsBloom = requireField("logs_bloom");
    requireHexField(std::string(sectionName), "logs_bloom", logsBloom, 512, false);
    header.m_logsBloom = fromHex(logsBloom);
    header.m_difficulty = quantityField("difficulty");
    auto number = quantityField("number");
    if (number != 0)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "[eth_genesis_header].number must be 0x0 (genesis block)"));
    }
    header.m_number = 0;
    header.m_gasLimit = quantityField("gas_limit");
    header.m_gasUsed = quantityField("gas_used");
    auto timestamp = quantityField("timestamp");
    if (timestamp > u256(std::numeric_limits<int64_t>::max()))
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("[eth_genesis_header].timestamp exceeds int64"));
    }
    header.m_timestamp = static_cast<int64_t>(timestamp);
    auto extraData = requireField("extra_data");
    requireHexField(std::string(sectionName), "extra_data", extraData, 0, true);
    header.m_extraData = fromHex(extraData);
    header.m_mixHash = hashField("mix_hash");
    auto nonce = requireField("nonce");
    requireHexField(std::string(sectionName), "nonce", nonce, 16, false);
    header.m_nonce = h64(nonce);
    header.m_baseFeePerGas = quantityField("base_fee_per_gas");
    header.m_withdrawalsRoot = hashField("withdrawals_root");
    header.m_blobGasUsed = quantityField("blob_gas_used");
    header.m_excessBlobGas = quantityField("excess_blob_gas");
    header.m_parentBeaconBlockRoot = hashField("parent_beacon_block_root");
    header.m_requestsHash = hashField("requests_hash");
    // The artifact's own hash claim. Ledger recomputes keccak256(rlp(header))
    // from the 21 fields above and refuses to build genesis on mismatch, so a
    // stale or hand-edited section cannot silently mint a different B0.
    header.m_hash = hashField("hash");

    genesisConfig.m_ethGenesisHeader = std::move(header);
    NodeConfig_LOG(INFO) << LOG_DESC("loadEthGenesisHeader")
                         << LOG_KV("hash", genesisConfig.m_ethGenesisHeader->m_hash.hex())
                         << LOG_KV("timestamp", genesisConfig.m_ethGenesisHeader->m_timestamp);
}

void NodeConfig::validateL2Invariants()
{
    auto const& genesis = genesisConfig;
    // L2 mode is signalled by the feature_l2_ethereum_compat flag in [features];
    // there is no separate chain_mode. allocs and the flag must agree.
    bool l2Enabled = std::any_of(genesis.m_features.begin(), genesis.m_features.end(),
        [](ledger::FeatureSet const& featureSet) {
            return featureSet.flag == ledger::Features::Flag::feature_l2_ethereum_compat &&
                   featureSet.enable > 0;
        });
    if (l2Enabled && genesis.m_allocs.empty())
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "feature_l2_ethereum_compat requires a non-empty [alloc.*] "
                                  "section in config.genesis"));
    }
    if (!l2Enabled && !genesis.m_allocs.empty())
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "[alloc.*] section requires feature_l2_ethereum_compat enabled "
                                  "in [features]"));
    }
    // The Ethereum B0 header and L2 mode are bound both ways: a pbft chain
    // with an [eth_genesis_header] section is a mis-assembled config, and an
    // L2 chain WITHOUT the section would mint a Tars-hashed B0 that no
    // op-node/op-reth can ever match — fail fast in both directions.
    if (!l2Enabled && genesis.m_ethGenesisHeader.has_value())
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("[eth_genesis_header] section requires "
                                               "feature_l2_ethereum_compat enabled in [features]"));
    }
    if (l2Enabled && !genesis.m_ethGenesisHeader.has_value())
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "feature_l2_ethereum_compat requires an [eth_genesis_header] section in "
                "config.genesis (all 22 fields from the merged genesis artifact); an L2 chain "
                "without it would build a non-Ethereum genesis block"));
    }
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
    service.rpcServiceName = getServiceName(_pt, "service.rpc", RPC_SERVANT_NAME);
    NodeConfig_LOG(INFO) << LOG_DESC("loadServiceConfig")
                         << LOG_KV("rpcServiceName", service.rpcServiceName);
}

void NodeConfig::loadGatewayServiceConfig(boost::property_tree::ptree const& _pt)
{
    // gateway service name
    service.gatewayServiceName = getServiceName(_pt, "service.gateway", GATEWAY_SERVANT_NAME);
    NodeConfig_LOG(INFO) << LOG_DESC("loadServiceConfig")
                         << LOG_KV("gatewayServiceName", service.gatewayServiceName);
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
    service.withoutTarsFramework = withoutTarsFramework;

    NodeConfig_LOG(INFO) << LOG_DESC("loadServiceConfig")
                         << LOG_KV("withoutTarsFramework", service.withoutTarsFramework);

    if (service.withoutTarsFramework)
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
    service.withoutTarsFramework = withoutTarsFramework;

    NodeConfig_LOG(INFO) << LOG_DESC("loadWithoutTarsFrameworkConfig")
                         << LOG_KV("withoutTarsFramework", service.withoutTarsFramework);
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
    service.withoutTarsFramework = withoutTarsFramework;

    NodeConfig_LOG(INFO) << LOG_DESC("loadNodeServiceConfig")
                         << LOG_KV("withoutTarsFramework", service.withoutTarsFramework);

    if (service.withoutTarsFramework)
    {
        std::string tarsProxyConf =
            _pt.get<std::string>("service.tars_proxy_conf", "conf/tars_proxy.ini");
        loadTarsProxyConfig(tarsProxyConf);
    }

    service.nodeName = nodeName;
    service.schedulerServiceName = getServiceName(_pt, "service.scheduler", SCHEDULER_SERVANT_NAME,
        getDefaultServiceName(nodeName, SCHEDULER_SERVICE_NAME), _require);
    service.executorServiceName = getServiceName(_pt, "service.executor", EXECUTOR_SERVANT_NAME,
        getDefaultServiceName(nodeName, EXECUTOR_SERVICE_NAME), _require);
    service.txpoolServiceName = getServiceName(_pt, "service.txpool", TXPOOL_SERVANT_NAME,
        getDefaultServiceName(nodeName, TXPOOL_SERVICE_NAME), _require);

    NodeConfig_LOG(INFO) << LOG_DESC("load node service") << LOG_KV("nodeName", service.nodeName)
                         << LOG_KV("withoutTarsFramework", service.withoutTarsFramework)
                         << LOG_KV("schedulerServiceName", service.schedulerServiceName)
                         << LOG_KV("executorServiceName", service.executorServiceName);
}

void NodeConfig::loadTarsProxyConfig(const std::string& _tarsProxyConf)
{
    if (!service.tarsSN2EndPoints.empty())
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
                             << LOG_KV("tars service endpoints size", service.tarsSN2EndPoints.size());
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
        service.tarsSN2EndPoints[_serviceName].push_back(endpoint);

        NodeConfig_LOG(INFO) << LOG_BADGE("loadTarsProxyConfig") << LOG_DESC("add element")
                             << LOG_KV("serviceName", _serviceName)
                             << LOG_KV("endpoint", endpoint.toString());
    }

    NodeConfig_LOG(INFO) << LOG_BADGE("loadTarsProxyConfig") << LOG_KV("serviceName", _serviceName)
                         << LOG_KV("endpoints size", service.tarsSN2EndPoints[_serviceName].size());
}

//
void NodeConfig::getTarsClientProxyEndpoints(
    const std::string& _clientPrx, std::vector<tars::TC_Endpoint>& _endpoints)
{
    if (!service.withoutTarsFramework)
    {
        NodeConfig_LOG(TRACE) << LOG_BADGE("getTarsClientProxyEndpoints")
                              << "not work with tars rpc"
                              << LOG_KV("withoutTarsFramework", service.withoutTarsFramework);
        return;
    }

    _endpoints.clear();

    auto it = service.tarsSN2EndPoints.find(boost::to_lower_copy(_clientPrx));
    if (it != service.tarsSN2EndPoints.end())
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
    rpc.load(_pt);
    bool needRetInput = _pt.get<bool>("rpc.return_input_params", true);
    g_BCOSConfig.setNeedRetInput(needRetInput);
    NodeConfig_LOG(INFO) << LOG_DESC("loadRpcConfig") << LOG_KV("listenIP", rpc.listenIP)
                         << LOG_KV("listenPort", rpc.listenPort) << LOG_KV("smSsl", rpc.smSsl)
                         << LOG_KV("disableSsl", rpc.disableSsl)
                         << LOG_KV("needRetInput", needRetInput);
}

void NodeConfig::loadWeb3RpcConfig(boost::property_tree::ptree const& _pt)
{
    web3Rpc.load(_pt);
}

void NodeConfig::loadOpEngineRpcConfig(boost::property_tree::ptree const& _pt)
{
    opEngineRpc.load(_pt);
    // Mutual-exclusion check, symmetric with loadSingleNodeConsensusConfig: whichever of the
    // two loaders runs second fires the guard, so it holds regardless of loadConfig's loader
    // order and also when a loader is invoked on its own.
    if (opEngineRpc.enable && singleNodeConsensus.enable)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "consensus.enable_single_node_consensus and op_engine_rpc.enable are mutually "
                "exclusive: both drive the same EngineService; enable at most one"));
    }
}

void NodeConfig::loadGatewayConfig(boost::property_tree::ptree const& _pt)
{
    gateway.load(_pt);
}

void NodeConfig::loadCertConfig(boost::property_tree::ptree const& _pt)
{
    cert.load(_pt);
}

// load the txpool related params
void NodeConfig::loadTxPoolConfig(boost::property_tree::ptree const& _pt)
{
    txpool.load(_pt, sealer.minSealTime);
}

void NodeConfig::loadChainConfig(boost::property_tree::ptree const& _pt, bool _enforceGroupId)
{
    try
    {
        genesisConfig.m_smCrypto = _pt.get<bool>("chain.sm_crypto", false);
        if (_enforceGroupId)
        {
            genesisConfig.m_groupID = _pt.get<std::string>("chain.group_id", "group");
        }
        genesisConfig.m_chainID = _pt.get<std::string>("chain.chain_id", "chain");
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "chain.sm_crypto/chain.group_id/chain.chain_id is null, please set it,"
                " if compatibility_version in genesis block >= 3.1.0,"
                " 'chain' config should appear in config.genesis, else in config.ini."));
    }
    if (!isalNumStr(genesisConfig.m_chainID))
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("The chainId must be number or digit"));
    }
    chain.load(_pt);
    NodeConfig_LOG(INFO) << METRIC << LOG_DESC("loadChainConfig")
                         << LOG_KV("smCrypto", genesisConfig.m_smCrypto)
                         << LOG_KV("chainId", genesisConfig.m_chainID)
                         << LOG_KV("groupId", genesisConfig.m_groupID)
                         << LOG_KV("blockLimit", chain.blockLimit);
}

void NodeConfig::loadWeb3ChainConfig(boost::property_tree::ptree const& _pt)
{
    genesisConfig.m_web3ChainID = _pt.get<std::string>("web3.chain_id", "0");
    if (!isNumStr(genesisConfig.m_web3ChainID))
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("The web3ChainId must be number string"));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadWeb3ChainConfig")
                         << LOG_KV("web3ChainID", genesisConfig.m_web3ChainID);
}

void NodeConfig::loadSecurityConfig(boost::property_tree::ptree const& _pt)
{
    security.load(_pt);
}

void NodeConfig::loadSealerConfig(boost::property_tree::ptree const& _pt)
{
    sealer.load(_pt);
}

void NodeConfig::loadSingleNodeConsensusConfig(boost::property_tree::ptree const& _pt)
{
    singleNodeConsensus.load(_pt);
    // Mutual exclusion with [op_engine_rpc].enable: the built-in single-node driver and an
    // external op-node would both drive the same EngineService forkchoice/payload state.
    // Refuse the combination at startup instead of leaving two block producers reachable by
    // configuration. The check is symmetric (loadOpEngineRpcConfig carries the same guard),
    // so it does not depend on the order the two loaders run in loadConfig.
    if (singleNodeConsensus.enable && opEngineRpc.enable)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "consensus.enable_single_node_consensus and op_engine_rpc.enable are mutually "
                "exclusive: both drive the same EngineService; enable at most one"));
    }
}

void NodeConfig::loadStorageSecurityConfig(boost::property_tree::ptree const& _pt)
{
    storageSecurity.load(_pt);
}

void NodeConfig::loadSyncConfig(const boost::property_tree::ptree& _pt)
{
    sync.load(_pt);
}

void NodeConfig::loadStorageConfig(boost::property_tree::ptree const& _pt)
{
    storage.load(_pt, genesisConfig.m_groupID);
    g_BCOSConfig.setStorageType(storage.type);  // Set storageType to global
}

// Note: In components that do not require failover, do not need to set member_id
void NodeConfig::loadFailOverConfig(boost::property_tree::ptree const& _pt, bool _enforceMemberID)
{
    failOver.load(_pt, _enforceMemberID);
}

void NodeConfig::loadOthersConfig(boost::property_tree::ptree const& _pt)
{
    executor.load(_pt);
    threadPool.load(_pt);
    tarsRPC.load(_pt);
    others.load(_pt);
}

void NodeConfig::loadConsensusConfig(boost::property_tree::ptree const& _pt)
{
    consensus.load(_pt);
}

void NodeConfig::loadLedgerConfig(boost::property_tree::ptree const& _genesisConfig)
{
    // consensus type
    try
    {
        genesisConfig.m_consensusType =
            _genesisConfig.get<std::string>("consensus.consensus_type", "pbft");
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("consensus.consensus_type is null, please set it!"));
    }
    if (genesisConfig.m_consensusType != bcos::ledger::PBFT_CONSENSUS_TYPE &&
        genesisConfig.m_consensusType != bcos::ledger::RPBFT_CONSENSUS_TYPE)
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
    ledgerConfig->setBlockTxCountLimit(blockTxCountLimit);
    genesisConfig.m_txCountLimit = blockTxCountLimit;

    // txGasLimit
    auto txGasLimit = checkAndGetValue(_genesisConfig, "tx.gas_limit", "3000000000");
    if (txGasLimit <= TX_GAS_LIMIT_MIN)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "Please set tx.gas_limit to more than " + std::to_string(TX_GAS_LIMIT_MIN) + " !"));
    }
    else if (txGasLimit < 100000)
    {
        // Small block gas limits (>= MIN, < 100000) are accepted for Ethereum
        // compatibility (EEST lowGasLimit fixtures use 80000), but flag them so an
        // operator setting a tiny limit by accident sees it in the boot log.
        NodeConfig_LOG(WARNING) << LOG_DESC("low tx.gas_limit") << LOG_KV("gasLimit", txGasLimit);
    }
    genesisConfig.m_txGasLimit = txGasLimit;
    // txGasPrice (base fee per gas; consumed by the v2 Ethereum executor as base_fee).
    // Seeded into SYS_CONFIG/tx_gas_price at genesis so EEST fixtures can reproduce their
    // environment's currentBaseFee.
    genesisConfig.m_txGasPrice = _genesisConfig.get<std::string>("tx.gas_price", "0x0");
    // txExcessBlobGas (EIP-4844 blob base-fee state; consumed by the v2 Ethereum executor).
    // Seeded into SYS_CONFIG/excess_blob_gas at genesis so EEST fixtures can reproduce their
    // environment's currentExcessBlobGas.
    auto excessBlobGasStr = _genesisConfig.get<std::string>("tx.excess_blob_gas", "");
    if (!excessBlobGasStr.empty())
    {
        genesisConfig.m_excessBlobGas = boost::lexical_cast<uint64_t>(excessBlobGasStr);
    }
    // the compatibility version
    auto compatibilityVersion = _genesisConfig.get<std::string>(
        "version.compatibility_version", bcos::protocol::RC4_VERSION_STR);
    // must call here to check the compatibility_version
    genesisConfig.m_compatibilityVersion = toVersionNumber(compatibilityVersion);
    // sealerList
    auto consensusNodeList = parseConsensusNodeList(_genesisConfig, "consensus", "node.");
    if (consensusNodeList.empty())
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment("Must set sealerList!"));
    }
    ledgerConfig->setConsensusNodeList(consensusNodeList);

    // rpbft
    if (genesisConfig.m_consensusType == RPBFT_CONSENSUS_TYPE)
    {
        genesisConfig.m_epochSealerNum =
            _genesisConfig.get<std::uint32_t>("consensus.epoch_sealer_num", 4);
        genesisConfig.m_epochBlockNum =
            _genesisConfig.get<std::uint32_t>("consensus.epoch_block_num", 1000);
    }

    // leaderSwitchPeriod
    auto consensusLeaderPeriod = checkAndGetValue(_genesisConfig, "consensus.leader_period", "1");
    if (consensusLeaderPeriod <= 0)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Please set consensus.leader_period to positive!"));
    }
    ledgerConfig->setLeaderSwitchPeriod(consensusLeaderPeriod);
    NodeConfig_LOG(INFO)
        << LOG_DESC("loadLedgerConfig") << LOG_KV("consensus_type", genesisConfig.m_consensusType)
        << LOG_KV("block_tx_count_limit", ledgerConfig->blockTxCountLimit())
        << LOG_KV("gas_limit", genesisConfig.m_txGasLimit)
        << LOG_KV("leader_period", ledgerConfig->leaderSwitchPeriod())
        << LOG_KV("minSealTime", sealer.minSealTime)
        << LOG_KV("compatibilityVersion",
               (bcos::protocol::BlockVersion)genesisConfig.m_compatibilityVersion);
}

ConsensusNodeList NodeConfig::parseConsensusNodeList(boost::property_tree::ptree const& _pt,
    std::string const& _sectionName, std::string const& _subSectionName)
{
    if (!_pt.get_child_optional(_sectionName))
    {
        NodeConfig_LOG(DEBUG) << LOG_DESC("parseConsensusNodeList return for empty config")
                              << LOG_KV("sectionName", _sectionName);
        return {};
    }
    ConsensusNodeList nodeList;
    for (auto const& it : _pt.get_child(_sectionName))
    {
        if (!it.first.starts_with(_subSectionName))
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
        int64_t voteWeight = 1;
        int64_t termWeight = 0;
        if (nodeInfo.size() > 1)
        {
            auto& voteWeightInfoStr = nodeInfo[1];
            boost::trim(voteWeightInfoStr);
            voteWeight = boost::lexical_cast<int64_t>(voteWeightInfoStr);
        }
        if (nodeInfo.size() > 2)
        {
            auto& termWeightInfoStr = nodeInfo[2];
            boost::trim(termWeightInfoStr);
            termWeight = boost::lexical_cast<int64_t>(termWeightInfoStr);
        }
        if (voteWeight <= 0 || termWeight < 0)
        {
            BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                      "Please set weight for " + nodeId + " to positive!"));
        }
        ConsensusNode consensusNode{.nodeID = keyFactory->createKey(fromHex(nodeId)),
            .type = consensus::Type::consensus_sealer,
            .voteWeight = static_cast<uint64_t>(voteWeight),
            .termWeight = static_cast<uint64_t>(termWeight),
            .enableNumber = 0};
        NodeConfig_LOG(INFO) << LOG_BADGE("parseConsensusNodeList")
                             << LOG_KV("sectionName", _sectionName) << LOG_KV("nodeId", nodeId)
                             << LOG_KV("voteWeight", voteWeight)
                             << LOG_KV("termWeight", termWeight);
        nodeList.push_back(consensusNode);
    }
    // only sort nodeList after rc3 version
    std::sort(nodeList.begin(), nodeList.end());
    NodeConfig_LOG(INFO) << LOG_BADGE("parseConsensusNodeList")
                         << LOG_KV("totalNodesSize", nodeList.size());
    return nodeList;
}

void NodeConfig::loadExecutorConfig(boost::property_tree::ptree const& _genesisConfig)
{
    try
    {
        genesisConfig.m_isAuthCheck = _genesisConfig.get<bool>("executor.is_auth_check", false);
        genesisConfig.m_isSerialExecute =
            _genesisConfig.get<bool>("executor.is_serial_execute", false);
        genesisConfig.m_executorVersion = _genesisConfig.get<int>("executor.version", 0);
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "executor.is_auth_check/"
                                  "executor.is_serial_execute is null, please set it!"));
    }
    // EVMC revision config — consumed by ethereum-executor (executor_version=2):
    //   executor.evm_revision       = explicit single revision for all blocks, e.g. "cancun"
    //   executor.evm_revision_forks = comma-separated "block:revision" fork transitions,
    //                                 e.g. "0:cancun,100000:osaka"
    // If neither is set, the ethereum-executor defaults to the latest revision from genesis.
    try
    {
        auto evmcRevisionStr = _genesisConfig.get<std::string>("executor.evm_revision", "");
        if (!evmcRevisionStr.empty())
        {
            if (auto rev = ledger::evmcRevisionFromName(evmcRevisionStr); rev)
            {
                if (*rev == EVMC_EXPERIMENTAL)
                {
                    BOOST_THROW_EXCEPTION(
                        InvalidConfig() << errinfo_comment(
                            "executor.evm_revision=experimental is not a released fork: evmone's "
                            "semantics for it change between versions, which would tie consensus "
                            "to the binary"));
                }
                genesisConfig.m_evmcRevision = *rev;
            }
            else
            {
                BOOST_THROW_EXCEPTION(
                    InvalidConfig() << errinfo_comment(
                        "executor.evm_revision is invalid: " + evmcRevisionStr +
                        ", supported revisions: frontier/homestead/tangerinewhistle/"
                        "spuriousdragon/byzantium/constantinople/petersburg/istanbul/berlin/"
                        "london/paris/shanghai/cancun/prague/osaka"));
            }
        }

        auto evmcForksStr = _genesisConfig.get<std::string>("executor.evm_revision_forks", "");
        if (!evmcForksStr.empty())
        {
            auto trim = [](std::string_view s) -> std::string_view {
                auto b = s.find_first_not_of(" \t\r\n");
                if (b == std::string_view::npos)
                {
                    return {};
                }
                auto e = s.find_last_not_of(" \t\r\n");
                return s.substr(b, e - b + 1);
            };
            std::stringstream ss(evmcForksStr);
            std::string token;
            while (std::getline(ss, token, ','))
            {
                auto entry = trim(token);
                if (entry.empty())
                {
                    continue;
                }
                auto colon = entry.find(':');
                if (colon == std::string_view::npos)
                {
                    BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                              "executor.evm_revision_forks invalid entry (expected "
                                              "\"block:revision\"): " +
                                              std::string(entry)));
                }
                auto blockStr = trim(entry.substr(0, colon));
                auto name = trim(entry.substr(colon + 1));
                auto block = boost::lexical_cast<protocol::BlockNumber>(blockStr);
                if (block < 0)
                {
                    // A negative fork height would otherwise become the block-0 baseline
                    // (encodeEVMCRevisionConfig picks forks.begin()->second when no 0: entry
                    // exists) — an operator typing -5 instead of 5 would silently get a
                    // different fork schedule. Reject it here.
                    BOOST_THROW_EXCEPTION(
                        InvalidConfig() << errinfo_comment(
                            "executor.evm_revision_forks block height must be >= 0, got " +
                            std::to_string(block) + " in entry: " + std::string(entry)));
                }
                auto rev = ledger::evmcRevisionFromName(name);
                if (!rev)
                {
                    BOOST_THROW_EXCEPTION(
                        InvalidConfig() << errinfo_comment(
                            "executor.evm_revision_forks invalid revision \"" + std::string(name) +
                            "\" in entry: " + std::string(entry)));
                }
                if (*rev == EVMC_EXPERIMENTAL)
                {
                    BOOST_THROW_EXCEPTION(
                        InvalidConfig() << errinfo_comment(
                            "executor.evm_revision_forks revision \"experimental\" is not a "
                            "released fork (evmone semantics change between versions); entry: " +
                            std::string(entry)));
                }
                genesisConfig.m_evmcRevisionForks[block] = *rev;
            }
        }
    }
    catch (InvalidConfig const& e)
    {
        throw;  // already carries a precise message
    }
    catch (std::exception const& e)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "Invalid executor.evm_revision config: " + std::string(e.what())));
    }
    // A v2 chain (executor_version >= 2 selects the ethereum-executor) MUST pin its EVMC
    // revision explicitly. Unlike v0/v1 the revision is consumed on every block, and a
    // binary-side default would be recorded nowhere on-chain — tying "upgrade the binary" to a
    // hard fork (replay/resync would diverge and a mixed-version network could split, without
    // either side erroring). Requiring it here makes the effective revision part of the
    // genesis config and therefore of the on-chain state.
    if (genesisConfig.m_executorVersion >= ledger::ETHEREUM_EXECUTOR_VERSION &&
        !genesisConfig.m_evmcRevision && genesisConfig.m_evmcRevisionForks.empty())
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "executor.version=2 (ethereum-executor) requires an explicit "
                "executor.evm_revision (or executor.evm_revision_forks) so the EVM "
                "revision is recorded on-chain; refusing to run with an implicit "
                "binary-side default"));
    }
    // A v2 chain must ALSO be able to persist that revision: Ledger::buildGenesisBlock only
    // writes evmc_revision for compatibility_version >= V3_18_0 (and executor_version for
    // >= V3_15_0). Below 3.18.0 the operator would be forced to write a value that is then
    // ignored (the chain runs the binary default); below 3.15.0 executor_version is not
    // persisted either, so getLedgerConfig never injects a revision and every transaction
    // throws EvmcRevisionNotConfigured. Reject both ranges up front.
    if (genesisConfig.m_executorVersion >= ledger::ETHEREUM_EXECUTOR_VERSION &&
        genesisConfig.m_compatibilityVersion <
            static_cast<uint32_t>(protocol::BlockVersion::V3_18_0_VERSION))
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "executor.version=2 requires compatibility_version >= 3.18.0: below that "
                "Ledger::buildGenesisBlock cannot persist evmc_revision, so the EVM revision "
                "would not be recorded on-chain"));
    }
    // WASM support was removed in 3.18; reject executor.is_wasm=true explicitly so operators get a
    // clear error instead of a silent EVM fallback or an opaque genesis-mismatch on startup.
    if (_genesisConfig.get<bool>("executor.is_wasm", false))
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "executor.is_wasm=true is not supported: WASM support was removed "
                "in FISCO-BCOS 3.18; use the EVM executor (set is_wasm=false)"));
    }
    try
    {
        genesisConfig.m_authAdminAccount =
            _genesisConfig.get<std::string>("executor.auth_admin_account", "");
        if (genesisConfig.m_authAdminAccount.empty() &&
            (genesisConfig.m_isAuthCheck ||
                genesisConfig.m_compatibilityVersion >= BlockVersion::V3_3_VERSION)) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(
                InvalidConfig() << errinfo_comment("executor.auth_admin_account is empty, "
                                                   "please set correct auth_admin_account"));
        }
    }
    catch (std::exception const& e)
    {
        if (genesisConfig.m_isAuthCheck ||
            genesisConfig.m_compatibilityVersion >= BlockVersion::V3_3_VERSION)
        {
            BOOST_THROW_EXCEPTION(
                InvalidConfig() << errinfo_comment("executor.auth_admin_account is null, "
                                                   "please set correct auth_admin_account"));
        }
    }
    NodeConfig_LOG(INFO) << METRIC << LOG_DESC("loadExecutorConfig")
                         << LOG_KV("isWasm", genesisConfig.m_isWasm)
                         << LOG_KV("isAuthCheck", genesisConfig.m_isAuthCheck)
                         << LOG_KV("authAdminAccount", genesisConfig.m_authAdminAccount)
                         << LOG_KV("ismSerialExecute", genesisConfig.m_isSerialExecute);
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
            genesisConfig.m_features.emplace_back(
                ledger::FeatureSet{.flag = ledger::Features::string2Flag(flag),
                    .enable = static_cast<int>(enableNumber)});
        }
    }
}

std::string bcos::tool::NodeConfig::getDefaultServiceName(
    std::string const& _nodeName, std::string const& _serviceName) const
{
    return genesisConfig.m_chainID + "." + _nodeName + _serviceName;
}

// ================================================================
// Config domain load() implementations
// ================================================================

void NodeConfig::ChainConfig::load(boost::property_tree::ptree const& _pt)
{
    blockLimit = checkAndGetValue(_pt, "chain.block_limit", "1000");
    if (blockLimit <= 0 || blockLimit > MAX_BLOCK_LIMIT)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set chain.block_limit to positive and less than " +
                                  std::to_string(MAX_BLOCK_LIMIT) + " !"));
    }
}

void NodeConfig::TxPoolConfig::load(
    boost::property_tree::ptree const& _pt, size_t _minSealTime)
{
    // Deprecation warnings for removed txpool thread config keys
    if (_pt.get_optional<std::string>("txpool.notify_worker_num"))
    {
        NodeConfig_LOG(WARNING) << LOG_DESC(
            "loadTxPoolConfig: txpool.notify_worker_num is deprecated, "
            "use thread_pool.io_thread_count instead");
    }
    if (_pt.get_optional<std::string>("txpool.verify_worker_num"))
    {
        NodeConfig_LOG(WARNING) << LOG_DESC(
            "loadTxPoolConfig: txpool.verify_worker_num is deprecated, "
            "use thread_pool.io_thread_count instead");
    }

    limit = checkAndGetValue(_pt, "txpool.limit", "15000");
    if (limit <= 0)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Please set txpool.limit to positive !"));
    }
    // the txs expiration time, in second
    auto txsExpirationTimeSeconds = checkAndGetValue(_pt, "txpool.txs_expiration_time", "600");
    if (txsExpirationTimeSeconds * 1000 <= DEFAULT_MIN_CONSENSUS_TIME_MS) [[unlikely]]
    {
        NodeConfig_LOG(WARNING) << LOG_DESC(
                                       "loadTxPoolConfig: the configured txs_expiration_time "
                                       "is smaller than default "
                                       "consensus time, reset to the consensus time")
                                << LOG_KV("txsExpirationTime(seconds)", txsExpirationTimeSeconds)
                                << LOG_KV("defaultConsTime", DEFAULT_MIN_CONSENSUS_TIME_MS);
    }
    txsExpirationTime = std::max({txsExpirationTimeSeconds * 1000,
        (int64_t)DEFAULT_MIN_CONSENSUS_TIME_MS, (int64_t)_minSealTime});
    checkBlockLimit = _pt.get<bool>("txpool.check_block_limit", true);

    // enable free node to send transactions or not
    enableTxsFromFreeNode = _pt.get<bool>("txpool.enable_txs_from_free_node", false);
    // pre-store backpressure controls
    preStoreBackpressureEnabled = _pt.get<bool>("txpool.pre_store_backpressure_enabled", true);
    auto preStoreCap = checkAndGetValue(_pt, "txpool.pre_store_max_inflight", "1024");
    if (preStoreCap <= 0)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set txpool.pre_store_max_inflight to positive !"));
    }
    preStoreMaxInflight = static_cast<size_t>(preStoreCap);
    NodeConfig_LOG(INFO) << LOG_DESC("loadTxPoolConfig") << LOG_KV("txpoolLimit", limit)
                         << LOG_KV("checkBlockLimit", checkBlockLimit)
                         << LOG_KV("txsExpirationTime(ms)", txsExpirationTime)
                         << LOG_KV("enableTxsFromFreeNode", enableTxsFromFreeNode)
                         << LOG_KV("preStoreBackpressureEnabled", preStoreBackpressureEnabled)
                         << LOG_KV("preStoreMaxInflight", preStoreMaxInflight);
}

void NodeConfig::SealerConfig::load(boost::property_tree::ptree const& _pt)
{
    minSealTime = checkAndGetValue(_pt, "consensus.min_seal_time", "500");
    allowFreeNode = _pt.get<bool>("sync.allow_free_node", false);
    if (minSealTime <= 0 || minSealTime > DEFAULT_MAX_SEAL_TIME_MS)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set consensus.min_seal_time between 1 and 600000!"));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadSealerConfig") << LOG_KV("minSealTime", minSealTime);
}

void NodeConfig::ConsensusConfig::load(boost::property_tree::ptree const& _pt)
{
    checkPointTimeoutInterval = checkAndGetValue(
        _pt, "consensus.checkpoint_timeout", std::to_string(DEFAULT_MIN_CONSENSUS_TIME_MS));
    pipelineSize =
        checkAndGetValue(_pt, "consensus.pipeline_size", std::to_string(DEFAULT_PIPELINE_SIZE));
    if (checkPointTimeoutInterval < DEFAULT_MIN_CONSENSUS_TIME_MS)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set consensus.checkpoint_timeout to no less than " +
                                  std::to_string(DEFAULT_MIN_CONSENSUS_TIME_MS) + "ms!"));
    }
    if (pipelineSize < DEFAULT_PIPELINE_SIZE)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set consensus.pipeline_size to no less than " +
                                  std::to_string(DEFAULT_PIPELINE_SIZE)));
    }
    pipelineAdmissionEnabled = _pt.get<bool>("consensus.pipeline_admission_enabled", true);
    pipelinePerPeerCapacity = checkAndGetValue(_pt, "consensus.pipeline_per_peer_capacity", "64");
    pipelineLruCapacity = checkAndGetValue(_pt, "consensus.pipeline_lru_capacity", "256");
    pipelineMaxPeers = checkAndGetValue(_pt, "consensus.pipeline_max_peers", "1024");
    if (pipelinePerPeerCapacity == 0 || pipelineLruCapacity == 0 || pipelineMaxPeers == 0)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "pipeline_per_peer_capacity / pipeline_lru_capacity / "
                                  "pipeline_max_peers must all be > 0"));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadConsensusConfig")
                         << LOG_KV("checkPointTimeoutInterval", checkPointTimeoutInterval)
                         << LOG_KV("pipeline_size", pipelineSize)
                         << LOG_KV("pipeline_admission_enabled", pipelineAdmissionEnabled)
                         << LOG_KV("pipeline_per_peer_capacity", pipelinePerPeerCapacity)
                         << LOG_KV("pipeline_lru_capacity", pipelineLruCapacity)
                         << LOG_KV("pipeline_max_peers", pipelineMaxPeers);
}

void NodeConfig::SecurityConfig::load(boost::property_tree::ptree const& _pt)
{
    privateKeyPath = _pt.get<std::string>("security.private_key_path", "node.pem");
    std::string keyEncryptionTypeStr = _pt.get<std::string>("security.kms_type", "LEGACY");
    auto keyEncryptionTypeOption = magic_enum::enum_cast<security::KeyEncryptionType>(
        keyEncryptionTypeStr, magic_enum::case_insensitive);
    if (!keyEncryptionTypeOption.has_value())
    {
        NodeConfig_LOG(ERROR) << LOG_DESC("loadSecurityConfig")
                              << LOG_KV("privateKeyPath", privateKeyPath)
                              << LOG_KV("keyEncryptionType", keyEncryptionTypeStr);
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment("Please set kms_type to LEGACY!"));
    }
    keyEncryptionType = keyEncryptionTypeOption.value();

    keyEncryptionUrl = _pt.get<std::string>("security.kms_connection_str", "");

    // Deprecated: This method will be removed in future versions.
    // Please use the new security configuration mechanism.
    // TODO: Remove in version future
    // Reason for deprecation: Old security configuration logic is being phased out
    bool enableHsm = _pt.get<bool>("security.enable_hsm", false);
    bool storageSecurityEnable = _pt.get<bool>("storage_security.enable", false);

    if (keyEncryptionType == security::KeyEncryptionType::LEGACY)
    {
        if (storageSecurityEnable)
        {
            keyEncryptionType = security::KeyEncryptionType::BCOSKMS;
            std::string keyCenterUrl =
                _pt.get<std::string>("storage_security.key_center_url", "");
            bcosKmsKeySecurityCipherDataKey =
                _pt.get<std::string>("storage_security.cipher_data_key", "");
            if (keyCenterUrl.empty() || bcosKmsKeySecurityCipherDataKey.empty())
            {
                NodeConfig_LOG(ERROR)
                    << LOG_DESC("loadSecurityConfig default with bcos kms failed!")
                    << LOG_KV("key_center_url", keyCenterUrl)
                    << LOG_KV("cipher_data_key", bcosKmsKeySecurityCipherDataKey);
                BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                          "Please provide key_center_url and cipher_data_key!"));
            }
            keyEncryptionUrl = keyCenterUrl;
            NodeConfig_LOG(INFO) << LOG_DESC("loadSecurityConfig LEGACY")
                                 << LOG_KV("privateKeyPath", privateKeyPath)
                                 << LOG_KV("keyEncryptionType",
                                        std::string(magic_enum::enum_name(keyEncryptionType)))
                                 << LOG_KV("keyEncryptionUrl", keyEncryptionUrl);
        }
        if (enableHsm)
        {
            NodeConfig_LOG(INFO) << LOG_DESC("loadSecurityConfig LEGACY")
                                 << LOG_KV("privateKeyPath", privateKeyPath)
                                 << LOG_KV("keyEncryptionType",
                                        std::string(magic_enum::enum_name(keyEncryptionType)));
            keyEncryptionType = security::KeyEncryptionType::HSM;
        }
    }
    /* TODO: Remove in version future around here */

    if (keyEncryptionType == security::KeyEncryptionType::HSM)  // hsm
    {
        hsmLibPath =
            _pt.get<std::string>("security.hsm_lib_path", "/usr/local/lib/libgmt0018.so");
        keyIndex = _pt.get<int>("security.key_index");
        password = _pt.get<std::string>("security.password", "");
        NodeConfig_LOG(INFO) << LOG_DESC("loadSecurityConfig HSM")
                             << LOG_KV("lib_path", hsmLibPath) << LOG_KV("key_index", keyIndex)
                             << LOG_KV("password", password);
    }
    else if (keyEncryptionType == security::KeyEncryptionType::CLOUDKMS)  // cloud kms
    {
        std::string cloudKmsTypeStr = _pt.get<std::string>("security.cloud_kms_type", "");
        auto cloudKmsTypeStrOption = magic_enum::enum_cast<security::CloudKmsType>(
            cloudKmsTypeStr, magic_enum::case_insensitive);
        if (!cloudKmsTypeStrOption.has_value())
        {
            NodeConfig_LOG(ERROR) << LOG_DESC("loadSecurityConfig")
                                  << LOG_KV("privateKeyPath", privateKeyPath)
                                  << LOG_KV("keyEncryptionType",
                                         std::string(magic_enum::enum_name(keyEncryptionType)));
            BOOST_THROW_EXCEPTION(
                InvalidConfig() << errinfo_comment("Please set cloud_kms_type with AWS!"));
        }
        cloudKmsType = cloudKmsTypeStrOption.value();
        NodeConfig_LOG(INFO) << LOG_DESC("loadSecurityConfig")
                             << LOG_KV("privateKeyPath", privateKeyPath)
                             << LOG_KV("keyEncryptionType",
                                    std::string(magic_enum::enum_name(keyEncryptionType)))
                             << LOG_KV("cloudKmsType",
                                    std::string(magic_enum::enum_name(cloudKmsType)));
    }
    else if (keyEncryptionType == security::KeyEncryptionType::BCOSKMS)  // bcos kms
    {
        // TODO: read form legacy config
        if (bcosKmsKeySecurityCipherDataKey.empty())
        {
            bcosKmsKeySecurityCipherDataKey =
                _pt.get<std::string>("security.cipher_data_key", "");
        }

        if (bcosKmsKeySecurityCipherDataKey.empty())
        {
            NodeConfig_LOG(ERROR) << LOG_DESC("loadSecurityConfig")
                                  << LOG_KV("privateKeyPath", privateKeyPath)
                                  << LOG_KV("keyEncryptionType",
                                         std::string(magic_enum::enum_name(keyEncryptionType)));
            BOOST_THROW_EXCEPTION(
                InvalidConfig() << errinfo_comment("Please provide cipher_data_key!"));
        }
    }
    else if (keyEncryptionType == security::KeyEncryptionType::LEGACY)  // default
    {
        NodeConfig_LOG(INFO) << LOG_DESC("loadSecurityConfig")
                             << LOG_KV("privateKeyPath", privateKeyPath)
                             << LOG_KV("keyEncryptionType",
                                    std::string(magic_enum::enum_name(keyEncryptionType)));
    }
    else
    {
        NodeConfig_LOG(ERROR) << LOG_DESC("loadSecurityConfig")
                              << LOG_KV("privateKeyPath", privateKeyPath)
                              << LOG_KV("keyEncryptionType",
                                     std::string(magic_enum::enum_name(keyEncryptionType)));
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set kms_type to DEFAULT or HSM or CLOUDKMS or BCOSKMS!"));
    }

    NodeConfig_LOG(INFO) << LOG_DESC("loadSecurityConfig")
                         << LOG_KV("privateKeyPath", privateKeyPath)
                         << LOG_KV("keyEncryptionType",
                                std::string(magic_enum::enum_name(keyEncryptionType)));
}

void NodeConfig::StorageSecurityConfig::load(boost::property_tree::ptree const& _pt)
{
    enable = _pt.get<bool>("storage_security.enable", false);
    if (!enable)
    {
        return;
    }
    // TODO: deprecated, remove in the future
    std::string storageEncryptionTypeStr =
        _pt.get<std::string>("storage_security.kms_type", "LEGACY");
    auto storageEncryptionTypeOption = magic_enum::enum_cast<security::StorageEncryptionType>(
        storageEncryptionTypeStr, magic_enum::case_insensitive);
    if (!storageEncryptionTypeOption.has_value())
    {
        NodeConfig_LOG(ERROR) << LOG_DESC("loadStorageSecurityConfig")
                              << LOG_KV("storageEncryptionType", storageEncryptionTypeStr);
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Please set kms_type to LEGACY or BCOSKMS!"));
    }
    encryptionType = storageEncryptionTypeOption.value();
    keyCenterUrl = _pt.get<std::string>("storage_security.kms_connection_str", "");

    // Deprecated: This method will be removed in future versions.
    // Please use the new security configuration mechanism.
    // TODO: Remove in version future
    // Reason for deprecation: Old security configuration logic is being phased out
    if (encryptionType == security::StorageEncryptionType::LEGACY)
    {
        encryptionType = security::StorageEncryptionType::BCOSKMS;
        std::string keyCenterUrlLegacy = _pt.get<std::string>("storage_security.key_center_url", "");
        if (keyCenterUrlLegacy.empty())
        {
            NodeConfig_LOG(ERROR)
                << LOG_DESC("loadStorageSecurityConfig default with bcos kms failed!")
                << LOG_KV("key_center_url", keyCenterUrlLegacy);
            BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                      "Please provide key_manager_ip and key_manager_port!"));
        }
        keyCenterUrl = keyCenterUrlLegacy;
        NodeConfig_LOG(INFO) << LOG_DESC("loadStorageSecurityConfig BCOSKMS")
                             << LOG_KV("storageEncryptionType",
                                    ("security::StorageEncryptionType::LEGACY"))
                             << LOG_KV("keyCenterUrl", keyCenterUrl);
    }
    /* TODO: Remove in version future around here */

    cipherDataKey = _pt.get<std::string>("storage_security.cipher_data_key", "");
    if (cipherDataKey.empty())
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Please provide cipher_data_key!"));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadStorageSecurityConfig")
                         << LOG_KV("keyCenterUrl", keyCenterUrl);
}

void NodeConfig::StorageConfig::load(
    boost::property_tree::ptree const& _pt, std::string const& _groupID)
{
    dataPath = _pt.get<std::string>("storage.data_path", "data/" + _groupID);
    type = _pt.get<std::string>("storage.type", "RocksDB");
    keyPageSize = _pt.get<int32_t>("storage.key_page_size", 10240);
    maxWriteBufferNumber = _pt.get<int32_t>("storage.max_write_buffer_number", 4);
    maxBackgroundJobs = _pt.get<int32_t>("storage.max_background_jobs", 4);
    writeBufferSize = _pt.get<size_t>("storage.write_buffer_size", 64 << 20);
    minWriteBufferNumberToMerge = _pt.get<int32_t>("storage.min_write_buffer_number_to_merge", 1);
    blockCacheSize = _pt.get<size_t>("storage.block_cache_size", 128 << 20);
    enableStatistics = _pt.get<bool>("storage.enable_statistics", false);
    enableRocksDBBlob = _pt.get<bool>("storage.enable_rocksdb_blob", false);
    pdCaPath = _pt.get<std::string>("storage.pd_ssl_ca_path", "");
    pdCertPath = _pt.get<std::string>("storage.pd_ssl_cert_path", "");
    pdKeyPath = _pt.get<std::string>("storage.pd_ssl_key_path", "");
    enableArchive = _pt.get<bool>("storage.enable_archive", false);
    syncArchivedBlocks = _pt.get<bool>("storage.sync_archived_blocks", false);
    enableSeparateBlockAndState = _pt.get<bool>("storage.enable_separate_block_state", false);
    if (boost::iequals(type, bcos::storage::TiKV))
    {
        enableSeparateBlockAndState = false;
        NodeConfig_LOG(INFO) << LOG_DESC("Only rocksDB support separate block and state")
                             << LOG_KV("separateBlockAndState", enableSeparateBlockAndState)
                             << LOG_KV("storageType", type);
    }
    stateDBPath = dataPath + "/state";
    blockDBPath = dataPath + "/block";

    if (enableArchive)
    {
        archiveListenIP = _pt.get<std::string>("storage.archive_ip");
        archiveListenPort = _pt.get<uint16_t>("storage.archive_port");
    }

    auto pdAddrsStr = _pt.get<std::string>("storage.pd_addrs", "127.0.0.1:2379");
    boost::split(pdAddrs, pdAddrsStr, boost::is_any_of(","));
    enableLRUCacheStorage = _pt.get<bool>("storage.enable_cache", true);
    cacheSize = _pt.get<ssize_t>("storage.cache_size", DEFAULT_CACHE_SIZE);
    NodeConfig_LOG(INFO) << LOG_DESC("loadStorageConfig") << LOG_KV("storagePath", dataPath)
                         << LOG_KV("KeyPage", keyPageSize) << LOG_KV("storageType", type)
                         << LOG_KV("pdAddrs", pdAddrsStr) << LOG_KV("pdCaPath", pdCaPath)
                         << LOG_KV("enableArchive", enableArchive)
                         << LOG_KV("enableSeparateBlockAndState", enableSeparateBlockAndState)
                         << LOG_KV("archiveListenIP", archiveListenIP)
                         << LOG_KV("archiveListenPort", archiveListenPort)
                         << LOG_KV("enable_rocksdb_blob", enableRocksDBBlob)
                         << LOG_KV("enableLRUCacheStorage", enableLRUCacheStorage);
}

void NodeConfig::ExecutorConfig::load(boost::property_tree::ptree const& _pt)
{
    vmCacheSize = _pt.get<int>("executor.vm_cache_size", 1024);
    baselineScheduler.grainSize = _pt.get<int>("executor.baseline_scheduler_chunksize", 100);
    baselineScheduler.parallel = _pt.get<bool>("executor.baseline_scheduler_parallel", false);

    // Deprecation warning for removed config key
    if (_pt.get_optional<std::string>("executor.baseline_scheduler_maxthread"))
    {
        NodeConfig_LOG(WARNING) << LOG_DESC(
            "loadOthersConfig: executor.baseline_scheduler_maxthread is deprecated, "
            "use thread_pool.tbb_thread_count instead");
    }
}

void NodeConfig::RpcConfig::load(boost::property_tree::ptree const& _pt)
{
    listenIP = _pt.get<std::string>("rpc.listen_ip", "0.0.0.0");
    listenPort = _pt.get<uint16_t>("rpc.listen_port", 20200);
    filterTimeout = _pt.get<uint32_t>("rpc.filter_timeout", 300) * 1000;  // to milliseconds
    maxProcessBlock = _pt.get<uint32_t>("rpc.filter_max_process_block", 10);
    smSsl = _pt.get<bool>("rpc.sm_ssl", false);
    disableSsl = _pt.get<bool>("rpc.disable_ssl", false);
    // enable ssl cover disable ssl
    if (auto enableSsl = _pt.get_optional<bool>("rpc.enable_ssl"))
    {
        disableSsl = !enableSsl.value();
    }

    // Deprecation warning for removed rpc.thread_count
    if (_pt.get_optional<int>("rpc.thread_count"))
    {
        NodeConfig_LOG(WARNING) << LOG_DESC(
            "loadRpcConfig: rpc.thread_count is deprecated, "
            "use thread_pool.io_thread_count instead");
    }

    NodeConfig_LOG(INFO) << LOG_DESC("loadRpcConfig") << LOG_KV("listenIP", listenIP)
                         << LOG_KV("listenPort", listenPort) << LOG_KV("smSsl", smSsl)
                         << LOG_KV("disableSsl", disableSsl);
}

void NodeConfig::Web3RpcConfig::load(boost::property_tree::ptree const& _pt)
{
    enable = _pt.get<bool>("web3_rpc.enable", false);
    listenIP = _pt.get<std::string>("web3_rpc.listen_ip", "127.0.0.1");
    listenPort = _pt.get<uint16_t>("web3_rpc.listen_port", 8545);
    filterTimeout = _pt.get<uint32_t>("web3_rpc.filter_timeout", 300) * 1000;  // to milliseconds
    maxProcessBlock = _pt.get<uint32_t>("web3_rpc.filter_max_process_block", 10);
    batchRequestSizeLimit = _pt.get<uint32_t>("web3_rpc.batch_request_size_limit", 8);
    httpBodySizeLimit = _pt.get<uint32_t>("web3_rpc.request_body_size_limit", 10240000);
    enableCors = _pt.get<bool>("web3_rpc.enable_cors", true);
    corsAllowCredentials = _pt.get<bool>("web3_rpc.cors_allow_credentials", true);
    corsAllowedOrigins = _pt.get<std::string>("web3_rpc.cors_allowed_origins", "*");
    corsAllowedMethods =
        _pt.get<std::string>("web3_rpc.cors_allowed_methods", "GET, POST, OPTIONS");
    corsAllowedHeaders = _pt.get<std::string>(
        "web3_rpc.cors_allowed_headers", "Content-Type, Authorization, X-Requested-With");
    corsMaxAge = _pt.get<int32_t>("web3_rpc.cors_max_age", 86400);
    syncTransaction = _pt.get<bool>("web3_rpc.sync_transaction", false);
    safeBlockDepth = _pt.get<uint32_t>("web3_rpc.safe_block_depth", 0);
    finalizedBlockDepth = _pt.get<uint32_t>("web3_rpc.finalized_block_depth", 0);

    // Deprecation warning for removed web3_rpc.thread_count
    if (_pt.get_optional<int>("web3_rpc.thread_count"))
    {
        NodeConfig_LOG(WARNING) << LOG_DESC(
            "loadWeb3RpcConfig: web3_rpc.thread_count is deprecated, "
            "use thread_pool.io_thread_count instead");
    }

    NodeConfig_LOG(INFO) << LOG_DESC("loadWeb3RpcConfig") << LOG_KV("enableWeb3Rpc", enable)
                         << LOG_KV("listenIP", listenIP) << LOG_KV("listenPort", listenPort)
                         << LOG_KV("filterTimeout", filterTimeout)
                         << LOG_KV("maxProcessBlock", maxProcessBlock)
                         << LOG_KV("batchRequestSizeLimit", batchRequestSizeLimit)
                         << LOG_KV("enableCors", enableCors)
                         << LOG_KV("corsAllowedOrigins", corsAllowedOrigins)
                         << LOG_KV("corsAllowedMethods", corsAllowedMethods)
                         << LOG_KV("corsAllowedHeaders", corsAllowedHeaders)
                         << LOG_KV("corsMaxAge", corsMaxAge)
                         << LOG_KV("corsAllowCredentials", corsAllowCredentials)
                         << LOG_KV("syncTransaction", syncTransaction)
                         << LOG_KV("safeBlockDepth", safeBlockDepth)
                         << LOG_KV("finalizedBlockDepth", finalizedBlockDepth);
}

void NodeConfig::OpEngineRpcConfig::load(boost::property_tree::ptree const& _pt)
{
    enable = _pt.get<bool>("op_engine_rpc.enable", false);
    listenIP = _pt.get<std::string>("op_engine_rpc.listen_ip", "127.0.0.1");
    listenPort = _pt.get<uint16_t>("op_engine_rpc.listen_port", 8551);
    httpBodySizeLimit = _pt.get<uint32_t>("op_engine_rpc.request_body_size_limit", 10485760);
    batchRequestSizeLimit = _pt.get<uint32_t>("op_engine_rpc.batch_request_size_limit", 8);
    jwtSecretFile = _pt.get<std::string>("op_engine_rpc.jwt_secret_file", "conf/op-engine/jwt.hex");
    clockSkewSecs = _pt.get<int32_t>("op_engine_rpc.clock_skew_secs", 60);
    // test-only escape hatch, see Initializer's executor-version guard
    allowV1Executor = _pt.get<bool>("op_engine_rpc.unsafe_allow_v1_executor", false);

    NodeConfig_LOG(INFO) << LOG_DESC("loadOpEngineRpcConfig")
                         << LOG_KV("enableOpEngineRpc", enable) << LOG_KV("listenIP", listenIP)
                         << LOG_KV("listenPort", listenPort)
                         << LOG_KV("requestBodySizeLimit", httpBodySizeLimit)
                         << LOG_KV("batchRequestSizeLimit", batchRequestSizeLimit)
                         << LOG_KV("jwtSecretFile", jwtSecretFile)
                         << LOG_KV("clockSkewSecs", clockSkewSecs)
                         << LOG_KV("unsafeAllowV1Executor", allowV1Executor);
}

void NodeConfig::SingleNodeConsensusConfig::load(boost::property_tree::ptree const& _pt)
{
    enable = _pt.get<bool>("consensus.enable_single_node_consensus", false);
    blockInterval = _pt.get<uint64_t>("consensus.block_interval", 1000);
    produceEmptyBlocks = _pt.get<bool>("consensus.produce_empty_blocks", true);
    feeRecipient = _pt.get<std::string>(
        "consensus.fee_recipient", "0x0000000000000000000000000000000000000000");
    prevRandao = _pt.get<std::string>("consensus.prev_randao", "");
    fixedTimestamp = _pt.get<std::uint64_t>("consensus.fixed_timestamp", 0);
    NodeConfig_LOG(INFO) << LOG_DESC("loadSingleNodeConsensusConfig")
                         << LOG_KV("enableSingleNodeConsensus", enable)
                         << LOG_KV("blockInterval", blockInterval)
                         << LOG_KV("produceEmptyBlocks", produceEmptyBlocks)
                         << LOG_KV("feeRecipient", feeRecipient);
}

void NodeConfig::GatewayConfig::load(boost::property_tree::ptree const& _pt)
{
    listenIP = _pt.get<std::string>("p2p.listen_ip", "0.0.0.0");
    listenPort = _pt.get<uint16_t>("p2p.listen_port", 30300);
    nodeDir = _pt.get<std::string>("p2p.nodes_path", "./");
    nodeFileName = _pt.get<std::string>("p2p.nodes_file", "nodes.json");
    smSsl = _pt.get<bool>("p2p.sm_ssl", false);

    NodeConfig_LOG(INFO) << LOG_DESC("loadGatewayConfig") << LOG_KV("listenIP", listenIP)
                         << LOG_KV("listenPort", listenPort) << LOG_KV("smSsl", smSsl)
                         << LOG_KV("nodesFile", nodeFileName);
}

void NodeConfig::SyncConfig::load(boost::property_tree::ptree const& _pt)
{
    enableSendBlockStatusByTree = _pt.get<bool>("sync.sync_block_by_tree", false);
    enableSendTxByTree = _pt.get<bool>("sync.send_txs_by_tree", false);
    treeWidth = _pt.get<std::uint32_t>("sync.tree_width", 3);
    if (treeWidth == 0 || treeWidth > UINT16_MAX)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Please set sync.tree_width in 1~65535"));
    }
    NodeConfig_LOG(INFO) << LOG_DESC("loadSyncConfig")
                         << LOG_KV("sync_block_by_tree", enableSendBlockStatusByTree)
                         << LOG_KV("send_txs_by_tree", enableSendTxByTree)
                         << LOG_KV("tree_width", treeWidth);
}

void NodeConfig::CertConfig::load(boost::property_tree::ptree const& _pt)
{
    path = _pt.get<std::string>("cert.ca_path", "./");

    // load sm cert
    std::string smCaCertFile = path + "/" + _pt.get<std::string>("cert.sm_ca_cert", "sm_ca.crt");
    std::string smNodeCertFile =
        path + "/" + _pt.get<std::string>("cert.sm_node_cert", "sm_ssl.crt");
    std::string smNodeKeyFile =
        path + "/" + _pt.get<std::string>("cert.sm_node_key", "sm_ssl.key");
    std::string smEnNodeCertFile =
        path + "/" + _pt.get<std::string>("cert.sm_ennode_cert", "sm_enssl.crt");
    std::string smEnNodeKeyFile =
        path + "/" + _pt.get<std::string>("cert.sm_ennode_key", "sm_enssl.key");

    smCaCert = smCaCertFile;
    smNodeCert = smNodeCertFile;
    smNodeKey = smNodeKeyFile;
    enSmNodeCert = smEnNodeCertFile;
    enSmNodeKey = smEnNodeKeyFile;

    NodeConfig_LOG(INFO) << LOG_DESC("loadCertConfig") << LOG_KV("ca_path", path)
                         << LOG_KV("sm_ca_cert", smCaCertFile)
                         << LOG_KV("sm_node_cert", smNodeCertFile)
                         << LOG_KV("sm_node_key", smNodeKeyFile)
                         << LOG_KV("sm_ennode_cert", smEnNodeCertFile)
                         << LOG_KV("sm_ennode_key", smEnNodeKeyFile);

    // load cert
    std::string caCertFile = path + "/" + _pt.get<std::string>("cert.ca_cert", "ca.crt");
    std::string nodeCertFile = path + "/" + _pt.get<std::string>("cert.node_cert", "ssl.crt");
    std::string nodeKeyFile = path + "/" + _pt.get<std::string>("cert.node_key", "ssl.key");

    caCert = caCertFile;
    nodeCert = nodeCertFile;
    nodeKey = nodeKeyFile;

    NodeConfig_LOG(INFO) << LOG_DESC("loadCertConfig") << LOG_KV("ca_path", path)
                         << LOG_KV("ca_cert", caCertFile) << LOG_KV("node_cert", nodeCertFile)
                         << LOG_KV("node_key", nodeKeyFile);
}

bool NodeConfig::engineDrivenBlockProduction() const
{
    return singleNodeConsensus.enable || opEngineRpc.enable;
}

void NodeConfig::FailOverConfig::load(
    boost::property_tree::ptree const& _pt, bool _enforceMemberID)
{
    // only enable leaderElection when using tikv
    enable = _pt.get("failover.enable", false);
    if (!enable)
    {
        return;
    }
    clusterUrl = _pt.get<std::string>("failover.cluster_url", "127.0.0.1:2379");
    memberID = _pt.get("failover.member_id", "");
    if (memberID.size() == 0 && _enforceMemberID)
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment("Please set failover.member_id must be non-empty "));
    }
    leaseTTL =
        checkAndGetValue(_pt, "failover.lease_ttl", std::to_string(DEFAULT_MIN_LEASE_TTL_SECONDS));
    if (leaseTTL < DEFAULT_MIN_LEASE_TTL_SECONDS)
    {
        BOOST_THROW_EXCEPTION(InvalidConfig() << errinfo_comment(
                                  "Please set failover.lease_ttl to no less than " +
                                  std::to_string(DEFAULT_MIN_LEASE_TTL_SECONDS) + " seconds!"));
    }

    NodeConfig_LOG(INFO) << LOG_DESC("loadFailOverConfig")
                         << LOG_KV("failOverClusterUrl", clusterUrl)
                         << LOG_KV("memberID", memberID.size() > 0 ? memberID : "not-set")
                         << LOG_KV("leaseTTL", leaseTTL) << LOG_KV("enableFailOver", enable);
}

void NodeConfig::ThreadPoolConfig::load(boost::property_tree::ptree const& _pt)
{
    ioThreadCount = checkAndGetValue(
        _pt, "thread_pool.io_thread_count", std::to_string(std::thread::hardware_concurrency() + 1));
    tbbThreadCount = checkAndGetValue(_pt, "thread_pool.tbb_thread_count", "0");
    NodeConfig_LOG(INFO) << LOG_DESC("loadOthersConfig") << LOG_KV("ioThreadCount", ioThreadCount)
                         << LOG_KV("tbbThreadCount", tbbThreadCount);
}

void NodeConfig::TarsRPCConfig::load(boost::property_tree::ptree const& _pt)
{
    host = _pt.get<std::string>("rpc.tars_rpc_host", "127.0.0.1");
    port = _pt.get<int>("rpc.tars_rpc_port", 0);

    // Deprecation warning for removed config key
    if (_pt.get_optional<std::string>("rpc.tars_rpc_thread_count"))
    {
        NodeConfig_LOG(WARNING) << LOG_DESC(
            "loadOthersConfig: rpc.tars_rpc_thread_count is deprecated, "
            "use thread_pool.io_thread_count instead");
    }
}

void NodeConfig::OthersConfig::load(boost::property_tree::ptree const& _pt)
{
    sendTxTimeout = _pt.get<int>("others.send_tx_timeout", -1);
    checkTransactionSignature = _pt.get<bool>("experimental.check_transaction_signature", true);
    checkParallelConflict = _pt.get<bool>("experimental.check_parallel_conflict", true);
    singlePointConsensus = _pt.get<bool>("experimental.single_point_consensus", false);
    if (auto forceSender = _pt.get<std::string>("experimental.force_sender", {});
        !forceSender.empty())
    {
        this->forceSender = fromHexWithPrefix(forceSender);
    }

    NodeConfig_LOG(INFO) << LOG_DESC("loadOthersConfig") << LOG_KV("sendTxTimeout", sendTxTimeout)
                         << LOG_KV("checkTransactionSignature", checkTransactionSignature)
                         << LOG_KV("checkParallelConflict", checkParallelConflict)
                         << LOG_KV("singlePointConsensus", singlePointConsensus)
                         << LOG_KV("enableAuth", toHex(this->forceSender));
}

std::string NodeConfig::compatibilityVersionStr() const
{
    std::stringstream ss;
    ss << (bcos::protocol::BlockVersion)genesisConfig.m_compatibilityVersion;
    return ss.str();
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
           << "gaslimit:" << genesisConfig.m_txGasLimit
           << '\n'
           // tx.gas_price / tx.excess_blob_gas are seeded into SYS_CONFIG at genesis and feed
           // v2 execution (base fee / blob base fee), so they must be part of the genesis pin
           // the guard at Ledger::buildGenesisBlock compares on restart — otherwise two nodes
           // configured identically except for these keys pass the genesis-mismatch check and
           // diverge on the first block. Emit them only when non-default (mirroring the
           // evmRevision pattern) so a default-configured chain's genesis string is byte-
           // identical to before this change and existing chains are unaffected.
           << (genesisConfig.m_txGasPrice != "0x0" ?
                      "gasprice:" + genesisConfig.m_txGasPrice + "\n" :
                      "")
           << (genesisConfig.m_excessBlobGas ?
                      "excessBlobGas:" + std::to_string(*genesisConfig.m_excessBlobGas) + "\n" :
                      "")
           << "[executor]" << '\n'
           << "iswasm: " << genesisConfig.m_isWasm << '\n'
           << "isAuthCheck:" << genesisConfig.m_isAuthCheck << '\n'
           << "authAdminAccount:" << genesisConfig.m_authAdminAccount << '\n'
           << "isSerialExecute:" << genesisConfig.m_isSerialExecute << '\n';
        if (genesisConfig.m_evmcRevision || !genesisConfig.m_evmcRevisionForks.empty())
        {
            ss << "evmRevision:"
               << ledger::encodeEVMCRevisionConfig(
                      genesisConfig.m_evmcRevision, genesisConfig.m_evmcRevisionForks)
               << '\n';
        }
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
        // A3: the eth genesis header is part of the genesis pin. Emitted only
        // when present, so every legacy chain's genesis string stays
        // byte-identical to before this change (node-admission compatibility).
        if (genesisConfig.m_ethGenesisHeader.has_value())
        {
            auto const& ethHeader = *genesisConfig.m_ethGenesisHeader;
            ss << "[ethGenesisHeader]" << '\n'
               << "parent_hash:" << ethHeader.m_parentHash.hexPrefixed() << '\n'
               << "sha3_uncles:" << ethHeader.m_sha3Uncles.hexPrefixed() << '\n'
               << "miner:" << ethHeader.m_miner.hexPrefixed() << '\n'
               << "state_root:" << ethHeader.m_stateRoot.hexPrefixed() << '\n'
               << "transactions_root:" << ethHeader.m_transactionsRoot.hexPrefixed() << '\n'
               << "receipts_root:" << ethHeader.m_receiptsRoot.hexPrefixed() << '\n'
               << "logs_bloom:" << toHexStringWithPrefix(ethHeader.m_logsBloom) << '\n'
               << "difficulty:" << ethHeader.m_difficulty << '\n'
               << "number:" << ethHeader.m_number << '\n'
               << "gas_limit:" << ethHeader.m_gasLimit << '\n'
               << "gas_used:" << ethHeader.m_gasUsed << '\n'
               << "timestamp:" << ethHeader.m_timestamp << '\n'
               << "extra_data:" << toHexStringWithPrefix(ethHeader.m_extraData) << '\n'
               << "mix_hash:" << ethHeader.m_mixHash.hexPrefixed() << '\n'
               << "nonce:" << ethHeader.m_nonce.hexPrefixed() << '\n'
               << "base_fee_per_gas:" << ethHeader.m_baseFeePerGas << '\n'
               << "withdrawals_root:" << ethHeader.m_withdrawalsRoot.hexPrefixed() << '\n'
               << "blob_gas_used:" << ethHeader.m_blobGasUsed << '\n'
               << "excess_blob_gas:" << ethHeader.m_excessBlobGas << '\n'
               << "parent_beacon_block_root:" << ethHeader.m_parentBeaconBlockRoot.hexPrefixed()
               << '\n'
               << "requests_hash:" << ethHeader.m_requestsHash.hexPrefixed() << '\n'
               << "hash:" << ethHeader.m_hash.hexPrefixed() << '\n';
        }

        size_t j = 0;
        for (const auto& node : ledgerConfig.consensusNodeList())
        {
            ss << "node." + boost::lexical_cast<std::string>(j) + ":" + toHex(node.nodeID->data()) +
                      "," + std::to_string(node.voteWeight) + "\n";
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
        ss << toHex(node.nodeID->data()) << "," << node.voteWeight << ";";
    }
    auto genesisdata = ss.str();
    NodeConfig_LOG(INFO) << LOG_BADGE("generateGenesisData") << LOG_KV("genesisData", genesisdata);

    return genesisdata;
}
