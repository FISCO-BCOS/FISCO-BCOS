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
#include <bcos-framework/interfaces/ledger/LedgerTypeDef.h>
#include <bcos-framework/interfaces/protocol/GlobalConfig.h>
#include <bcos-framework/interfaces/protocol/Protocol.h>
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

SystemConfigPrecompiled::SystemConfigPrecompiled(crypto::Hash::Ptr _hashImpl)
  : Precompiled(_hashImpl)
{
    name2Selector[SYSCONFIG_METHOD_SET_STR] = getFuncSelector(SYSCONFIG_METHOD_SET_STR, _hashImpl);
    name2Selector[SYSCONFIG_METHOD_GET_STR] = getFuncSelector(SYSCONFIG_METHOD_GET_STR, _hashImpl);
    auto defaultCmp = [](std::string const& _key, int64_t _value, int64_t _minValue) {
        if (_value >= _minValue)
        {
            return;
        }
        BOOST_THROW_EXCEPTION(
            PrecompiledError("Invalid value " + std::to_string(_value) + " ,the value for " + _key +
                             " must be no less than " + std::to_string(_minValue)));
    };
    m_sysValueCmp.insert(std::make_pair(SYSTEM_KEY_TX_GAS_LIMIT,
        [defaultCmp](int64_t _v) { defaultCmp(SYSTEM_KEY_TX_GAS_LIMIT, _v, TX_GAS_LIMIT_MIN); }));
    m_sysValueCmp.insert(std::make_pair(SYSTEM_KEY_CONSENSUS_LEADER_PERIOD,
        [defaultCmp](int64_t _v) { defaultCmp(SYSTEM_KEY_CONSENSUS_LEADER_PERIOD, _v, 1); }));
    m_sysValueCmp.insert(std::make_pair(SYSTEM_KEY_TX_COUNT_LIMIT, [defaultCmp](int64_t _v) {
        defaultCmp(SYSTEM_KEY_TX_COUNT_LIMIT, _v, TX_COUNT_LIMIT_MIN);
    }));
    // for compatibility
    // Note: the compatibility_version is not compatibility
    m_sysValueCmp.insert(std::make_pair(SYSTEM_KEY_COMPATIBILITY_VERSION, [](int64_t _v) {
        if (_v < (uint32_t)(g_BCOSConfig.minSupportedVersion()))
        {
            std::stringstream errorMsg;
            errorMsg << LOG_DESC("set " + std::string(SYSTEM_KEY_COMPATIBILITY_VERSION) +
                                 " failed for lower than min_supported_version")
                     << LOG_KV("minSupportedVersion", g_BCOSConfig.minSupportedVersion());
            PRECOMPILED_LOG(WARNING) << errorMsg.str() << LOG_KV("setValue", _v);
            BOOST_THROW_EXCEPTION(PrecompiledError(errorMsg.str()));
        }
    }));
    m_valueConverter.insert(
        std::make_pair(SYSTEM_KEY_COMPATIBILITY_VERSION, [](std::string _value) -> uint64_t {
            return (uint64_t)(bcos::tool::toVersionNumber(_value));
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
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("SystemConfigPrecompiled") << LOG_DESC("sender is not from sys")
                << LOG_KV("sender", _callParameters->m_sender);
            _callParameters->setExecResult(codec.encode(int32_t(CODE_NO_AUTHORIZED)));
        }
        else
        {
            std::string configKey, configValue;
            codec.decode(_callParameters->params(), configKey, configValue);
            // Uniform lowercase configKey
            boost::to_lower(configKey);
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("SystemConfigPrecompiled") << LOG_DESC("setValueByKey func")
                << LOG_KV("configKey", configKey) << LOG_KV("configValue", configValue);

            checkValueValid(configKey, configValue);
            auto table = _executive->storage().openTable(ledger::SYS_CONFIG);

            auto entry = table->newEntry();
            auto systemConfigEntry = SystemConfigEntry{configValue, blockContext->number() + 1};
            entry.setObject(systemConfigEntry);

            table->setRow(configKey, std::move(entry));

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
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("SystemConfigPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
        BOOST_THROW_EXCEPTION(PrecompiledError("SystemConfigPrecompiled call undefined function!"));
    }
    auto version = blockContext->blockVersion();
    // Not recommend gas calculation for system-contract
    if (version <= (int32_t)(bcos::protocol::Version::RC4_VERSION))
    {
        _callParameters->setGas(_callParameters->m_gas);
    }
    return _callParameters;
}

void SystemConfigPrecompiled::checkValueValid(std::string_view _key, std::string_view value)
{
    int64_t configuredValue;
    std::string key = std::string(_key);
    if (!c_supportedKey.count(key))
    {
        BOOST_THROW_EXCEPTION(PrecompiledError("unsupported key " + key));
    }
    if (value.empty())
    {
        BOOST_THROW_EXCEPTION(PrecompiledError("The value for " + key + " must be non-empty."));
    }
    try
    {
        if (m_valueConverter.count(key))
        {
            configuredValue = (m_valueConverter.at(key))(std::string(value));
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
        PRECOMPILED_LOG(WARNING) << LOG_DESC("SystemConfigPrecompiled: invalid version")
                                 << LOG_KV("errorInfo", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(PrecompiledError(errorMsg));
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("SystemConfigPrecompiled")
                               << LOG_DESC("checkValueValid failed") << LOG_KV("key", _key)
                               << LOG_KV("value", value)
                               << LOG_KV("errorInfo", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(
            PrecompiledError("The value for " + key + " must be a valid number."));
    }
    if (m_sysValueCmp.count(key))
    {
        (m_sysValueCmp.at(key))(configuredValue);
    }
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
        else
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("SystemConfigPrecompiled")
                                   << LOG_DESC("get sys config error") << LOG_KV("configKey", _key);
            return {"", -1};
        }
    }
    catch (std::exception const& e)
    {
        auto errorMsg =
            "getSysConfigByKey for " + _key + "failed, error:" + boost::diagnostic_information(e);
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("SystemConfigPrecompiled") << errorMsg;
        return {errorMsg, -1};
    }
}