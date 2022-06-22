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
 * @file ContractAuthMgrPrecompiled.cpp
 * @author: kyonGuo
 * @date 2022/4/15
 */

#include "ContractAuthMgrPrecompiled.h"
#include "AuthManagerPrecompiled.h"

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;

/// contract ACL
/// wasm
const char* const AUTH_METHOD_GET_ADMIN = "getAdmin(string)";
const char* const AUTH_METHOD_SET_ADMIN = "resetAdmin(string,string)";
const char* const AUTH_METHOD_SET_AUTH_TYPE = "setMethodAuthType(string,bytes4,uint8)";
const char* const AUTH_METHOD_OPEN_AUTH = "openMethodAuth(string,bytes4,string)";
const char* const AUTH_METHOD_CLOSE_AUTH = "closeMethodAuth(string,bytes4,string)";
const char* const AUTH_METHOD_CHECK_AUTH = "checkMethodAuth(string,bytes4,string)";
const char* const AUTH_METHOD_GET_AUTH = "getMethodAuth(string,bytes4)";
/// evm
const char* const AUTH_METHOD_GET_ADMIN_ADD = "getAdmin(address)";
const char* const AUTH_METHOD_SET_ADMIN_ADD = "resetAdmin(address,address)";
const char* const AUTH_METHOD_SET_AUTH_TYPE_ADD = "setMethodAuthType(address,bytes4,uint8)";
const char* const AUTH_METHOD_OPEN_AUTH_ADD = "openMethodAuth(address,bytes4,address)";
const char* const AUTH_METHOD_CLOSE_AUTH_ADD = "closeMethodAuth(address,bytes4,address)";
const char* const AUTH_METHOD_CHECK_AUTH_ADD = "checkMethodAuth(address,bytes4,address)";
const char* const AUTH_METHOD_GET_AUTH_ADD = "getMethodAuth(address,bytes4)";

/// contract status
const char* const AUTH_METHOD_SET_CONTRACT = "setContractStatus(address,bool)";
const char* const AUTH_METHOD_GET_CONTRACT = "contractAvailable(address)";

