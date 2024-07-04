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
 * @file SystemConfigPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-26
 */

#include "SystemConfigPrecompiled.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/storage/LegacyStorageMethods.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-ledger/src/libledger/utilities/Common.h>
#include <bcos-tool/VersionConverter.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>

using namespace bcos;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::ledger;
using namespace bcos::protocol;

const char* const SYSCONFIG_METHOD_SET_STR = "setValueByKey(string,string)";
const char* const SYSCONFIG_METHOD_GET_STR = "getValueByKey(string)";

SystemConfigPrecompiled::SystemConfigPrecompiled(crypto::Hash::Ptr hashImpl) : Precompiled(hashImpl)
{
    name2Selector[SYSCONFIG_METHOD_SET_STR] = getFuncSelector(SYSCONFIG_METHOD_SET_STR, hashImpl);
    name2Selector[SYSCONFIG_METHOD_GET_STR] = getFuncSelector(SYSCONFIG_METHOD_GET_STR, hashImpl);
    auto defaultCmp = [](std::string_view _key, int64_t _value, int64_t _minValue, uint32_t version,
                          BlockVersion minVersion = BlockVersion::V3_0_VERSION) {
        if (versionCompareTo(version, minVersion) < 0) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(PrecompiledError("unsupported key " + std::string(_key)));
        }
        if (_value >= _minValue) [[likely]]
        {
            return;
        }
        BOOST_THROW_EXCEPTION(PrecompiledError(
            "Invalid value " + std::to_string(_value) + " ,the value for " + std::string{_key} +
            " must be no less than " + std::to_string(_minValue)));
    };
    m_sysValueCmp.insert(
        std::make_pair(SYSTEM_KEY_TX_GAS_LIMIT, [defaultCmp](int64_t _value, uint32_t version) {
            defaultCmp(SYSTEM_KEY_TX_GAS_LIMIT, _value, TX_GAS_LIMIT_MIN, version);
        }));
    m_sysValueCmp.insert(
        std::make_pair(SYSTEM_KEY_TX_GAS_PRICE, [](int64_t _value, uint32_t version) {
            if (versionCompareTo(version, BlockVersion::V3_6_VERSION) < 0) [[unlikely]]
            {
                BOOST_THROW_EXCEPTION(
                    PrecompiledError("unsupported key " + std::string(SYSTEM_KEY_TX_GAS_PRICE)));
            }
            return;
        }));
    m_sysValueCmp.insert(std::make_pair(
        SYSTEM_KEY_CONSENSUS_LEADER_PERIOD, [defaultCmp](int64_t _value, uint32_t version) {
            defaultCmp(SYSTEM_KEY_CONSENSUS_LEADER_PERIOD, _value, 1, version);
        }));
    m_sysValueCmp.insert(
        std::make_pair(SYSTEM_KEY_TX_COUNT_LIMIT, [defaultCmp](int64_t _value, uint32_t version) {
            defaultCmp(SYSTEM_KEY_TX_COUNT_LIMIT, _value, TX_COUNT_LIMIT_MIN, version);
        }));
    m_sysValueCmp.insert(std::make_pair(SYSTEM_KEY_AUTH_CHECK_STATUS, [defaultCmp](int64_t _value,
                                                                          uint32_t version) {
        defaultCmp(SYSTEM_KEY_AUTH_CHECK_STATUS, _value, 0, version, BlockVersion::V3_3_VERSION);
        if (_value > (decltype(_value))UINT8_MAX) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(PrecompiledError(
                "Invalid status value, must less than " + std::to_string(UINT8_MAX)));
        }
    }));
    m_sysValueCmp.insert(std::make_pair(
        SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM, [defaultCmp](int64_t _value, uint32_t version) {
            defaultCmp(SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM, _value, RPBFT_EPOCH_BLOCK_NUM_MIN, version,
                BlockVersion::V3_5_VERSION);
        }));
    m_sysValueCmp.insert(std::make_pair(
        SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM, [defaultCmp](int64_t _value, uint32_t version) {
            defaultCmp(SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM, _value, RPBFT_EPOCH_SEALER_NUM_MIN,
                version, BlockVersion::V3_5_VERSION);
        }));
    // for compatibility
    // Note: the compatibility_version is not compatibility
    m_sysValueCmp.insert(
        std::make_pair(SYSTEM_KEY_COMPATIBILITY_VERSION, [](int64_t _value, uint32_t version) {
            if (_value < (uint32_t)(g_BCOSConfig.minSupportedVersion()))
            {
                std::stringstream errorMsg;
                errorMsg << LOG_DESC("set " + std::string(SYSTEM_KEY_COMPATIBILITY_VERSION) +
                                     " failed for lower than min_supported_version")
                         << LOG_KV("minSupportedVersion", g_BCOSConfig.minSupportedVersion());
                PRECOMPILED_LOG(INFO) << errorMsg.str() << LOG_KV("setValue", _value);
                BOOST_THROW_EXCEPTION(PrecompiledError(errorMsg.str()));
            }
        }));
    m_valueConverter.insert(std::make_pair(SYSTEM_KEY_COMPATIBILITY_VERSION,
        [](const std::string& _value, uint32_t blockVersion) -> uint64_t {
            auto version = bcos::tool::toVersionNumber(_value);
            if (versionCompareTo(blockVersion, BlockVersion::V3_1_VERSION) >= 0)
            {
                if (version < blockVersion)
                {
                    BOOST_THROW_EXCEPTION(PrecompiledError(
                        "Set compatibility version should not lower than version nowadays."));
                }
            }
            return version;
        }));
    m_valueConverter.insert(std::make_pair(
        SYSTEM_KEY_TX_GAS_PRICE, [](const std::string& _value, uint32_t blockVersion) -> uint64_t {
            if (versionCompareTo(blockVersion, BlockVersion::V3_6_VERSION) < 0) [[unlikely]]
            {
                BOOST_THROW_EXCEPTION(
                    PrecompiledError("unsupported key " + std::string(SYSTEM_KEY_TX_GAS_PRICE)));
            }
            if (!isHexStringV2(_value))
            {
                BOOST_THROW_EXCEPTION(PrecompiledError(
                    "Invalid value " + _value + " ,the value for " +
                    std::string{SYSTEM_KEY_TX_GAS_PRICE} + " must be a hex number like 0xa."));
            }
            return 0;
        }));
    m_valueConverter.insert(std::make_pair(
        SYSTEM_KEY_WEB3_CHAIN_ID, [](const std::string& _value, uint32_t blockVersion) -> uint64_t {
            if (blockVersion < BlockVersion::V3_9_0_VERSION)
            {
                BOOST_THROW_EXCEPTION(bcos::tool::InvalidVersion(
                    fmt::format("unsupported key {}", SYSTEM_KEY_WEB3_CHAIN_ID)));
            }
            uint64_t number = 0;
            try
            {
                number = std::stoull(_value);
            }
            catch (...)
            {
                BOOST_THROW_EXCEPTION(PrecompiledError(
                    fmt::format("Invalid value {}, the value for {} must be a number string.",
                        _value, SYSTEM_KEY_WEB3_CHAIN_ID)));
            }
            if (number > UINT32_MAX)
            {
                BOOST_THROW_EXCEPTION(PrecompiledError(
                    fmt::format("Invalid value {}, the value for {} must be less than UINT32_MAX.",
                        _value, SYSTEM_KEY_WEB3_CHAIN_ID)));
            }
            return 0;
        }));
}

