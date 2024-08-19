/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file AccountPrecompiled.cpp
 * @author: kyonGuo
 * @date 2022/9/26
 */

#include "AccountPrecompiled.h"
#include "../../vm/HostContext.h"
#include <regex>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::protocol;

const char* const ACCOUNT_METHOD_SET_ACCOUNT_STATUS = "setAccountStatus(uint8)";
const char* const ACCOUNT_METHOD_GET_ACCOUNT_STATUS = "getAccountStatus()";
const char* const AM_METHOD_GET_ACCOUNT_BALANCE = "getAccountBalance()";
const char* const AM_METHOD_ADD_ACCOUNT_BALANCE = "addAccountBalance(uint256)";
const char* const AM_METHOD_SUB_ACCOUNT_BALANCE = "subAccountBalance(uint256)";
const uint32_t AM_METHOD_RECEIVE_FALLBACK_SELECTOR = 1;


AccountPrecompiled::AccountPrecompiled(crypto::Hash::Ptr hashImpl) : Precompiled(hashImpl)
{
    name2Selector[ACCOUNT_METHOD_SET_ACCOUNT_STATUS] =
        getFuncSelector(ACCOUNT_METHOD_SET_ACCOUNT_STATUS, hashImpl);
    name2Selector[ACCOUNT_METHOD_GET_ACCOUNT_STATUS] =
        getFuncSelector(ACCOUNT_METHOD_GET_ACCOUNT_STATUS, hashImpl);
    name2Selector[AM_METHOD_GET_ACCOUNT_BALANCE] =
        getFuncSelector(AM_METHOD_GET_ACCOUNT_BALANCE, hashImpl);
    name2Selector[AM_METHOD_ADD_ACCOUNT_BALANCE] =
        getFuncSelector(AM_METHOD_ADD_ACCOUNT_BALANCE, hashImpl);
    name2Selector[AM_METHOD_SUB_ACCOUNT_BALANCE] =
        getFuncSelector(AM_METHOD_SUB_ACCOUNT_BALANCE, hashImpl);
}

std::shared_ptr<PrecompiledExecResult> AccountPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    // [tableName][actualParams]
    std::vector<std::string> dynamicParams;
    bytes param;
    codec.decode(_callParameters->input(), dynamicParams, param);
    auto accountTableName = dynamicParams.at(0);
    // get user call actual params
    auto originParam = ref(param);
    uint32_t func;
    if (originParam.size() == 0 &&
        blockContext.features().get(
            ledger::Features::Flag::bugfix_support_transfer_receive_fallback))
    {
        // Transfer to EOA operation, call receive() function
        func = AM_METHOD_RECEIVE_FALLBACK_SELECTOR;
    }
    else
    {
        func = getParamFunc(originParam);
    }
    bytesConstRef data = getParamData(originParam);
    auto table = _executive->storage().openTable(accountTableName);

    if (func == name2Selector[ACCOUNT_METHOD_SET_ACCOUNT_STATUS])
    {
        setAccountStatus(accountTableName, _executive, data, _callParameters);
    }
    else if (func == name2Selector[ACCOUNT_METHOD_GET_ACCOUNT_STATUS])
    {
        getAccountStatus(accountTableName, _executive, _callParameters);
    }
    else if (func == name2Selector[AM_METHOD_GET_ACCOUNT_BALANCE])
    {
        getAccountBalance(accountTableName, _executive, _callParameters);
    }
    else if (func == name2Selector[AM_METHOD_ADD_ACCOUNT_BALANCE])
    {
        addAccountBalance(accountTableName, _executive, data, _callParameters);
    }
    else if (func == name2Selector[AM_METHOD_SUB_ACCOUNT_BALANCE])
    {
        subAccountBalance(accountTableName, _executive, data, _callParameters);
    }
    else if (func == AM_METHOD_RECEIVE_FALLBACK_SELECTOR)
    {
        // Transfer to EOA operation
        // receive() fallback logic
        // Just return _callParameters, do noting
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("AccountPrecompiled")
                               << LOG_DESC("call receive() function. do nothing")
                               << LOG_KV("func", func);
    }
    else
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("AccountPrecompiled")
                              << LOG_DESC("call undefined function") << LOG_KV("func", func);
        BOOST_THROW_EXCEPTION(
            bcos::protocol::PrecompiledError("AccountPrecompiled call undefined function!"));
    }
    return _callParameters;
}