ContractAuthMgrPrecompiled::ContractAuthMgrPrecompiled(crypto::Hash::Ptr _hashImpl)
  : bcos::precompiled::Precompiled(_hashImpl)
{
    /// wasm
    name2Selector[AUTH_METHOD_GET_ADMIN] = getFuncSelector(AUTH_METHOD_GET_ADMIN, _hashImpl);
    name2Selector[AUTH_METHOD_SET_ADMIN] = getFuncSelector(AUTH_METHOD_SET_ADMIN, _hashImpl);
    name2Selector[AUTH_METHOD_SET_AUTH_TYPE] =
        getFuncSelector(AUTH_METHOD_SET_AUTH_TYPE, _hashImpl);
    name2Selector[AUTH_METHOD_OPEN_AUTH] = getFuncSelector(AUTH_METHOD_OPEN_AUTH, _hashImpl);
    name2Selector[AUTH_METHOD_CLOSE_AUTH] = getFuncSelector(AUTH_METHOD_CLOSE_AUTH, _hashImpl);
    name2Selector[AUTH_METHOD_CHECK_AUTH] = getFuncSelector(AUTH_METHOD_CHECK_AUTH, _hashImpl);
    name2Selector[AUTH_METHOD_GET_AUTH] = getFuncSelector(AUTH_METHOD_GET_AUTH, _hashImpl);

    /// evm
    name2Selector[AUTH_METHOD_GET_ADMIN_ADD] =
        getFuncSelector(AUTH_METHOD_GET_ADMIN_ADD, _hashImpl);
    name2Selector[AUTH_METHOD_SET_ADMIN_ADD] =
        getFuncSelector(AUTH_METHOD_SET_ADMIN_ADD, _hashImpl);
    name2Selector[AUTH_METHOD_SET_AUTH_TYPE_ADD] =
        getFuncSelector(AUTH_METHOD_SET_AUTH_TYPE_ADD, _hashImpl);
    name2Selector[AUTH_METHOD_OPEN_AUTH_ADD] =
        getFuncSelector(AUTH_METHOD_OPEN_AUTH_ADD, _hashImpl);
    name2Selector[AUTH_METHOD_CLOSE_AUTH_ADD] =
        getFuncSelector(AUTH_METHOD_CLOSE_AUTH_ADD, _hashImpl);
    name2Selector[AUTH_METHOD_CHECK_AUTH_ADD] =
        getFuncSelector(AUTH_METHOD_CHECK_AUTH_ADD, _hashImpl);
    name2Selector[AUTH_METHOD_GET_AUTH_ADD] = getFuncSelector(AUTH_METHOD_GET_AUTH_ADD, _hashImpl);

    name2Selector[AUTH_METHOD_SET_CONTRACT] = getFuncSelector(AUTH_METHOD_SET_CONTRACT, _hashImpl);
    name2Selector[AUTH_METHOD_GET_CONTRACT] = getFuncSelector(AUTH_METHOD_GET_CONTRACT, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> ContractAuthMgrPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    // parse function name
    uint32_t func = getParamFunc(_callParameters->input());
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthMgrPrecompiled") << LOG_DESC("call")
                           << LOG_KV("func", func);
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    gasPricer->setMemUsed(_callParameters->input().size());

    auto blockContext = _executive->blockContext().lock();
    auto authAddress = blockContext->isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());

    if (_callParameters->m_sender != authAddress)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("sender is not from AuthManager")
                               << LOG_KV("sender", _callParameters->m_sender);
        BOOST_THROW_EXCEPTION(BCOS_ERROR(
            CODE_NO_AUTHORIZED, "ContractAuthMgrPrecompiled: sender is not from AuthManager"));
    }
    if (func == name2Selector[AUTH_METHOD_GET_ADMIN] ||
        func == name2Selector[AUTH_METHOD_GET_ADMIN_ADD])
    {
        getAdmin(_executive, gasPricer, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_ADMIN] ||
             func == name2Selector[AUTH_METHOD_SET_ADMIN_ADD])
    {
        resetAdmin(_executive, gasPricer, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_AUTH_TYPE] ||
             func == name2Selector[AUTH_METHOD_SET_AUTH_TYPE_ADD])
    {
        setMethodAuthType(_executive, gasPricer, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_OPEN_AUTH] ||
             func == name2Selector[AUTH_METHOD_OPEN_AUTH_ADD])
    {
        setMethodAuth(_executive, false, gasPricer, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_CLOSE_AUTH] ||
             func == name2Selector[AUTH_METHOD_CLOSE_AUTH_ADD])
    {
        setMethodAuth(_executive, true, gasPricer, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_CHECK_AUTH] ||
             func == name2Selector[AUTH_METHOD_CHECK_AUTH_ADD])
    {
        checkMethodAuth(_executive, gasPricer, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_GET_AUTH] ||
             func == name2Selector[AUTH_METHOD_GET_AUTH_ADD])
    {
        getMethodAuth(_executive, gasPricer, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_CONTRACT])
    {
        setContractStatus(_executive, gasPricer, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_GET_CONTRACT])
    {
        contractAvailable(_executive, gasPricer, _callParameters);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
        BOOST_THROW_EXCEPTION(bcos::protocol::PrecompiledError(
            "ContractAuthMgrPrecompiled call undefined function!"));
    }
    gasPricer->updateMemUsed(_callParameters->m_execResult.size());
    _callParameters->setGas(_callParameters->m_gas - gasPricer->calTotalGas());
    return _callParameters;
}

void ContractAuthMgrPrecompiled::getAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)