std::shared_ptr<PrecompiledExecResult> SystemConfigPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    // parse function name
    uint32_t func = getParamFunc(_callParameters->input());
    const auto& blockContext = _executive->blockContext();

    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (func == name2Selector[SYSCONFIG_METHOD_SET_STR])
    {
        // setValueByKey(string,string)
        if (blockContext.isAuthCheck() && !checkSenderFromAuth(_callParameters->m_sender))
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("SystemConfigPrecompiled") << LOG_DESC("sender is not from sys")
                << LOG_KV("sender", _callParameters->m_sender);
            _callParameters->setExecResult(codec.encode(int32_t(CODE_NO_AUTHORIZED)));
        }
        else
        {
            std::string configKey;
            std::string configValue;
            codec.decode(_callParameters->params(), configKey, configValue);
            // Uniform lowercase configKey
            boost::to_lower(configKey);
            PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                                  << LOG_BADGE("SystemConfigPrecompiled")
                                  << LOG_DESC("setValueByKey") << LOG_KV("configKey", configKey)
                                  << LOG_KV("configValue", configValue);

            int64_t value =
                validate(_executive, configKey, configValue, blockContext.blockVersion());
            auto table = _executive->storage().openTable(ledger::SYS_CONFIG);

            auto entry = table->newEntry();
            auto systemConfigEntry = SystemConfigEntry{configValue, blockContext.number() + 1};
            entry.setObject(systemConfigEntry);

            table->setRow(configKey, std::move(entry));

            if (shouldUpgradeChain(configKey, blockContext.blockVersion(), value))
            {
                upgradeChain(_executive, _callParameters, codec, value);
            }
            // if feature_balance_precompiled is enabled, register governor to caller
            if (configKey == SYSTEM_KEY_BALANCE_PRECOMPILED_SWITCH &&
                blockContext.blockVersion() >= BlockVersion::V3_6_VERSION)
            {
                registerGovernorToCaller(_executive, _callParameters, codec);
            }

            PRECOMPILED_LOG(INFO) << LOG_BADGE("SystemConfigPrecompiled")
                                  << LOG_DESC("set system config") << LOG_KV("configKey", configKey)
                                  << LOG_KV("configValue", configValue)
                                  << LOG_KV("enableNum", blockContext.number() + 1);
            _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
        }
    }
    else if (func == name2Selector[SYSCONFIG_METHOD_GET_STR])
    {
        // getValueByKey(string)
        std::string configKey;
        codec.decode(_callParameters->params(), configKey);
        // Uniform lowercase configKey
        boost::to_lower(configKey);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("SystemConfigPrecompiled")
                               << LOG_DESC("getValueByKey func") << LOG_KV("configKey", configKey);

        auto valueNumberPair = getSysConfigByKey(_executive, configKey);
        _callParameters->setExecResult(codec.encode(valueNumberPair.first, valueNumberPair.second));
    }
    else [[unlikely]]
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("SystemConfigPrecompiled")
                              << LOG_DESC("call undefined function") << LOG_KV("func", func);
        BOOST_THROW_EXCEPTION(PrecompiledError("SystemConfigPrecompiled call undefined function!"));
    }
    return _callParameters;
}