std::string AccountPrecompiled::getContractTableName(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const std::string_view& _address) const
{
    return _executive->getContractTableName(_address, _executive->isWasm(), false);
}

void AccountPrecompiled::setAccountStatus(const std::string& accountTableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const PrecompiledExecResult::Ptr& _callParameters) const
{
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    auto table = _executive->storage().openTable(accountTableName);
    if (!table)
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_TABLE_NOT_EXIST)));
        BOOST_THROW_EXCEPTION(PrecompiledError("Account table not exist!"));
        return;
    }
    const auto* accountMgrSender =
        blockContext.isWasm() ? ACCOUNT_MANAGER_NAME : ACCOUNT_MGR_ADDRESS;
    if (_callParameters->m_sender != accountMgrSender)
    {
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }

    uint8_t status = 0;
    codec.decode(data, status);

    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("AccountPrecompiled")
                          << LOG_DESC("setAccountStatus") << LOG_KV("account", accountTableName)
                          << LOG_KV("status", std::to_string(status));
    auto existEntry = _executive->storage().getRow(accountTableName, ACCOUNT_STATUS);
    // already exist status, check and move it to last status
    if (existEntry.has_value())
    {
        auto statusStr = std::string(existEntry->get());
        auto existStatus = boost::lexical_cast<uint8_t>(statusStr);
        // account already abolish, should not set any status to it
        if (existStatus == AccountStatus::abolish && status != AccountStatus::abolish)
        {
            PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                                  << LOG_BADGE("AccountPrecompiled")
                                  << LOG_DESC("account already abolish, should not set any status")
                                  << LOG_KV("account", accountTableName)
                                  << LOG_KV("status", std::to_string(status));
            BOOST_THROW_EXCEPTION(
                PrecompiledError("Account already abolish, should not set any status."));
        }
        _executive->storage().setRow(
            accountTableName, ACCOUNT_LAST_STATUS, std::move(existEntry.value()));
    }
    else
    {
        // first time
        Entry lastStatusEntry;
        lastStatusEntry.importFields({"0"});
        _executive->storage().setRow(
            accountTableName, ACCOUNT_LAST_STATUS, std::move(lastStatusEntry));
    }
    // set status and lastUpdateNumber
    Entry statusEntry;
    statusEntry.importFields({boost::lexical_cast<std::string>(status)});
    _executive->storage().setRow(accountTableName, ACCOUNT_STATUS, std::move(statusEntry));
    Entry lastUpdateEntry;
    lastUpdateEntry.importFields({boost::lexical_cast<std::string>(blockContext.number())});
    _executive->storage().setRow(accountTableName, ACCOUNT_LAST_UPDATE, std::move(lastUpdateEntry));
    _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
}

void AccountPrecompiled::getAccountStatus(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters) const
{
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    auto table = _executive->storage().openTable(tableName);
    if (!table)
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_TABLE_NOT_EXIST)));
        BOOST_THROW_EXCEPTION(PrecompiledError("Account table not exist!"));
        return;
    }
    uint8_t status = getAccountStatus(tableName, _executive);
    _callParameters->setExecResult(codec.encode(status));
}

uint8_t AccountPrecompiled::getAccountStatus(
    const std::string& account, const std::shared_ptr<executor::TransactionExecutive>& _executive)
{
    auto accountTable = getAccountTableName(account);
    auto entry = _executive->storage().getRow(accountTable, ACCOUNT_STATUS);
    auto lastUpdateEntry = _executive->storage().getRow(accountTable, ACCOUNT_LAST_UPDATE);
    if (!lastUpdateEntry.has_value())
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("AccountPrecompiled") << LOG_DESC("getAccountStatus")
                               << LOG_DESC(" Status row not exist, return 0 by default");
        return 0;
    }
    auto lastUpdateNumber = boost::lexical_cast<BlockNumber>(std::string(lastUpdateEntry->get()));
    const auto& blockContext = _executive->blockContext();
    std::string statusStr;
    if (blockContext.number() > lastUpdateNumber) [[likely]]
    {
        statusStr = std::string(entry->get());
    }
    else [[unlikely]]
    {
        auto lastStatusEntry = _executive->storage().getRow(accountTable, ACCOUNT_LAST_STATUS);
        statusStr = std::string(lastStatusEntry->get());
    }

    PRECOMPILED_LOG(TRACE) << LOG_BADGE("AccountPrecompiled") << BLOCK_NUMBER(blockContext.number())
                           << LOG_DESC("getAccountStatus")
                           << LOG_KV("lastUpdateNumber", lastUpdateNumber)
                           << LOG_KV("status", statusStr);
    auto status = boost::lexical_cast<uint8_t>(statusStr);
    return status;
}