{
    /// evm:  getAdmin(address) => string admin
    /// wasm: getAdmin(string)  => string admin

    std::string path;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (blockContext->isWasm())
    {
        codec.decode(_callParameters->params(), path);
    }
    else
    {
        Address contractAddress;
        codec.decode(_callParameters->params(), contractAddress);
        path = contractAddress.hex();
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled") << LOG_DESC("getAdmin")
                           << LOG_KV("path", path);
    path = getAuthTableName(path);

    auto lastStorage = _executive->lastStorage();
    auto table =
        (lastStorage) ? lastStorage->openTable(path) : _executive->storage().openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("path not found") << LOG_KV("path", path);
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Contract address not found."));
    }
    auto entry = table->getRow(ADMIN_FIELD);
    if (!entry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("entry not found") << LOG_KV("path", path);
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Contract Admin row not found."));
    }
    std::string adminStr = std::string(entry->getField(0));
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                           << LOG_DESC("getAdmin success") << LOG_KV("admin", adminStr);
    gasPricer->updateMemUsed(1);
    _callParameters->setExecResult(codec.encode(adminStr));
}

void ContractAuthMgrPrecompiled::resetAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// evm:  resetAdmin(address path,address admin) => int256
    /// wasm: resetAdmin(string  path,string  admin) => int256

    std::string path;
    std::string admin;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        Address adminAddress;
        codec.decode(_callParameters->params(), contractAddress, adminAddress);
        path = contractAddress.hex();
        admin = adminAddress.hex();
    }
    else
    {
        codec.decode(_callParameters->params(), path, admin);
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled") << LOG_DESC("resetAdmin")
                           << LOG_KV("path", path) << LOG_KV("admin", admin);
    path = getAuthTableName(path);
    auto table = _executive->storage().openTable(path);
    if (!table || !table->getRow(ADMIN_FIELD))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("resetAdmin: path not found") << LOG_KV("path", path);
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Contract address not found."));
    }
    auto newEntry = table->newEntry();
    newEntry.setField(SYS_VALUE, admin);
    table->setRow(ADMIN_FIELD, std::move(newEntry));
    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Set);
    getErrorCodeOut(_callParameters->mutableExecResult(), CODE_SUCCESS, codec);
}

void ContractAuthMgrPrecompiled::setMethodAuthType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)

{
    /// evm:  setMethodAuthType(address path, bytes4 func, uint8 type) => int256
    /// wasm: setMethodAuthType(string  path, bytes4 func, uint8 type) => int256

    std::string path;
    string32 _func;
    string32 _type;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        codec.decode(_callParameters->params(), contractAddress, _func, _type);
        path = contractAddress.hex();
    }
    else
    {
        codec.decode(_callParameters->params(), path, _func, _type);
    }
    bytes func = codec::fromString32(_func).ref().getCroppedData(0, 4).toBytes();
    uint8_t type = _type[_type.size() - 1];
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                           << LOG_DESC("setMethodAuthType") << LOG_KV("path", path)
                           << LOG_KV("func", toHexStringWithPrefix(func)) << LOG_KV("type", type);
    path = getAuthTableName(path);
    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("path not found") << LOG_KV("path", path);
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
        return;
    }
    auto entry = table->getRow(METHOD_AUTH_TYPE);
    if (!entry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("method_auth_type row not found")
                               << LOG_KV("path", path);
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_TABLE_AUTH_ROW_NOT_EXIST, codec);
        return;
    }

    std::string authTypeStr = std::string(entry->getField(SYS_VALUE));
    std::map<bytes, uint8_t> methAuthTypeMap;
    if (!authTypeStr.empty())
    {
        auto&& out = asBytes(authTypeStr);
        codec::scale::decode(methAuthTypeMap, gsl::make_span(out));
    }
    // covered writing
    methAuthTypeMap[func] = type;
    entry->setField(SYS_VALUE, asString(codec::scale::encode(methAuthTypeMap)));
    table->setRow(METHOD_AUTH_TYPE, std::move(entry.value()));

    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Set);
    getErrorCodeOut(_callParameters->mutableExecResult(), CODE_SUCCESS, codec);
}

void ContractAuthMgrPrecompiled::checkMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)

{
    /// evm:  checkMethodAuth(address path, bytes4 func, address account) => bool
    /// wasm: checkMethodAuth(string  path, bytes4 func, string  account) => bool

    std::string path;
    string32 _func;
    std::string account;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        Address accountAddress;
        codec.decode(_callParameters->params(), contractAddress, _func, accountAddress);
        path = contractAddress.hex();
        account = accountAddress.hex();
    }
    else
    {
        codec.decode(_callParameters->params(), path, _func, account);
    }
    auto result = checkMethodAuth(
        _executive, path, codec::fromString32(_func).ref().getCroppedData(0, 4), account);
    _callParameters->setExecResult(codec.encode(result));
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
}

