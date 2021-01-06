/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2021 fisco-dev contributors.
 */
/**
 * @brief : Implementation of GasChargeManagePrecompiled
 * @file: GasChargeManagePrecompiled.cpp
 * @author: yujiechen
 * @date: 2021-01-05
 */

#include "GasChargeManagePrecompiled.h"
#include "SystemConfigPrecompiled.h"
#include <json/json.h>
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/Address.h>
#include <libethcore/ABI.h>
#include <libethcore/Common.h>
#include <libstorage/Common.h>

using namespace dev;
using namespace dev::eth;
using namespace dev::precompiled;
using namespace dev::blockverifier;
using namespace dev::storage;

const char* const GCM_METHOD_CHARGE_STR = "charge(address,uint256)";
const char* const GCM_METHOD_DEDUCT_STR = "deduct(address,uint256)";
const char* const GCM_METHOD_QUERY_GAS_STR = "queryRemainGas(address)";
// permission related operations
const char* const GCM_METHOD_GRANT_CHARGER_STR = "grantCharger(address)";
const char* const GCM_METHOD_REVOKE_CHARGER_STR = "revokeCharger(address)";
const char* const GCM_METHOD_QUERY_CHARGERS_STR = "listChargers()";

GasChargeManagePrecompiled::GasChargeManagePrecompiled()
{
    PRECOMPILED_LOG(INFO) << LOG_DESC("init GasChargeManagePrecompiled");
    name2Selector[GCM_METHOD_CHARGE_STR] = getFuncSelector(GCM_METHOD_CHARGE_STR);
    name2Selector[GCM_METHOD_DEDUCT_STR] = getFuncSelector(GCM_METHOD_DEDUCT_STR);
    name2Selector[GCM_METHOD_QUERY_GAS_STR] = getFuncSelector(GCM_METHOD_QUERY_GAS_STR);
    name2Selector[GCM_METHOD_GRANT_CHARGER_STR] = getFuncSelector(GCM_METHOD_GRANT_CHARGER_STR);
    name2Selector[GCM_METHOD_REVOKE_CHARGER_STR] = getFuncSelector(GCM_METHOD_REVOKE_CHARGER_STR);
    name2Selector[GCM_METHOD_QUERY_CHARGERS_STR] = getFuncSelector(GCM_METHOD_QUERY_CHARGERS_STR);
}

PrecompiledExecResult::Ptr GasChargeManagePrecompiled::call(
    std::shared_ptr<ExecutiveContext> _context, bytesConstRef _param, Address const& _origin,
    Address const& _sender)
{
    auto funcSelector = getParamFunc(_param);
    auto paramData = getParamData(_param);
    dev::eth::ContractABI contractABI;
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    do
    {
        if (funcSelector == name2Selector[GCM_METHOD_CHARGE_STR])
        {
            charge(callResult, _context, paramData, _origin, _sender);
        }
        if (funcSelector == name2Selector[GCM_METHOD_DEDUCT_STR])
        {
            deduct(callResult, _context, paramData, _origin, _sender);
        }
        if (funcSelector == name2Selector[GCM_METHOD_QUERY_GAS_STR])
        {
            queryRemainGas(callResult, _context, paramData, _origin);
        }
        PRECOMPILED_LOG(WARNING) << LOG_DESC("GasChargeManagePrecompiled: undefined function")
                                 << LOG_KV("funcSelector", funcSelector);
        callResult->setExecResult(contractABI.abiIn("", (u256)CODE_GCM_UNDEFINED_FUNCTION));
    } while (0);
    return callResult;
}

