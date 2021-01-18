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
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    do
    {
        if (funcSelector == name2Selector[GCM_METHOD_CHARGE_STR])
        {
            charge(callResult, _context, paramData, _origin, _sender);
            break;
        }
        if (funcSelector == name2Selector[GCM_METHOD_DEDUCT_STR])
        {
            deduct(callResult, _context, paramData, _origin, _sender);
            break;
        }
        if (funcSelector == name2Selector[GCM_METHOD_QUERY_GAS_STR])
        {
            queryRemainGas(callResult, _context, paramData, _origin);
            break;
        }
        if (funcSelector == name2Selector[GCM_METHOD_GRANT_CHARGER_STR])
        {
            grantCharger(callResult, _context, paramData, _origin, _sender);
            break;
        }
        if (funcSelector == name2Selector[GCM_METHOD_REVOKE_CHARGER_STR])
        {
            revokeCharger(callResult, _context, paramData, _origin, _sender);
            break;
        }
        if (funcSelector == name2Selector[GCM_METHOD_QUERY_CHARGERS_STR])
        {
            listChargers(callResult, _context);
            break;
        }
        PRECOMPILED_LOG(WARNING) << LOG_DESC("GasChargeManagePrecompiled: undefined function")
                                 << LOG_KV("funcSelector", funcSelector);
        dev::eth::ContractABI contractABI;
        callResult->setExecResult(contractABI.abiIn("", (u256)CODE_GCM_UNDEFINED_FUNCTION));
    } while (0);
    return callResult;
}

std::shared_ptr<std::set<Address>> GasChargeManagePrecompiled::getChargerList(
    std::shared_ptr<ExecutiveContext> _context)
{
    // get charger list from the _sys_config_ table
    auto result = dev::precompiled::getSystemConfigByKey(
        _context->stateStorage(), SYSTEM_KEY_CHARGER_LIST, _context->blockInfo().number);

    PRECOMPILED_LOG(DEBUG) << LOG_DESC("getChargerList") << LOG_KV("chargerList", result->first);
    return convertStringToAddressSet(result->first, c_chargerListSplitStr);
}

// check the account status
int GasChargeManagePrecompiled::checkAccountStatus(
    ExecutiveContext::Ptr _context, Address const& _userAccount)
{
    auto tableName = dev::precompiled::getContractTableName(_userAccount);
    auto accountStatusRet = dev::precompiled::getAccountStatus(_context, tableName);
    int retCode = dev::precompiled::parseAccountStatus(accountStatusRet.first);
    if (retCode != CODE_SUCCESS)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("GasChargeManagePrecompiled")
                                 << LOG_DESC("invalid account")
                                 << LOG_KV("userAccount", _userAccount.hex())
                                 << LOG_KV("retCode", retCode);
        return retCode;
    }
    return CODE_SUCCESS;
}

int GasChargeManagePrecompiled::checkParams(std::shared_ptr<ExecutiveContext> _context,
    Address const& _userAccount, u256 const& _gasValue, Address const& _origin,
    Address const& _sender)
{
    // check the gasValue
    if (_gasValue == 0)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("GasChargeManagePrecompiled")
                                 << LOG_DESC("Invalid gasValue: must be larger than 0")
                                 << LOG_KV("userAccount", _userAccount.hex())
                                 << LOG_KV("gasValue", _gasValue);
        return CODE_GCM_INVALID_ZERO_GAS_VALUE;
    }
    // check the account status
    auto retCode = checkAccountStatus(_context, _userAccount);
    // Note: if the account doesn not exist, updateRemainGas will create the account
    if (retCode != CODE_SUCCESS && retCode != CODE_ACCOUNT_NOT_EXIST)
    {
        return retCode;
    }
    // check the origin
    auto chargerList = getChargerList(_context);
    if (!chargerList->count(_origin))
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("GasChargeManagePrecompiled")
                                 << LOG_DESC("permission denied") << LOG_KV("origin", _origin.hex())
                                 << LOG_KV("sender", _sender.hex());
        return CODE_GCM_PERMISSION_DENIED;
    }
    return CODE_SUCCESS;
}