bool ContractAuthMgrPrecompiled::checkMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const std::string& _path,
    bytesRef func, const std::string& account)
{
    auto path = _path;
    path = getAuthTableName(path);
    auto lastStorage = _executive->lastStorage();
    auto table =
        (lastStorage) ? lastStorage->openTable(path) : _executive->storage().openTable(path);
    if (!table)
    {
        // only precompiled contract in /sys/, or pre-built-in contract
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("auth table not found, auth pass through by default.")
                               << LOG_KV("path", path);
        return true;
    }
    try
    {
        auto getMethodType = getMethodAuthType(_executive, path, func);
        if (getMethodType == (int)CODE_TABLE_AUTH_TYPE_NOT_EXIST)
        {
            // this method not set type
            return true;
        }
        auto&& authMap = getMethodAuth(_executive, path, getMethodType);

        if (authMap.empty())
        {
            PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                                   << LOG_DESC("auth row not found, no method set acl")
                                   << LOG_KV("path", path) << LOG_KV("authType", getMethodType);
            // if entry still empty
            //      if white list mode, return false
            //      if black list mode， return true
            return getMethodType == (int)AuthType::BLACK_LIST_MODE;
        }
        if (authMap.find(func.toBytes()) == authMap.end())
        {
            // func not set acl, pass through
            return true;
        }
        if (authMap.at(func.toBytes()).find(account) == authMap.at(func.toBytes()).end())
        {
            // can't find account in user map
            // if white list mode, return false
            // if black list mode, return true
            return getMethodType == (int)AuthType::BLACK_LIST_MODE;
        }
        if (getMethodType == (int)AuthType::BLACK_LIST_MODE)
        {
            return !authMap.at(func.toBytes()).at(account);
        }
        return authMap.at(func.toBytes()).at(account);
    }
    catch (...)
    {
        // getMethodAuth will throw PrecompiledError
        return true;
    }
}

void ContractAuthMgrPrecompiled::getMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)

{
    /// evm:  getMethodAuth(address,bytes4) => (uint8,string[],string[])
    /// wasm: getMethodAuth(string,bytes4) => (uint8,string[],string[])

    std::string path;
    string32 _func;
    bytes funcBytes;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        codec.decode(_callParameters->params(), contractAddress, _func);
        path = contractAddress.hex();
    }
    else
    {
        codec.decode(_callParameters->params(), path, _func);
    }
    funcBytes = codec::fromString32(_func).ref().getCroppedData(0, 4).toBytes();
    path = getAuthTableName(path);
    auto getMethodType = getMethodAuthType(_executive, path, ref(funcBytes));
    if (getMethodType == (int)CODE_TABLE_AUTH_TYPE_NOT_EXIST)
    {
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Auth ACL type not found"));
    }
    auto&& authMap = getMethodAuth(_executive, path, getMethodType);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);

    if (authMap.empty())
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("auth row not found, no method set acl")
                               << LOG_KV("path", path);
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Auth ACL row not found"));
    }
    std::vector<std::string> accessList = {};
    std::vector<std::string> blockList = {};
    if (authMap.find(funcBytes) == authMap.end())
    {
        // func not set acl, return empty
        _callParameters->setExecResult(
            codec.encode(uint8_t(0), std::move(accessList), std::move(blockList)));
    }
    else
    {
        for (const auto& addressPair : authMap.at(funcBytes))
        {
            bool access = addressPair.second ? (getMethodType == (int)AuthType::WHITE_LIST_MODE) :
                                               (getMethodType == (int)AuthType::BLACK_LIST_MODE);
            if (access)
            {
                accessList.emplace_back(std::move(addressPair.first));
            }
            else
            {
                blockList.emplace_back(std::move(addressPair.first));
            }
        }
        _callParameters->setExecResult(
            codec.encode(uint8_t(getMethodType), std::move(accessList), std::move(blockList)));
    }
    gasPricer->updateMemUsed(authMap.size());
}