int64_t SystemConfigPrecompiled::validate(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, std::string_view _key,
    std::string_view value, uint32_t blockVersion)
{
    int64_t configuredValue = 0;
    std::string key = std::string(_key);
    auto featureKeys = ledger::Features::featureKeys();
    bool setFeature = (RANGES::find(featureKeys, key) != featureKeys.end());
    if (!m_sysValueCmp.contains(key) && !m_valueConverter.contains(key) && !setFeature)
    {
        BOOST_THROW_EXCEPTION(PrecompiledError("unsupported key " + key));
    }

    if (value.empty())
    {
        BOOST_THROW_EXCEPTION(PrecompiledError("The value for " + key + " must be non-empty."));
    }
    try
    {
        if (setFeature && value != "1")
        {
            BOOST_THROW_EXCEPTION(PrecompiledError("The value for " + key + " must be 1."));
        }

        if (setFeature)
        {
            _executive->blockContext().features().validate(key);
        }

        if (m_valueConverter.contains(key))
        {
            configuredValue = (m_valueConverter.at(key))(std::string(value), blockVersion);
        }
        else
        {
            configuredValue = boost::lexical_cast<int64_t>(value);
        }
    }
    catch (bcos::tool::InvalidVersion const& e)
    {
        // Note: be careful when modify error message here
        auto errorMsg =
            "Invalid value for " + key +
            ". The version must be in format of major_version.middle_version.minimum_version, and "
            "the minimum version is optional. The major version must between " +
            std::to_string(bcos::protocol::MIN_MAJOR_VERSION) + " to " +
            std::to_string(bcos::protocol::MAX_MAJOR_VERSION);
        PRECOMPILED_LOG(INFO) << LOG_DESC("SystemConfigPrecompiled: invalid version")
                              << LOG_KV("info", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(PrecompiledError(errorMsg));
    }
    catch (bcos::tool::InvalidSetFeature const& e)
    {
        ///
        PRECOMPILED_LOG(INFO) << LOG_DESC("SystemConfigPrecompiled: set feature failed")
                              << LOG_KV("info", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(PrecompiledError(*boost::get_error_info<bcos::errinfo_comment>(e)));
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("SystemConfigPrecompiled")
                              << LOG_DESC("checkValueValid failed") << LOG_KV("key", _key)
                              << LOG_KV("value", value)
                              << LOG_KV("info", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(
            PrecompiledError("The value for " + key + " must be a valid number."));
    }
    if (m_sysValueCmp.contains(key))
    {
        (m_sysValueCmp.at(key))(configuredValue, blockVersion);
    }
    return configuredValue;
}

std::pair<std::string, protocol::BlockNumber> SystemConfigPrecompiled::getSysConfigByKey(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const std::string& _key)
{
    try
    {
        auto table = _executive->storage().openTable(ledger::SYS_CONFIG);
        auto entry = table->getRow(_key);
        if (entry) [[likely]]
        {
            auto [value, enableNumber] = entry->getObject<SystemConfigEntry>();
            return {value, enableNumber};
        }

        // entry not exist
        auto const& blockContext = _executive->blockContext();
        if (_key == ledger::SYSTEM_KEY_AUTH_CHECK_STATUS &&
            blockContext.blockVersion() >= BlockVersion::V3_3_VERSION) [[unlikely]]
        {
            return {blockContext.isAuthCheck() ? "1" : "0", 0};
        }

        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("SystemConfigPrecompiled")
                               << LOG_DESC("get sys config failed") << LOG_KV("configKey", _key);
        return {"", -1};
    }
    catch (std::exception const& e)
    {
        auto errorMsg =
            "getSysConfigByKey for " + _key + "failed, e:" + boost::diagnostic_information(e);
        PRECOMPILED_LOG(INFO) << LOG_BADGE("SystemConfigPrecompiled") << errorMsg;
        return {errorMsg, -1};
    }
}

bool bcos::precompiled::SystemConfigPrecompiled::shouldUpgradeChain(
    std::string_view key, uint32_t fromVersion, uint32_t toVersion) noexcept
{
    return key == bcos::ledger::SYSTEM_KEY_COMPATIBILITY_VERSION && toVersion > fromVersion;
}

void SystemConfigPrecompiled::upgradeChain(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters, CodecWrapper const& codec,
    uint32_t toVersion)
{
    const auto& blockContext = _executive->blockContext();
    auto version = blockContext.blockVersion();
    if (versionCompareTo(version, BlockVersion::V3_0_VERSION) <= 0 &&
        versionCompareTo(toVersion, BlockVersion::V3_1_VERSION) >= 0)
    {
        // rebuild Bfs
        auto input = codec.encodeWithSig(
            "rebuildBfs(uint256,uint256)", blockContext.blockVersion(), toVersion);
        std::string sender =
            blockContext.isWasm() ? precompiled::SYS_CONFIG_NAME : precompiled::SYS_CONFIG_ADDRESS;
        std::string toAddress =
            blockContext.isWasm() ? precompiled::BFS_NAME : precompiled::BFS_ADDRESS;
        auto response = externalRequest(_executive, ref(input), _callParameters->m_origin, sender,
            toAddress, false, false, _callParameters->m_gasLeft);

        if (response->status != (int32_t)TransactionStatus::None)
        {
            PRECOMPILED_LOG(INFO) << LOG_BADGE("SystemConfigPrecompiled")
                                  << LOG_DESC("rebuildBfs failed")
                                  << LOG_KV("status", response->status);
            BOOST_THROW_EXCEPTION(PrecompiledError("Rebuild BFS error."));
        }

        // create new system tables of 3.1.0
        // clang-format off
        constexpr auto tables = std::to_array<std::string_view>({
            SYS_CODE_BINARY, std::string_view(bcos::ledger::SYS_VALUE),
            SYS_CONTRACT_ABI, std::string_view(bcos::ledger::SYS_VALUE)
        });
        // clang-format on
        constexpr size_t total = tables.size();

        for (size_t i = 0; i < total; i += 2)
        {
            _executive->storage().createTable(
                std::string(tables.at(i)), std::string(tables.at(i + 1)));
        }
    }

    // Write default features when data version changes
    Features bugfixFeatures;
    auto fromVersionNum = static_cast<protocol::BlockVersion>(version);
    auto toVersionNum = static_cast<protocol::BlockVersion>(toVersion);
    bugfixFeatures.setUpgradeFeatures(fromVersionNum, toVersionNum);
    PRECOMPILED_LOG(INFO) << "Upgrade chain, from: " << fromVersionNum << ", to: " << toVersionNum;
    for (auto [flag, name, value] : bugfixFeatures.flags())
    {
        if (value)
        {
            PRECOMPILED_LOG(INFO) << "Add set flag: " << name;
        }
    }

    task::syncWait(bugfixFeatures.writeToStorage(*_executive->blockContext().storage(), 0,
        toVersionNum > protocol::BlockVersion::V3_2_7_VERSION));

    // From 3.3 / 3.4 or to 3.3 / 3.4, enable the feature_sharding
    if ((version >= BlockVersion::V3_3_VERSION && version <= BlockVersion::V3_4_VERSION) ||
        (toVersion >= BlockVersion::V3_3_VERSION && toVersion <= BlockVersion::V3_4_VERSION))
    {
        Features shardingFeatures;
        shardingFeatures.set(ledger::Features::Flag::feature_sharding);
        task::syncWait(
            shardingFeatures.writeToStorage(*_executive->blockContext().backendStorage(), 0));
    }
}

void SystemConfigPrecompiled::registerGovernorToCaller(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters, CodecWrapper const& codec)
{
    std::vector<Address> governorAddress;
    try
    {
        governorAddress = getGovernorList(_executive, _callParameters, codec);
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("SystemConfigPrecompiled, registerGovernorToCaller")
                              << LOG_DESC("get governor list failed")
                              << LOG_KV("info", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(
            PrecompiledError("get governor list failed, maybe current is wasm model, "
                             "feature_balance_precompiled is not supported in wasm model."));
    }
    if (governorAddress.empty())
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("SystemConfigPrecompiled")
                              << LOG_DESC("get governor is empty, maybe governor is not set");
        return;
    }
    // register governor to caller
    auto table = _executive->storage().openTable(ledger::SYS_BALANCE_CALLER);
    if (!table)
    {
        std::string tableStr(SYS_BALANCE_CALLER);
        table = _executive->storage().createTable(tableStr, "value");
        for (auto const& address : governorAddress)
        {
            auto entry = table->newEntry();
            Entry CallerEntry;
            CallerEntry.importFields({"1"});
            _executive->storage().setRow(SYS_BALANCE_CALLER, address.hex(), std::move(CallerEntry));
        }
        return;
    }
    else
    {
        for (auto const& address : governorAddress)
        {
            auto entry = table->getRow(address.hex());
            if (!entry)
            {
                Entry CallerEntry;
                CallerEntry.importFields({"1"});
                _executive->storage().setRow(
                    SYS_BALANCE_CALLER, address.hex(), std::move(CallerEntry));
            }
        }
    }
}