void GasChargeManagePrecompiled::deduct(PrecompiledExecResult::Ptr _callResult,
    std::shared_ptr<ExecutiveContext> _context, bytesConstRef _paramData, Address const& _origin,
    Address const& _sender)
{
    ContractABI abi;
    Address userAccount;
    u256 gasValue;
    abi.abiOut(_paramData, userAccount, gasValue);
    auto retCode = checkParams(_context, userAccount, gasValue, _origin, _sender);
    if (retCode != CODE_SUCCESS)
    {
        _callResult->setExecResult(abi.abiIn("", (u256)retCode, (u256)0));
        return;
    }
    // get the remain gas
    auto accountRemainGas = _context->getState()->remainGas(userAccount);
    if (accountRemainGas.second < gasValue)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("GasChargeManagePrecompiled")
                                 << LOG_DESC("deduct failed for not enough remainGas")
                                 << LOG_KV("accountRemainGas", accountRemainGas.second)
                                 << LOG_KV("gasValue", gasValue);
        _callResult->setExecResult(abi.abiIn("", (u256)CODE_GCM_NOT_ENOUGH_REMAIN_GAS, (u256)0));
        return;
    }
    _context->getState()->updateRemainGas(
        userAccount, (accountRemainGas.second - gasValue), _origin);
    auto remainGas = _context->getState()->remainGas(userAccount).second;
    _callResult->setExecResult(abi.abiIn("", (u256)CODE_SUCCESS, remainGas));
    PRECOMPILED_LOG(INFO) << LOG_DESC("GasChargeManagePrecompiled deduct success")
                          << LOG_KV("user", userAccount.hex()) << LOG_KV("deductValue", gasValue)
                          << LOG_KV("remainGas", remainGas)
                          << LOG_KV("onChainNum", _context->blockInfo().number + 1)
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
    auto retCode = checkParams(_context, userAccount, gasValue, _origin, _sender);
    if (retCode != CODE_SUCCESS)
    {
        _callResult->setExecResult(abi.abiIn("", (u256)retCode, u256(0)));
        return;
    }
    // get the remain gas
    auto accountRemainGas = _context->getState()->remainGas(userAccount);
    u256 updatedGasValue = accountRemainGas.second + gasValue;
    if (updatedGasValue < accountRemainGas.second || updatedGasValue < gasValue)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("GasChargeManagePrecompiled")
                                 << LOG_DESC("charge failed for overflow")
                                 << LOG_KV("accountRemainGas", accountRemainGas.second)
                                 << LOG_KV("gasValue", gasValue)
                                 << LOG_KV("updatedGasValue", updatedGasValue);
        _callResult->setExecResult(abi.abiIn("", (u256)CODE_GCM_CHARGED_GAS_OVERFLOW, (u256)0));
        return;
    }
    _context->getState()->updateRemainGas(userAccount, updatedGasValue, _origin);
    auto remainGas = _context->getState()->remainGas(userAccount).second;
    _callResult->setExecResult(abi.abiIn("", (u256)CODE_SUCCESS, remainGas));
    PRECOMPILED_LOG(INFO) << LOG_DESC("GasChargeManagePrecompiled charge success")
                          << LOG_KV("user", userAccount.hex()) << LOG_KV("chargedValue", gasValue)
                          << LOG_KV("remainGas", remainGas)
                          << LOG_KV("onChainNum", _context->blockInfo().number + 1)
                          << LOG_KV("charger", _origin.hex()) << LOG_KV("sender", _sender.hex());
}

void GasChargeManagePrecompiled::queryRemainGas(PrecompiledExecResult::Ptr _callResult,
    ExecutiveContext::Ptr _context, bytesConstRef _paramData, Address const& _origin)
{
    ContractABI abi;
    Address userAccount;
    abi.abiOut(_paramData, userAccount);
    auto retCode = checkAccountStatus(_context, userAccount);
    if (retCode != CODE_SUCCESS)
    {
        PRECOMPILED_LOG(WARNING) << LOG_DESC(
            "GasChargeManagePrecompiled: queryRemainGas for invalid account status");
        _callResult->setExecResult(abi.abiIn("", (u256)retCode, u256(0)));
        return;
    }
    // return the latest remain gas
    auto remainGas = _context->getState()->remainGas(userAccount);
    _callResult->setExecResult(abi.abiIn("", u256(CODE_SUCCESS), (u256)remainGas.second));
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("GasChargeManagePrecompiled")
                           << LOG_DESC("queryRemainGas success") << LOG_KV("origin", _origin.hex())
                           << LOG_KV("queryAccount", userAccount.hex());
}


void GasChargeManagePrecompiled::updateChargers(ExecutiveContext::Ptr _context,
    std::shared_ptr<std::set<Address>> _chargerList, Address const& _origin)
{
    std::string chargerListStr = "";
    for (auto const& charger : *_chargerList)
    {
        chargerListStr += charger.hex() + c_chargerListSplitStr;
    }
    SystemConfigPrecompiled::setSystemConfigByKey(_context, SYSTEM_KEY_CHARGER_LIST, chargerListStr,
        std::make_shared<AccessOptions>(_origin, false));
}