std::shared_ptr<std::set<Address>> GasChargeManagePrecompiled::parseChargerList(
    std::string const& _chargerListStr)
{
    std::vector<std::string> chargerList;
    boost::split(chargerList, _chargerListStr, boost::is_any_of(c_chargerListSplitStr));
    std::shared_ptr<std::set<Address>> chargerAddressList = std::make_shared<std::set<Address>>();
    for (auto const& charger : chargerList)
    {
        if (charger.length() == 0)
        {
            continue;
        }
        try
        {
            chargerAddressList->insert(dev::eth::toAddress(charger));
        }
        catch (const std::exception& e)
        {
            PRECOMPILED_LOG(WARNING) << LOG_DESC("convert charger str to address failed")
                                     << LOG_KV("chargerAccount", charger);
        }
    }
    return chargerAddressList;
}

std::shared_ptr<std::set<Address>> GasChargeManagePrecompiled::getChargerList(
    std::shared_ptr<ExecutiveContext> _context)
{
    // open table
    auto sysConfigTable = openTable(_context, SYS_CONFIG);
    if (!sysConfigTable)
    {
        BOOST_THROW_EXCEPTION(PrecompiledException("Open table to get charger list failed"));
    }
    // get charger list
    auto result = dev::precompiled::getSystemConfigByKey(
        sysConfigTable, SYSTEM_KEY_CHARGER_LIST, _context->blockInfo().number);

    PRECOMPILED_LOG(DEBUG) << LOG_DESC("getChargerList") << LOG_KV("chargerList", result->first);
    return parseChargerList(result->first);
}

// check the account status
bool GasChargeManagePrecompiled::checkAccountStatus(PrecompiledExecResult::Ptr _callResult,
    ExecutiveContext::Ptr _context, Address const& _userAccount)
{
    ContractABI abi;
    auto tableName = dev::precompiled::getContractTableName(_userAccount);
    auto accountStatusRet = dev::precompiled::getAccountStatus(_context, tableName);
    int retCode = dev::precompiled::parseAccountStatus(accountStatusRet.first);
    if (retCode != CODE_SUCCESS)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("GasChargeManagePrecompiled")
                                 << LOG_DESC("invalid account")
                                 << LOG_KV("userAccount", _userAccount.hex())
                                 << LOG_KV("retCode", retCode);
        _callResult->setExecResult(abi.abiIn("", (u256)retCode));
        return false;
    }
    return true;
}

bool GasChargeManagePrecompiled::checkParams(PrecompiledExecResult::Ptr _callResult,
    std::shared_ptr<ExecutiveContext> _context, Address const& _userAccount, u256 const& _gasValue,
    Address const& _origin, Address const& _sender)
{
    ContractABI abi;
    // check the gasValue
    if (_gasValue == 0)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("GasChargeManagePrecompiled")
                                 << LOG_DESC("Invalid gasValue: must be larger than 0")
                                 << LOG_KV("gasValue", _gasValue);
        _callResult->setExecResult(abi.abiIn("", (u256)CODE_GCM_INVALID_ZERO_GAS_VALUE));
        return false;
    }
    // check the account status
    if (!checkAccountStatus(_callResult, _context, _userAccount))
    {
        return false;
    }
    // check the origin
    auto chargerList = getChargerList(_context);
    if (!chargerList->count(_origin))
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("GasChargeManagePrecompiled")
                                 << LOG_DESC("permission denied") << LOG_KV("origin", _origin.hex())
                                 << LOG_KV("sender", _sender.hex());
        _callResult->setExecResult(abi.abiIn("", (u256)CODE_GCM_PERMISSION_DENIED));
        return false;
    }
    return true;
}