void ContractAuthMgrPrecompiled::setMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bool _isClose,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)

{
    std::string path;
    std::string account;
    string32 _func;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), path, _func, account);
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        Address accountAddress;
        codec.decode(_callParameters->params(), contractAddress, _func, accountAddress);
        path = contractAddress.hex();
        account = accountAddress.hex();
    }
    else
    {
        codec.decode(_callParameters->params(), path, _func, account);
    }
    bytes func = codec::fromString32(_func).ref().getCroppedData(0, 4).toBytes();
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled") << LOG_DESC("setAuth")
                           << LOG_KV("path", path) << LOG_KV("func", toHexStringWithPrefix(func))
                           << LOG_KV("account", account);
    path = getAuthTableName(path);
    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("auth table not found, should not set acl")
                               << LOG_KV("path", path);
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
        return;
    }
    s256 authType = getMethodAuthType(_executive, path, ref(func));
    std::string getTypeStr;
    if (authType == (int)AuthType::WHITE_LIST_MODE)
        getTypeStr = METHOD_AUTH_WHITE;
    else if (authType == (int)AuthType::BLACK_LIST_MODE)
        getTypeStr = METHOD_AUTH_BLACK;
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("error auth type") << LOG_KV("path", path)
                               << LOG_KV("type", authType);
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_TABLE_ERROR_AUTH_TYPE, codec);
        return;
    }
    auto entry = table->getRow(getTypeStr);
    if (!entry)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("auth row not found, try to set new one")
                               << LOG_KV("path", path) << LOG_KV("Type", getTypeStr);
        entry = table->newEntry();
        entry->setField(SYS_VALUE, "");
    }

    bool access = _isClose ? (authType == (int)AuthType::BLACK_LIST_MODE) :
                             (authType == (int)AuthType::WHITE_LIST_MODE);

    auto userAuth = std::make_pair(account, access);

    MethodAuthMap authMap;
    if (entry->getField(SYS_VALUE).empty())
    {
        // first insert func
        authMap.insert({func, {std::move(userAuth)}});
    }
    else
    {
        try
        {
            auto&& out = asBytes(std::string(entry->getField(SYS_VALUE)));
            codec::scale::decode(authMap, gsl::make_span(out));
            if (authMap.find(func) != authMap.end())
            {
                authMap.at(func)[account] = access;
            }
            else
            {
                authMap.insert({func, {std::move(userAuth)}});
            }
        }
        catch (...)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthMgrPrecompiled")
                                   << LOG_DESC("auth map parse error") << LOG_KV("path", path);
            getErrorCodeOut(
                _callParameters->mutableExecResult(), CODE_TABLE_AUTH_ROW_NOT_EXIST, codec);
            return;
        }
    }
    entry->setField(SYS_VALUE, asString(codec::scale::encode(authMap)));
    table->setRow(getTypeStr, std::move(entry.value()));
    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Set);
    getErrorCodeOut(_callParameters->mutableExecResult(), CODE_SUCCESS, codec);
}

int32_t ContractAuthMgrPrecompiled::getMethodAuthType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const std::string& _path,
    bytesConstRef _func)
{
    auto lastStorage = _executive->lastStorage();
    auto table =
        (lastStorage) ? lastStorage->openTable(_path) : _executive->storage().openTable(_path);
    // _table can't be nullopt
    auto entry = table->getRow(METHOD_AUTH_TYPE);
    if (!entry || entry->getField(SYS_VALUE).empty())
    {
        PRECOMPILED_LOG(DEBUG)
            << LOG_BADGE("ContractAuthMgrPrecompiled")
            << LOG_DESC("no acl strategy exist, should set the method access auth type firstly");
        return (int)CODE_TABLE_AUTH_TYPE_NOT_EXIST;
    }
    std::string authTypeStr = std::string(entry->getField(SYS_VALUE));
    std::map<bytes, uint8_t> authTypeMap;
    auto s = _func.toString();
    try
    {
        auto&& out = asBytes(authTypeStr);
        codec::scale::decode(authTypeMap, gsl::make_span(out));
        if (authTypeMap.find(_func.toBytes()) == authTypeMap.end())
        {
            return (int)CODE_TABLE_AUTH_TYPE_NOT_EXIST;
        }
        return authTypeMap.at(_func.toBytes());
    }
    catch (...)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("decode method type error");
        return (int)CODE_TABLE_AUTH_TYPE_DECODE_ERROR;
    }
}