void GasChargeManagePrecompiled::grantCharger(PrecompiledExecResult::Ptr _callResult,
    ExecutiveContext::Ptr _context, bytesConstRef _paramData, Address const& _origin,
    Address const& _sender)
{
    ContractABI abi;
    Address chargerAccount;
    abi.abiOut(_paramData, chargerAccount);
    // check the account status, Since the charger is stored separately in the sys_config_ system
    // table, there is no need to guarantee that the charger must have an account table
    auto retCode = checkAccountStatus(_context, chargerAccount);
    if (retCode != CODE_SUCCESS && retCode != CODE_ACCOUNT_NOT_EXIST)
    {
        _callResult->setExecResult(abi.abiIn("", (u256)retCode));
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("GasChargeManagePrecompiled")
                                 << LOG_DESC(
                                        "grantCharger failed for invalid status of granted charger")
                                 << LOG_KV("chargerAccount", chargerAccount.hex())
                                 << LOG_KV("_origin", _origin.hex()) << LOG_KV("retCode", retCode);
        return;
    }
    // check the origin, must be the  committee member
    if (!dev::precompiled::isCommitteeMember(_context, _origin))
    {
        _callResult->setExecResult(abi.abiIn("", (u256)CODE_GCM_GRANT_PERMISSION_DENIED));
        PRECOMPILED_LOG(WARNING)
            << LOG_BADGE("GasChargeManagePrecompiled")
            << LOG_DESC("grantCharger failed for the origin not the committee member")
            << LOG_KV("_origin", _origin.hex()) << LOG_KV("chargerAccount", chargerAccount.hex());

        return;
    }
    auto chargerList = getChargerList(_context);
    if (chargerList->count(chargerAccount))
    {
        _callResult->setExecResult(abi.abiIn("", (u256)CODE_GCM_CHARGER_ALREADY_EXISTS));
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("GasChargeManagePrecompiled")
                                 << LOG_DESC(
                                        "grantCharger failed for the granted account already exist")
                                 << LOG_KV("chargerAccount", chargerAccount.hex())
                                 << LOG_KV("origin", _origin.hex());
        return;
    }
    chargerList->insert(chargerAccount);
    updateChargers(_context, chargerList, _origin);
    _callResult->setExecResult(abi.abiIn("", (u256)CODE_SUCCESS));
    PRECOMPILED_LOG(INFO) << LOG_BADGE("GasChargeManagePrecompiled")
                          << LOG_DESC("grantCharger success")
                          << LOG_KV("grantedAccount", chargerAccount.hex())
                          << LOG_KV("origin", _origin.hex()) << LOG_KV("sender", _sender.hex());
}

void GasChargeManagePrecompiled::revokeCharger(PrecompiledExecResult::Ptr _callResult,
    ExecutiveContext::Ptr _context, bytesConstRef _paramData, Address const& _origin,
    Address const& _sender)
{
    ContractABI abi;
    Address chargerAccount;
    abi.abiOut(_paramData, chargerAccount);

    // check the origin, must be the  committee member
    if (!dev::precompiled::isCommitteeMember(_context, _origin))
    {
        _callResult->setExecResult(abi.abiIn("", (u256)CODE_GCM_REVOKE_PERMISSION_DENIED));
        PRECOMPILED_LOG(WARNING)
            << LOG_BADGE("GasChargeManagePrecompiled")
            << LOG_DESC("revokeCharger failed for the origin not the committee member")
            << LOG_KV("_origin", _origin.hex());

        return;
    }
    auto chargerList = getChargerList(_context);
    if (!chargerList->count(chargerAccount))
    {
        _callResult->setExecResult(abi.abiIn("", (u256)CODE_GCM_CHARGER_NOT_EXISTS));
        PRECOMPILED_LOG(WARNING)
            << LOG_BADGE("GasChargeManagePrecompiled")
            << LOG_DESC("revokeCharger failed for the revoked account does not exist")
            << LOG_KV("chargerAccount", chargerAccount.hex()) << LOG_KV("origin", _origin.hex());
        return;
    }
    chargerList->erase(chargerAccount);
    updateChargers(_context, chargerList, _origin);
    _callResult->setExecResult(abi.abiIn("", (u256)CODE_SUCCESS));
    PRECOMPILED_LOG(INFO) << LOG_BADGE("GasChargeManagePrecompiled")
                          << LOG_DESC("revokeCharger success")
                          << LOG_KV("revokedAccount", chargerAccount.hex())
                          << LOG_KV("origin", _origin.hex()) << LOG_KV("sender", _sender.hex());
}

void GasChargeManagePrecompiled::listChargers(
    PrecompiledExecResult::Ptr _callResult, ExecutiveContext::Ptr _context)
{
    ContractABI abi;
    auto chargerList = getChargerList(_context);
    std::vector<Address> chargerVec(chargerList->begin(), chargerList->end());
    _callResult->setExecResult(abi.abiIn("", chargerVec));
}