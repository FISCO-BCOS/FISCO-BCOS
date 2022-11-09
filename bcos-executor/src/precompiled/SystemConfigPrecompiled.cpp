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
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-framework/protocol/Protocol.h>
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

SystemConfigPrecompiled::SystemConfigPrecompiled() : Precompiled(GlobalHashImpl::g_hashImpl)
{
    name2Selector[SYSCONFIG_METHOD_SET_STR] =
        getFuncSelector(SYSCONFIG_METHOD_SET_STR, GlobalHashImpl::g_hashImpl);
    name2Selector[SYSCONFIG_METHOD_GET_STR] =
        getFuncSelector(SYSCONFIG_METHOD_GET_STR, GlobalHashImpl::g_hashImpl);
    auto defaultCmp = [](std::string_view _key, int64_t _value, int64_t _minValue) {
        if (_value >= _minValue)
        {
            return;
        }
        BOOST_THROW_EXCEPTION(PrecompiledError(
            "Invalid value " + std::to_string(_value) + " ,the value for " + std::string{_key} +
            " must be no less than " + std::to_string(_minValue)));
    };
    m_sysValueCmp.insert(std::make_pair(SYSTEM_KEY_TX_GAS_LIMIT, [defaultCmp](int64_t _value) {
        defaultCmp(SYSTEM_KEY_TX_GAS_LIMIT, _value, TX_GAS_LIMIT_MIN);
    }));
    m_sysValueCmp.insert(
        std::make_pair(SYSTEM_KEY_CONSENSUS_LEADER_PERIOD, [defaultCmp](int64_t _value) {
            defaultCmp(SYSTEM_KEY_CONSENSUS_LEADER_PERIOD, _value, 1);
        }));
    m_sysValueCmp.insert(std::make_pair(SYSTEM_KEY_TX_COUNT_LIMIT, [defaultCmp](int64_t _value) {
        defaultCmp(SYSTEM_KEY_TX_COUNT_LIMIT, _value, TX_COUNT_LIMIT_MIN);
    }));
    // for compatibility
    // Note: the compatibility_version is not compatibility
    m_sysValueCmp.insert(std::make_pair(SYSTEM_KEY_COMPATIBILITY_VERSION, [](int64_t _value) {
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
}

std::shared_ptr<PrecompiledExecResult> SystemConfigPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    // parse function name
    uint32_t func = getParamFunc(_callParameters->input());
    auto blockContext = _executive->blockContext().lock();

    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (func == name2Selector[SYSCONFIG_METHOD_SET_STR])
    {
        // setValueByKey(string,string)
        if (blockContext->isAuthCheck() && !checkSenderFromAuth(_callParameters->m_sender))
        {
            PRECOMPILED_LOG(INFO) << LOG_BADGE("SystemConfigPrecompiled")
                                  << LOG_DESC("sender is not from sys")
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
            PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext->number())
                                  << LOG_BADGE("SystemConfigPrecompiled")
                                  << LOG_DESC("setValueByKey") << LOG_KV("configKey", configKey)
                                  << LOG_KV("configValue", configValue);

            int64_t value = checkValueValid(configKey, configValue, blockContext->blockVersion());
            auto table = _executive->storage().openTable(ledger::SYS_CONFIG);

            auto entry = table->newEntry();
            auto systemConfigEntry = SystemConfigEntry{configValue, blockContext->number() + 1};
            entry.setObject(systemConfigEntry);

            table->setRow(configKey, std::move(entry));

            if (shouldUpgradeChain(configKey, blockContext->blockVersion(), value))
            {
                upgradeChain(_executive, _callParameters, codec, value);
            }

            PRECOMPILED_LOG(INFO) << LOG_BADGE("SystemConfigPrecompiled")
                                  << LOG_DESC("set system config") << LOG_KV("configKey", configKey)
                                  << LOG_KV("configValue", configValue)
                                  << LOG_KV("enableNum", blockContext->number() + 1);
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
    else
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("SystemConfigPrecompiled")
                              << LOG_DESC("call undefined function") << LOG_KV("func", func);
        BOOST_THROW_EXCEPTION(PrecompiledError("SystemConfigPrecompiled call undefined function!"));
    }
    return _callParameters;
}

int64_t SystemConfigPrecompiled::checkValueValid(
    std::string_view _key, std::string_view value, uint32_t blockVersion)
{
    int64_t configuredValue = 0;
    std::string key = std::string(_key);
    if (!c_supportedKey.contains(key))
    {
        BOOST_THROW_EXCEPTION(PrecompiledError("unsupported key " + key));
    }
    if (value.empty())
    {
        BOOST_THROW_EXCEPTION(PrecompiledError("The value for " + key + " must be non-empty."));
    }
    try
    {
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
                              << LOG_KV("errorInfo", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(PrecompiledError(errorMsg));
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("SystemConfigPrecompiled")
                              << LOG_DESC("checkValueValid failed") << LOG_KV("key", _key)
                              << LOG_KV("value", value)
                              << LOG_KV("errorInfo", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(
            PrecompiledError("The value for " + key + " must be a valid number."));
    }
    if (m_sysValueCmp.contains(key))
    {
        (m_sysValueCmp.at(key))(configuredValue);
    }
    return configuredValue;
}

std::pair<std::string, protocol::BlockNumber> SystemConfigPrecompiled::getSysConfigByKey(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const std::string& _key) const
{
    try
    {
        auto table = _executive->storage().openTable(ledger::SYS_CONFIG);
        auto entry = table->getRow(_key);
        if (entry)
        {
            auto [value, enableNumber] = entry->getObject<SystemConfigEntry>();
            return {value, enableNumber};
        }

        PRECOMPILED_LOG(INFO) << LOG_BADGE("SystemConfigPrecompiled")
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

void SystemConfigPrecompiled::upgradeChain(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters, CodecWrapper const& codec,
    uint32_t toVersion) const
{
    auto blockContext = _executive->blockContext().lock();

    if (blockContext->blockVersion() <= static_cast<uint32_t>(BlockVersion::V3_0_VERSION) &&
        toVersion >= static_cast<uint32_t>(BlockVersion::V3_1_VERSION))
    {
        // rebuild Bfs
        auto input = codec.encodeWithSig(
            "rebuildBfs(uint256,uint256)", blockContext->blockVersion(), toVersion);
        std::string sender =
            blockContext->isWasm() ? precompiled::SYS_CONFIG_NAME : precompiled::SYS_CONFIG_ADDRESS;
        std::string toAddress =
            blockContext->isWasm() ? precompiled::BFS_NAME : precompiled::BFS_ADDRESS;
        auto response = externalRequest(_executive, ref(input), _callParameters->m_origin, sender,
            toAddress, false, false, _callParameters->m_gasLeft);

        if (response->status != (int32_t)TransactionStatus::None)
        {
            PRECOMPILED_LOG(INFO) << LOG_BADGE("SystemConfigPrecompiled")
                                  << LOG_DESC("rebuildBfs failed")
                                  << LOG_KV("status", response->status);
            BOOST_THROW_EXCEPTION(PrecompiledError("Rebuild BFS error."));
        }
    }
}