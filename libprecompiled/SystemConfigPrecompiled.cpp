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

bytes SystemConfigPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("SystemConfigPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHex(param));

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    bytes out;
    int count = 0;
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
                << LOG_BADGE("SystemConfigPrecompiled")
                << LOG_DESC("SystemConfigPrecompiled set invalid value")
                << LOG_KV("configKey", configKey) << LOG_KV("configValue", configValue);
            getErrorCodeOut(out, CODE_INVALID_CONFIGURATION_VALUES);
            return out;
        }

        storage::Table::Ptr table = openTable(context, SYS_CONFIG);

        auto condition = table->newCondition();
        auto entries = table->select(configKey, condition);
        auto entry = table->newEntry();
        entry->setField(SYSTEM_CONFIG_KEY, configKey);
        entry->setField(SYSTEM_CONFIG_VALUE, configValue);
        entry->setField(SYSTEM_CONFIG_ENABLENUM,
            boost::lexical_cast<std::string>(context->blockInfo().number + 1));

        if (entries->size() == 0u)
        {
            count = table->insert(configKey, entry, std::make_shared<AccessOptions>(origin));
            if (count == storage::CODE_NO_AUTHORIZED)
            {
                PRECOMPILED_LOG(DEBUG)
                    << LOG_BADGE("SystemConfigPrecompiled") << LOG_DESC("permission denied");
                result = storage::CODE_NO_AUTHORIZED;
            }
            else
            {
                PRECOMPILED_LOG(DEBUG) << LOG_BADGE("SystemConfigPrecompiled")
                                       << LOG_DESC("setValueByKey successfully");
                result = count;
            }
        }
        else
        {
            count =
                table->update(configKey, entry, condition, std::make_shared<AccessOptions>(origin));
            if (count == storage::CODE_NO_AUTHORIZED)
            {
                PRECOMPILED_LOG(DEBUG)
                    << LOG_BADGE("SystemConfigPrecompiled") << LOG_DESC("permission denied");
                result = storage::CODE_NO_AUTHORIZED;
            }
            else
            {
                PRECOMPILED_LOG(DEBUG) << LOG_BADGE("SystemConfigPrecompiled")
                                       << LOG_DESC("update value by key successfully");
                result = count;
            }
        }
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("SystemConfigPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    getErrorCodeOut(out, result);
    return out;
}

bool SystemConfigPrecompiled::checkValueValid(std::string const& key, std::string const& value)
{
    if (SYSTEM_KEY_TX_COUNT_LIMIT == key)
    {
        try
        {
            return (boost::lexical_cast<uint64_t>(value) >= TX_COUNT_LIMIT_MIN);
        }
        catch (boost::bad_lexical_cast& e)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE(e.what());
            return false;
        }
    }
    else if (SYSTEM_KEY_TX_GAS_LIMIT == key)
    {
        try
        {
            return (boost::lexical_cast<uint64_t>(value) >= TX_GAS_LIMIT_MIN);
        }
        catch (boost::bad_lexical_cast& e)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE(e.what());
            return false;
        }
    }
    else if (SYSTEM_KEY_RPBFT_EPOCH_SIZE == key)
    {
        try
        {
            return (boost::lexical_cast<uint64_t>(value) >= RPBFT_EPOCH_SIZE_MIN);
        }
        catch (boost::bad_lexical_cast& e)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_DESC("checkValueValid failed") << LOG_KV("errInfo", e.what());
            return false;
        }
    }
    else if (SYSTEM_KEY_RPBFT_ROTATING_INTERVAL == key)
    {
        try
        {
            return (boost::lexical_cast<uint64_t>(value) >= RPBFT_ROTATING_INTERVAL_MIN);
        }
        catch (boost::bad_lexical_cast& e)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_DESC("checkValueValid failed") << LOG_KV("errInfo", e.what());
            return false;
        }
    }
    // only can insert tx_count_limit and tx_gas_limit, rpbft_epoch_size,  rpbft_rotating_interval
    // as system config
    return false;
}