void GasChargeManagePrecompiled::deduct(PrecompiledExecResult::Ptr _callResult,
    std::shared_ptr<ExecutiveContext> _context, bytesConstRef _paramData, Address const& _origin,
    Address const& _sender)
{
    ContractABI abi;
    Address userAccount;
    u256 gasValue;
    abi.abiOut(_paramData, userAccount, gasValue);
    if (!checkParams(_callResult, _context, userAccount, gasValue, _origin, _sender))
    {
        return;
    }
    // get the remain gas
    auto accountRemainGas = _context->getState()->remainGas(userAccount);
    if (accountRemainGas < gasValue)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("GasChargeManagePrecompiled")
                                 << LOG_DESC("deduct failed for not enough remainGas")
                                 << LOG_KV("accountRemainGas", accountRemainGas)
                                 << LOG_KV("gasValue", gasValue);
        _callResult->setExecResult(abi.abiIn("", (u256)CODE_GCM_NOT_ENOUGH_REMAIN_GAS));
        return;
    }
    _context->getState()->updateRemainGas(userAccount, (accountRemainGas - gasValue));
    _callResult->setExecResult(
        abi.abiIn("", (u256)CODE_SUCCESS, _context->getState()->remainGas(userAccount)));
    PRECOMPILED_LOG(WARNING) << LOG_DESC("GasChargeManagePrecompiled deduct success")
                             << LOG_KV("user", userAccount.hex()) << LOG_KV("deductValue", gasValue)
                             << LOG_KV("charger", _origin.hex()) << LOG_KV("sender", _sender.hex());
}

void GasChargeManagePrecompiled::charge(PrecompiledExecResult::Ptr _callResult,
    std::shared_ptr<ExecutiveContext> _context, bytesConstRef _paramData, Address const& _origin,
    Address const& _sender)
{
    ContractABI abi;
    Address userAccount;
    u256 gasValue;
    abi.abiOut(_paramData, userAccount, gasValue);
    if (!checkParams(_callResult, _context, userAccount, gasValue, _origin, _sender))
    {
        return;
    }
    // get the remain gas
    auto accountRemainGas = _context->getState()->remainGas(userAccount);
    u256 updatedGasValue = accountRemainGas + gasValue;
    if (updatedGasValue < accountRemainGas || updatedGasValue < gasValue)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("GasChargeManagePrecompiled")
                                 << LOG_DESC("charge failed for overflow")
                                 << LOG_KV("accountRemainGas", accountRemainGas)
                                 << LOG_KV("gasValue", gasValue)
                                 << LOG_KV("updatedGasValue", updatedGasValue);
        _callResult->setExecResult(abi.abiIn("", (u256)CODE_GCM_CHARGED_GAS_OVERFLOW));
        return;
    }
    _context->getState()->updateRemainGas(userAccount, updatedGasValue);
    _callResult->setExecResult(
        abi.abiIn("", (u256)CODE_SUCCESS, _context->getState()->remainGas(userAccount)));
    PRECOMPILED_LOG(WARNING) << LOG_DESC("GasChargeManagePrecompiled charge success")
                             << LOG_KV("user", userAccount.hex())
                             << LOG_KV("chargedValue", gasValue) << LOG_KV("charger", _origin.hex())
                             << LOG_KV("sender", _sender.hex());
}

void GasChargeManagePrecompiled::queryRemainGas(PrecompiledExecResult::Ptr _callResult,
    ExecutiveContext::Ptr _context, bytesConstRef _paramData, Address const& _origin)
{
    ContractABI abi;
    Address userAccount;
    abi.abiOut(_paramData, userAccount);
    if (!checkAccountStatus(_callResult, _context, userAccount))
    {
        PRECOMPILED_LOG(WARNING) << LOG_DESC(
            "GasChargeManagePrecompiled: queryRemainGas for invalid account status");
        return;
    }
    // check permission: only the chargers or the user self can call this interface
    auto chargerList = getChargerList(_context);
    if (!chargerList->count(_origin) && (_origin != userAccount))
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("GasChargeManagePrecompiled")
                                 << LOG_DESC("queryRemainGas failed for no permission");
        _callResult->setExecResult(abi.abiIn("", (u256)CODE_GCM_PERMISSION_DENIED));
        return;
    }
    // return the latest remain gas
    auto remainGas = _context->getState()->remainGas(userAccount);
    _callResult->setExecResult(abi.abiIn("", (u256)remainGas));
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("GasChargeManagePrecompiled")
                           << LOG_DESC("queryRemainGas success") << LOG_KV("origin", _origin.hex())
                           << LOG_KV("queryAccount", userAccount.hex());
}