void AccountPrecompiled::getAccountBalance(const std::string& accountTableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters) const
{
    u256 balance;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    auto table = _executive->storage().openTable(accountTableName);

    // if account table not exist in apps but usr exist, return 0 by default
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << BLOCK_NUMBER(blockContext.number())
                               << LOG_BADGE("AccountPrecompiled, getAccountBalance")
                               << LOG_DESC("Account table not exist, return 0 by default")
                               << LOG_KV("account", accountTableName);
        _callParameters->setExecResult(codec.encode(0));
        return;
    }
    auto entry = _executive->storage().getRow(accountTableName, ACCOUNT_BALANCE);
    if (!entry.has_value())
    {
        PRECOMPILED_LOG(TRACE) << BLOCK_NUMBER(blockContext.number())
                               << LOG_BADGE("AccountPrecompiled, getAccountBalance")
                               << LOG_DESC("balance not exist, return 0 by default")
                               << LOG_KV("account", accountTableName);
        _callParameters->setExecResult(codec.encode(0));
        return;
    }
    balance = u256(std::string(entry->get()));
    PRECOMPILED_LOG(TRACE) << BLOCK_NUMBER(blockContext.number())
                           << LOG_BADGE("AccountPrecompiled, getAccountBalance")
                           << LOG_DESC("get account balance success")
                           << LOG_KV("account", accountTableName)
                           << LOG_KV("balance", to_string(balance));

    _callParameters->setExecResult(codec.encode(balance));
}

void AccountPrecompiled::addAccountBalance(const std::string& accountTableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    PrecompiledExecResult::Ptr const& _callParameters) const
{
    u256 value;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(data, value);
    PRECOMPILED_LOG(DEBUG) << "AccountPrecompiled::addAccountBalance"
                           << LOG_KV("addAccountBalanceSender", _callParameters->m_origin)
                           << LOG_KV("accountTableName", accountTableName);
    // check sender
    const auto* addAccountBalanceSender =
        blockContext.isWasm() ? BALANCE_PRECOMPILED_NAME : BALANCE_PRECOMPILED_ADDRESS;
    if (!(_callParameters->m_sender == addAccountBalanceSender ||
            _callParameters->m_origin == BALANCE_PRECOMPILED_ADDRESS))
    {
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }

    // check account exist
    auto table = _executive->storage().openTable(accountTableName);
    if (!table.has_value()) [[unlikely]]
    {
        // table is not exist, this call form EVM_BALANCE_SENDER_ADDRESS, create it


        // get account hex from /sys/xxxxx or /apps/xxxxx
        std::smatch match;
        std::regex_search(accountTableName, match, std::regex("/([^/]+)$"));
        auto accountHex = match[1].str();

        PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                              << LOG_BADGE("AccountPrecompiled, addAccountBalance")
                              << LOG_DESC("table not exist, create it")
                              << LOG_KV("account", accountHex);
        auto accountTable = getAccountTableName(accountHex);
        // create account table
        std::string codeString = getDynamicPrecompiledCodeString(ACCOUNT_ADDRESS, accountTable);
        auto input = codec.encode(accountTable, codeString);

        auto response = externalRequest(_executive, ref(input), _callParameters->m_origin,
            _callParameters->m_codeAddress, accountHex, false, true, _callParameters->m_gasLeft,
            true);

        if (response->status != (int32_t)TransactionStatus::None)
        {
            PRECOMPILED_LOG(INFO) << LOG_BADGE("AccountPrecompiled")
                                  << LOG_DESC("createAccount failed")
                                  << LOG_KV("accountTableName", accountTableName)
                                  << LOG_KV("status", response->status);
            BOOST_THROW_EXCEPTION(PrecompiledError("Create account error."));
        }
    }

    // ensure here accountTable must exist， add balance
    auto entry = _executive->storage().getRow(accountTableName, ACCOUNT_BALANCE);
    if (entry.has_value())
    {
        u256 balance = u256(std::string(entry->get()));
        if (balance + value < balance)
        {
            PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                                  << LOG_BADGE("AccountPrecompiled, addAccountBalance")
                                  << LOG_DESC("account balance overflow")
                                  << LOG_KV("account", accountTableName)
                                  << LOG_KV("balance", to_string(balance))
                                  << LOG_KV("add balance", to_string(value));
            BOOST_THROW_EXCEPTION(PrecompiledError("Account balance overflow!"));
            return;
        }
        balance += value;
        Entry Balance;
        Balance.importFields({boost::lexical_cast<std::string>(balance)});
        _executive->storage().setRow(accountTableName, ACCOUNT_BALANCE, std::move(Balance));
    }
    else
    {
        // first time
        Entry Balance;
        Balance.importFields({boost::lexical_cast<std::string>(value)});
        _executive->storage().setRow(accountTableName, ACCOUNT_BALANCE, std::move(Balance));
    }
    PRECOMPILED_LOG(TRACE) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("AccountPrecompiled")
                           << LOG_DESC("addAccountBalance") << LOG_KV("account", accountTableName)
                           << LOG_KV("add account balance success", to_string(value));

    _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
}

