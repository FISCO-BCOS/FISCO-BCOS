/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file SystemConfigPrecompiled.cpp
 *  @author chaychen
 *  @date 20181211
 */
#include "SystemConfigPrecompiled.h"

#include "libprecompiled/EntriesPrecompiled.h"
#include "libprecompiled/TableFactoryPrecompiled.h"
#include <libethcore/ABI.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

const char* const SYSCONFIG_METHOD_SET_STR = "setValueByKey(string,string)";

SystemConfigPrecompiled::SystemConfigPrecompiled()
{
    name2Selector[SYSCONFIG_METHOD_SET_STR] = getFuncSelector(SYSCONFIG_METHOD_SET_STR);
}

int SystemConfigPrecompiled::setSystemConfigByKey(
    dev::blockverifier::ExecutiveContext::Ptr _context, std::string const& _configKey,
    std::string const& _configValue, std::shared_ptr<AccessOptions> _accessOption)
{
    storage::Table::Ptr table = openTable(_context, SYS_CONFIG);

    auto condition = table->newCondition();
    auto entries = table->select(_configKey, condition);
    auto entry = table->newEntry();
    entry->setField(SYSTEM_CONFIG_KEY, _configKey);
    entry->setField(SYSTEM_CONFIG_VALUE, _configValue);
    entry->setField(SYSTEM_CONFIG_ENABLENUM,
        boost::lexical_cast<std::string>(_context->blockInfo().number + 1));
    int count = 0;
    if (entries->size() == 0u)
    {
        count = table->insert(_configKey, entry, _accessOption);
        if (count == dev::storage::CODE_NO_AUTHORIZED)
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("setSystemConfigByKey") << LOG_DESC("permission denied");
            return dev::storage::CODE_NO_AUTHORIZED;
        }
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("setSystemConfigByKey")
                               << LOG_DESC("setValueByKey successfully");
        return count;
    }
    count = table->update(_configKey, entry, condition, _accessOption);
    if (count == dev::storage::CODE_NO_AUTHORIZED)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("SystemConfigPrecompiled")
                               << LOG_DESC("permission denied");
        return dev::storage::CODE_NO_AUTHORIZED;
    }
    return count;
}

PrecompiledExecResult::Ptr SystemConfigPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin, Address const&)
{
    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    int result = 0;
    if (func == name2Selector[SYSCONFIG_METHOD_SET_STR])
    {
        // setValueByKey(string,string)
        std::string configKey, configValue;
        abi.abiOut(data, configKey, configValue);
        // Uniform lowercase configKey
        boost::to_lower(configKey);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("SystemConfigPrecompiled")
                               << LOG_DESC("setValueByKey func") << LOG_KV("configKey", configKey)
                               << LOG_KV("configValue", configValue);

        if (!checkValueValid(configKey, configValue))
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("SystemConfigPrecompiled") << LOG_DESC("set invalid value")
                << LOG_KV("configKey", configKey) << LOG_KV("configValue", configValue);
            getErrorCodeOut(callResult->mutableExecResult(), CODE_INVALID_CONFIGURATION_VALUES);
            return callResult;
        }
        result = SystemConfigPrecompiled::setSystemConfigByKey(
            context, configKey, configValue, std::make_shared<AccessOptions>(origin));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("SystemConfigPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    getErrorCodeOut(callResult->mutableExecResult(), result);
    return callResult;
}

bool SystemConfigPrecompiled::checkValueValid(std::string const& key, std::string const& value)
{
    // switch for gasChargeManager
    if (SYSTEM_KEY_CHARGE_MANAGE_SWITCH == key)
    {
        if (g_BCOSConfig.version() < V2_8_0)
        {
            return false;
        }
        return ((value.compare(SYSTEM_KEY_CHARGE_MANAGE_SWITCH_ON) == 0) ||
                (value.compare(SYSTEM_KEY_CHARGE_MANAGE_SWITCH_OFF) == 0));
    }

    int64_t configuredValue = 0;
    try
    {
        configuredValue = boost::lexical_cast<int64_t>(value);
    }
    catch (std::exception const& e)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("SystemConfigPrecompiled")
                               << LOG_DESC("checkValueValid failed") << LOG_KV("key", key)
                               << LOG_KV("value", value) << LOG_KV("errorInfo", e.what());
        return false;
    }
    if (SYSTEM_KEY_TX_COUNT_LIMIT == key)
    {
        return (configuredValue >= TX_COUNT_LIMIT_MIN);
    }
    else if (SYSTEM_KEY_TX_GAS_LIMIT == key)
    {
        return (configuredValue >= TX_GAS_LIMIT_MIN);
    }
    else if (SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM == key)
    {
        return (configuredValue >= RPBFT_EPOCH_SEALER_NUM_MIN);
    }
    else if (SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM == key)
    {
        if (g_BCOSConfig.version() < V2_6_0)
        {
            return (configuredValue >= RPBFT_EPOCH_BLOCK_NUM_MIN);
        }
        else
        {
            // epoch_block_num is at least 2 when supported_version >= v2.6.0
            return (configuredValue > RPBFT_EPOCH_BLOCK_NUM_MIN);
        }
    }
    else if (SYSTEM_KEY_CONSENSUS_TIMEOUT == key)
    {
        if (g_BCOSConfig.version() < V2_6_0)
        {
            return false;
        }
        return (configuredValue >= SYSTEM_CONSENSUS_TIMEOUT_MIN &&
                configuredValue < SYSTEM_CONSENSUS_TIMEOUT_MAX);
    }
    // only can insert tx_count_limit and tx_gas_limit, rpbft_epoch_sealer_num,
    // rpbft_epoch_block_num as system config
    return false;
}