MethodAuthMap ContractAuthMgrPrecompiled::getMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const std::string& path,
    int32_t getMethodType) const
{
    auto lastStorage = _executive->lastStorage();
    auto table =
        (lastStorage) ? lastStorage->openTable(path) : _executive->storage().openTable(path);
    if (!table)
    {
        // only precompiled contract in /sys/, or pre-built-in contract
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("auth table not found.") << LOG_KV("path", path);
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Auth table not found"));
    }
    std::string getTypeStr =
        getMethodType == (int)AuthType::WHITE_LIST_MODE ? METHOD_AUTH_WHITE : METHOD_AUTH_BLACK;

    auto entry = table->getRow(getTypeStr);
    MethodAuthMap authMap = {};
    if (!entry || entry->getField(SYS_VALUE).empty())
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("auth row not found, no method set acl")
                               << LOG_KV("path", path) << LOG_KV("authType", getTypeStr);
        return authMap;
    }
    bytes&& out = asBytes(std::string(entry->getField(SYS_VALUE)));
    codec::scale::decode(authMap, gsl::make_span(out));
    return authMap;
}

void ContractAuthMgrPrecompiled::setContractStatus(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)

{
    /// setContractStatus(address _addr, bool isFreeze) => int256

    std::string address;
    bool isFreeze = false;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (blockContext->isWasm())
    {
        codec.decode(_callParameters->params(), address, isFreeze);
    }
    else
    {
        Address contractAddress;
        codec.decode(_callParameters->params(), contractAddress, isFreeze);
        address = contractAddress.hex();
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                           << LOG_DESC("setContractStatus") << LOG_KV("address", address)
                           << LOG_KV("isFreeze", isFreeze);
    auto path = getAuthTableName(address);
    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("auth table not found") << LOG_KV("path", path);
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
        return;
    }
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    auto status = isFreeze ? CONTRACT_FROZEN : CONTRACT_NORMAL;
    Entry entry = {};
    entry.importFields({status});
    table->setRow("status", std::move(entry));
    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Set);
    getErrorCodeOut(_callParameters->mutableExecResult(), CODE_SUCCESS, codec);
}

void ContractAuthMgrPrecompiled::contractAvailable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)

{
    /// contractAvailable(address _addr) => bool

    std::string address;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (blockContext->isWasm())
    {
        codec.decode(_callParameters->params(), address);
    }
    else
    {
        Address contractAddress;
        codec.decode(_callParameters->params(), contractAddress);
        address = contractAddress.hex();
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                           << LOG_DESC("contractAvailable") << LOG_KV("address", address);
    auto result = getContractStatus(_executive, address);
    // result !=0 && result != 1
    if (result >> 1)
    {
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Cannot get contract status"));
    }
    _callParameters->setExecResult(codec.encode(result));
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
}

int32_t ContractAuthMgrPrecompiled::getContractStatus(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const std::string& _path)
{
    auto path = getAuthTableName(_path);
    auto lastStorage = _executive->lastStorage();
    auto table =
        (lastStorage) ? lastStorage->openTable(path) : _executive->storage().openTable(path);
    if (!table)
    {
        // only precompiled contract in /sys/, or pre-built-in contract
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("auth table not found, auth pass through by default.")
                               << LOG_KV("path", path);
        return (int)CODE_TABLE_NOT_EXIST;
    }
    auto entry = table->getRow("status");
    if (!entry)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC(
                                      "auth status row not found, auth pass through by default.");
        return (int)CODE_TABLE_AUTH_ROW_NOT_EXIST;
    }
    return entry->get() == CONTRACT_NORMAL;
}