void AccountPrecompiled::subAccountBalance(const std::string& accountTableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    PrecompiledExecResult::Ptr const& _callParameters) const
{
    u256 value;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(data, value);

    PRECOMPILED_LOG(DEBUG) << "AccountPrecompiled::subAccountBalance"
                           << LOG_KV("subAccountBalanceSender", _callParameters->m_origin)
                           << LOG_KV("accountTableName", accountTableName);

    // check sender
    const auto* subAccountBalanceSender =
        blockContext.isWasm() ? BALANCE_PRECOMPILED_NAME : BALANCE_PRECOMPILED_ADDRESS;
    if (!(_callParameters->m_sender == subAccountBalanceSender ||
            _callParameters->m_origin == BALANCE_PRECOMPILED_ADDRESS))
    {
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }

    // check account exist
    auto table = _executive->storage().openTable(accountTableName);
    if (!table.has_value()) [[unlikely]]
    {
        PRECOMPILED_LOG(WARNING) << BLOCK_NUMBER(blockContext.number())
                                 << LOG_BADGE("AccountPrecompiled, subAccountBalance")
                                 << LOG_DESC("table not exist!");
        BOOST_THROW_EXCEPTION(NotEnoughCashError("Account table not exist, subBalance failed!"));
        return;
    }

    // check balance exist
    auto entry = _executive->storage().getRow(accountTableName, ACCOUNT_BALANCE);
    if (entry.has_value())
    {
        u256 balance = u256(std::string(entry->get()));
        // if balance not enough, revert
        if (balance < value)
        {
            PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number())
                                   << LOG_BADGE("AccountPrecompiled, subAccountBalance")
                                   << LOG_DESC("account balance not enough");
            BOOST_THROW_EXCEPTION(NotEnoughCashError("Account balance is not enough!"));
            return;
        }
        else
        {
            balance -= value;
            Entry Balance;
            Balance.importFields({boost::lexical_cast<std::string>(balance)});
            _executive->storage().setRow(accountTableName, ACCOUNT_BALANCE, std::move(Balance));
            _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
            return;
        }
    }
    else
    {
        // table exist, but ACCOUNT_BALANCE filed not exist
        Entry Balance;
        Balance.importFields({boost::lexical_cast<std::string>(0)});
        _executive->storage().setRow(accountTableName, ACCOUNT_BALANCE, std::move(Balance));
        BOOST_THROW_EXCEPTION(NotEnoughCashError("Account balance is not enough!"));
        return;
    }
}