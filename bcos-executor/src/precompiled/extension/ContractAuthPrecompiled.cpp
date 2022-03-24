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
 * @file ContractAuthPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-10-09
 */

#include "ContractAuthPrecompiled.h"
#include "../../vm/HostContext.h"
#include <boost/core/ignore_unused.hpp>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;

/// wasm
const char* const AUTH_METHOD_GET_ADMIN = "getAdmin(string)";
const char* const AUTH_METHOD_SET_ADMIN = "resetAdmin(string,address)";
const char* const AUTH_METHOD_SET_AUTH_TYPE = "setMethodAuthType(string,bytes4,uint8)";
const char* const AUTH_METHOD_OPEN_AUTH = "openMethodAuth(string,bytes4,address)";
const char* const AUTH_METHOD_CLOSE_AUTH = "closeMethodAuth(string,bytes4,address)";
const char* const AUTH_METHOD_CHECK_AUTH = "checkMethodAuth(string,bytes4,address)";
/// evm
const char* const AUTH_METHOD_GET_ADMIN_ADD = "getAdmin(address)";
const char* const AUTH_METHOD_SET_ADMIN_ADD = "resetAdmin(address,address)";
const char* const AUTH_METHOD_SET_AUTH_TYPE_ADD = "setMethodAuthType(address,bytes4,uint8)";
const char* const AUTH_METHOD_OPEN_AUTH_ADD = "openMethodAuth(address,bytes4,address)";
const char* const AUTH_METHOD_CLOSE_AUTH_ADD = "closeMethodAuth(address,bytes4,address)";
const char* const AUTH_METHOD_CHECK_AUTH_ADD = "checkMethodAuth(address,bytes4,address)";

const char* const AUTH_METHOD_GET_DEPLOY_TYPE = "deployType()";
const char* const AUTH_METHOD_SET_DEPLOY_TYPE = "setDeployAuthType(uint8)";
const char* const AUTH_OPEN_DEPLOY_ACCOUNT = "openDeployAuth(address)";
const char* const AUTH_CLOSE_DEPLOY_ACCOUNT = "closeDeployAuth(address)";
const char* const AUTH_CHECK_DEPLOY_ACCESS = "hasDeployAuth(address)";

ContractAuthPrecompiled::ContractAuthPrecompiled(crypto::Hash::Ptr _hashImpl)
  : Precompiled(_hashImpl)
{
    /// wasm
    name2Selector[AUTH_METHOD_GET_ADMIN] = getFuncSelector(AUTH_METHOD_GET_ADMIN, _hashImpl);
    name2Selector[AUTH_METHOD_SET_ADMIN] = getFuncSelector(AUTH_METHOD_SET_ADMIN, _hashImpl);
    name2Selector[AUTH_METHOD_SET_AUTH_TYPE] =
        getFuncSelector(AUTH_METHOD_SET_AUTH_TYPE, _hashImpl);
    name2Selector[AUTH_METHOD_OPEN_AUTH] = getFuncSelector(AUTH_METHOD_OPEN_AUTH, _hashImpl);
    name2Selector[AUTH_METHOD_CLOSE_AUTH] = getFuncSelector(AUTH_METHOD_CLOSE_AUTH, _hashImpl);
    name2Selector[AUTH_METHOD_CHECK_AUTH] = getFuncSelector(AUTH_METHOD_CHECK_AUTH, _hashImpl);

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

    /// deploy
    name2Selector[AUTH_METHOD_GET_DEPLOY_TYPE] =
        getFuncSelector(AUTH_METHOD_GET_DEPLOY_TYPE, _hashImpl);
    name2Selector[AUTH_METHOD_SET_DEPLOY_TYPE] =
        getFuncSelector(AUTH_METHOD_SET_DEPLOY_TYPE, _hashImpl);
    name2Selector[AUTH_OPEN_DEPLOY_ACCOUNT] = getFuncSelector(AUTH_OPEN_DEPLOY_ACCOUNT, _hashImpl);
    name2Selector[AUTH_CLOSE_DEPLOY_ACCOUNT] =
        getFuncSelector(AUTH_CLOSE_DEPLOY_ACCOUNT, _hashImpl);
    name2Selector[AUTH_CHECK_DEPLOY_ACCESS] = getFuncSelector(AUTH_CHECK_DEPLOY_ACCESS, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> ContractAuthPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string&, const std::string& _sender)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("call")
                           << LOG_KV("func", func);
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[AUTH_METHOD_GET_ADMIN] ||
        func == name2Selector[AUTH_METHOD_GET_ADMIN_ADD])
    {
        getAdmin(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_ADMIN] ||
             func == name2Selector[AUTH_METHOD_SET_ADMIN_ADD])
    {
        resetAdmin(_executive, data, callResult, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_AUTH_TYPE] ||
             func == name2Selector[AUTH_METHOD_SET_AUTH_TYPE_ADD])
    {
        setMethodAuthType(_executive, data, callResult, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_METHOD_OPEN_AUTH] ||
             func == name2Selector[AUTH_METHOD_OPEN_AUTH_ADD])
    {
        openMethodAuth(_executive, data, callResult, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_METHOD_CLOSE_AUTH] ||
             func == name2Selector[AUTH_METHOD_CLOSE_AUTH_ADD])
    {
        closeMethodAuth(_executive, data, callResult, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_METHOD_CHECK_AUTH] ||
             func == name2Selector[AUTH_METHOD_CHECK_AUTH_ADD])
    {
        auto blockContext = _executive->blockContext().lock();
        auto codec =
            std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
        bool result = checkMethodAuth(_executive, data);
        callResult->setExecResult(codec->encode(result));
    }
    else if (func == name2Selector[AUTH_METHOD_GET_DEPLOY_TYPE])
    {
        getDeployType(_executive, callResult, gasPricer);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_DEPLOY_TYPE])
    {
        setDeployType(_executive, data, callResult, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_OPEN_DEPLOY_ACCOUNT])
    {
        openDeployAuth(_executive, data, callResult, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_CLOSE_DEPLOY_ACCOUNT])
    {
        closeDeployAuth(_executive, data, callResult, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_CHECK_DEPLOY_ACCESS])
    {
        hasDeployAuth(_executive, data, callResult);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

void ContractAuthPrecompiled::getAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    std::string path;
    Address admin;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    if (blockContext->isWasm())
    {
        codec->decode(data, path);
    }
    else
    {
        Address contractAddress;
        codec->decode(data, contractAddress);
        path = contractAddress.hex();
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("getAdmin")
                           << LOG_KV("path", path);
    path = getAuthTableName(path);
    std::string adminStr = getContractAdmin(_executive, path);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("getAdmin success")
                           << LOG_KV("admin", adminStr);
    if (adminStr.empty())
    {
        callResult->setExecResult(codec->encode(Address()));
        return;
    }
    admin = Address(std::string(adminStr));
    gasPricer->updateMemUsed(1);
    callResult->setExecResult(codec->encode(admin));
}

void ContractAuthPrecompiled::resetAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _sender,
    const PrecompiledGas::Ptr& gasPricer)
{
    std::string path;
    Address admin;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        codec->decode(data, contractAddress, admin);
        path = contractAddress.hex();
    }
    else
    {
        codec->decode(data, path, admin);
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("resetAdmin")
                           << LOG_KV("path", path) << LOG_KV("admin", admin);
    if (!checkSender(_sender))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("sender is not from sys") << LOG_KV("path", path)
                               << LOG_KV("sender", _sender);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, *codec);
        return;
    }
    path = getAuthTableName(path);
    auto table = _executive->storage().openTable(path);
    if (!table || !table->getRow(ADMIN_FIELD))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("resetAdmin: path not found") << LOG_KV("path", path);
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError() << errinfo_comment("Contract address not found."));
    }
    auto newEntry = table->newEntry();
    newEntry.setField(SYS_VALUE, admin.hex());
    table->setRow(ADMIN_FIELD, std::move(newEntry));
    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Set);
    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, *codec);
}

void ContractAuthPrecompiled::setMethodAuthType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _sender,
    const PrecompiledGas::Ptr& gasPricer)
{
    std::string path;
    string32 _func;
    string32 _type;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        codec->decode(data, contractAddress, _func, _type);
        path = contractAddress.hex();
    }
    else
    {
        codec->decode(data, path, _func, _type);
    }
    bytes func = codec::fromString32(_func).ref().getCroppedData(0, 4).toBytes();
    u256 type = _type[_type.size() - 1];
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("setMethodAuthType")
                           << LOG_KV("path", path) << LOG_KV("func", toHexStringWithPrefix(func))
                           << LOG_KV("type", type);
    path = getAuthTableName(path);
    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("path not found")
                               << LOG_KV("path", path);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_NOT_EXIST, *codec);
        return;
    }
    auto admin = Address(getContractAdmin(_executive, path));
    if (Address(_sender) != admin)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("Permission denied, only admin can set contract access.")
                               << LOG_KV("address", path) << LOG_KV("sender", _sender);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, *codec);
        return;
    }
    auto entry = table->getRow(METHOD_AUTH_TYPE);
    if (!entry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("method_auth_type row not found")
                               << LOG_KV("path", path);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_AUTH_ROW_NOT_EXIST, *codec);
        return;
    }

    std::string authTypeStr = std::string(entry->getField(SYS_VALUE));
    std::map<bytes, u256> methAuthTypeMap;
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
    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, *codec);
}

void ContractAuthPrecompiled::openMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _sender,
    const PrecompiledGas::Ptr& gasPricer)
{
    setMethodAuth(_executive, data, callResult, false, _sender, gasPricer);
}

void ContractAuthPrecompiled::closeMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _sender,
    const PrecompiledGas::Ptr& gasPricer)
{
    setMethodAuth(_executive, data, callResult, true, _sender, gasPricer);
}

bool ContractAuthPrecompiled::checkMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data)
{
    std::string path;
    string32 _func;
    Address account;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        codec->decode(data, contractAddress, _func, account);
        path = contractAddress.hex();
    }
    else
    {
        codec->decode(data, path, _func, account);
    }
    return checkMethodAuth(
        _executive, path, codec::fromString32(_func).ref().getCroppedData(0, 4), account);
}

bool ContractAuthPrecompiled::checkMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const std::string& _path,
    bytesRef func, const Address& account)
{
    auto path = _path;
    path = getAuthTableName(path);
    auto lastStorage = _executive->lastStorage();
    auto table =
        (lastStorage) ? lastStorage->openTable(path) : _executive->storage().openTable(path);
    if (!table)
    {
        // only precompiled contract in /sys/, or pre-built-in contract
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("auth table not found, auth pass through by default.")
                               << LOG_KV("path", path);
        return true;
    }
    auto getMethodType = getMethodAuthType(_executive, path, func);
    if (getMethodType == (int)CODE_TABLE_AUTH_TYPE_NOT_EXIST)
    {
        // this method not set type
        return true;
    }
    std::string getTypeStr;
    if (getMethodType == (int)AuthType::WHITE_LIST_MODE)
        getTypeStr = METHOD_AUTH_WHITE;
    else if (getMethodType == (int)AuthType::BLACK_LIST_MODE)
        getTypeStr = METHOD_AUTH_BLACK;
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("error auth type") << LOG_KV("path", path)
                               << LOG_KV("type", getMethodType);
        return false;
    }

    auto entry = table->getRow(getTypeStr);
    if (!entry || entry->getField(SYS_VALUE).empty())
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("auth row not found, no method set acl")
                               << LOG_KV("path", path) << LOG_KV("authType", getTypeStr);
        // if entry still empty
        //      if white list mode, return false
        //      if black list mode， return true
        return getMethodType == (int)AuthType::BLACK_LIST_MODE;
    }
    MethodAuthMap authMap;
    bytes&& out = asBytes(std::string(entry->getField(SYS_VALUE)));
    codec::scale::decode(authMap, gsl::make_span(out));
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

void ContractAuthPrecompiled::setMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, bool _isClose,
    const std::string& _sender, const PrecompiledGas::Ptr& gasPricer)
{
    std::string path;
    Address account;
    string32 _func;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, path, _func, account);
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        codec->decode(data, contractAddress, _func, account);
        path = contractAddress.hex();
    }
    else
    {
        codec->decode(data, path, _func, account);
    }
    bytes func = codec::fromString32(_func).ref().getCroppedData(0, 4).toBytes();
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("setAuth")
                           << LOG_KV("path", path) << LOG_KV("func", toHexStringWithPrefix(func))
                           << LOG_KV("account", account.hex());
    path = getAuthTableName(path);
    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("auth table not found, should not set acl")
                               << LOG_KV("path", path);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_NOT_EXIST, *codec);
        return;
    }
    s256 authType = getMethodAuthType(_executive, path, ref(func));
    if (authType <= 0)
    {
        callResult->setExecResult(codec->encode(authType));
        return;
    }
    auto admin = Address(getContractAdmin(_executive, path));
    if (Address(_sender) != admin)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("Permission denied, only admin can set contract access.")
                               << LOG_KV("address", path) << LOG_KV("sender", _sender);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, *codec);
        return;
    }
    std::string getTypeStr;
    if (authType == (int)AuthType::WHITE_LIST_MODE)
        getTypeStr = METHOD_AUTH_WHITE;
    else if (authType == (int)AuthType::BLACK_LIST_MODE)
        getTypeStr = METHOD_AUTH_BLACK;
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("error auth type") << LOG_KV("path", path)
                               << LOG_KV("type", authType);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_ERROR_AUTH_TYPE, *codec);
        return;
    }
    auto entry = table->getRow(getTypeStr);
    if (!entry)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthPrecompiled")
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
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                                   << LOG_DESC("auth map parse error") << LOG_KV("path", path);
            getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_AUTH_ROW_NOT_EXIST, *codec);
            return;
        }
    }
    entry->setField(SYS_VALUE, asString(codec::scale::encode(authMap)));
    table->setRow(getTypeStr, std::move(entry.value()));
    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Set);
    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, *codec);
}

s256 ContractAuthPrecompiled::getMethodAuthType(
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
            << LOG_BADGE("ContractAuthPrecompiled")
            << LOG_DESC("no acl strategy exist, should set the method access auth type firstly");
        return (int)CODE_TABLE_AUTH_TYPE_NOT_EXIST;
    }
    std::string authTypeStr = std::string(entry->getField(SYS_VALUE));
    std::map<bytes, u256> authTypeMap;
    s256 type = -1;
    try
    {
        auto&& out = asBytes(authTypeStr);
        codec::scale::decode(authTypeMap, gsl::make_span(out));
        if (authTypeMap.find(_func.toBytes()) == authTypeMap.end())
        {
            return (int)CODE_TABLE_AUTH_TYPE_NOT_EXIST;
        }
        type = u2s(authTypeMap.at(_func.toBytes()));
    }
    catch (...)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("decode method type error");
        return (int)CODE_TABLE_AUTH_TYPE_DECODE_ERROR;
    }
    return type;
}

std::string ContractAuthPrecompiled::getContractAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const std::string& _path)
{
    auto lastStorage = _executive->lastStorage();
    auto table =
        (lastStorage) ? lastStorage->openTable(_path) : _executive->storage().openTable(_path);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("path not found")
                               << LOG_KV("path", _path);
        return "";
    }
    auto entry = table->getRow(ADMIN_FIELD);
    if (!entry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("entry not found") << LOG_KV("path", _path);
        return "";
    }
    return std::string(entry->getField(0));
}

u256 ContractAuthPrecompiled::getDeployAuthType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive)
{
    auto lastStorage = _executive->lastStorage();
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("getDeployAuthType") << (lastStorage ? "use lastStorage" :
                                                                                "use latestStorage");
    auto table =
        (lastStorage) ? lastStorage->openTable("/apps") : _executive->storage().openTable("/apps");
    // table must exist
    auto entry = table->getRow(FS_ACL_TYPE);
    // entry must exist
    u256 type = 0;
    try
    {
        type = boost::lexical_cast<u256>(std::string(entry->getField(0)));
    }
    catch (...)
    {
        return 0;
    }
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("getDeployAuthType") << LOG_KV("type", type);
    return type;
}

void ContractAuthPrecompiled::getDeployType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)

{
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());

    u256 type = getDeployAuthType(_executive);
    gasPricer->updateMemUsed(1);
    callResult->setExecResult(codec->encode(type));
}

void ContractAuthPrecompiled::setDeployType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _sender,
    const PrecompiledGas::Ptr& gasPricer)
{
    string32 _type;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, _type);
    if (!checkSender(_sender))
    {
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, *codec);
        return;
    }
    u256 type = _type[_type.size() - 1];
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("setDeployType")
                           << LOG_KV("type", type);
    if (type > 2)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("deploy auth type must be 1 or 2.");
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_ERROR_AUTH_TYPE, *codec);
        return;
    }
    auto table = _executive->storage().openTable("/apps");
    Entry entry;
    entry.importFields({boost::lexical_cast<std::string>(type)});
    table->setRow(FS_ACL_TYPE, std::move(entry));

    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Set);
    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, *codec);
}

void ContractAuthPrecompiled::openDeployAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _sender,
    const PrecompiledGas::Ptr& gasPricer)
{
    setDeployAuth(_executive, data, callResult, false, _sender, gasPricer);
}

void ContractAuthPrecompiled::closeDeployAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _sender,
    const PrecompiledGas::Ptr& gasPricer)
{
    setDeployAuth(_executive, data, callResult, true, _sender, gasPricer);
}

void ContractAuthPrecompiled::setDeployAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, bool _isClose,
    const std::string& _sender, const PrecompiledGas::Ptr& gasPricer)
{
    Address account;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, account);
    if (!checkSender(_sender))
    {
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, *codec);
        return;
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthPrecompiled") << LOG_DESC("setDeployAuth")
                           << LOG_KV("account", account.hex()) << LOG_KV("isClose", _isClose);
    auto table = _executive->storage().openTable("/apps");
    auto type = getDeployAuthType(_executive);
    auto getAclStr = (type == (int)AuthType::BLACK_LIST_MODE) ? FS_ACL_BLACK : FS_ACL_WHITE;

    std::map<Address, bool> aclMap;
    auto entry = table->getRow(getAclStr);
    auto mapStr = std::string(entry->getField(0));
    if (!mapStr.empty())
    {
        auto&& out = asBytes(mapStr);
        codec::scale::decode(aclMap, gsl::make_span(out));
    }
    bool access = _isClose ? (type == (int)AuthType::BLACK_LIST_MODE) :
                             (type == (int)AuthType::WHITE_LIST_MODE);
    // covered writing
    aclMap[account] = access;
    entry->setField(0, asString(codec::scale::encode(aclMap)));
    table->setRow(getAclStr, std::move(entry.value()));

    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Set);
    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, *codec);
}

void ContractAuthPrecompiled::hasDeployAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult)
{
    Address account;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, account);

    callResult->setExecResult(codec->encode(checkDeployAuth(_executive, account)));
}

bool ContractAuthPrecompiled::checkDeployAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const Address& _account)
{
    auto lastStorage = _executive->lastStorage();
    auto table =
        (lastStorage) ? lastStorage->openTable("/apps") : _executive->storage().openTable("/apps");
    // table must exist
    auto type = getDeployAuthType(_executive);
    if (type == 0)
    {
        return true;
    }
    auto getAclType = (type == (int)AuthType::WHITE_LIST_MODE) ? FS_ACL_WHITE : FS_ACL_BLACK;
    auto entry = table->getRow(getAclType);
    if (entry->getField(0).empty())
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("not deploy acl exist, return by default")
                               << LOG_KV("aclType", type);
        // if entry still empty
        //      if white list mode, return false
        //      if black list mode， return true
        return type == (int)AuthType::BLACK_LIST_MODE;
    }
    std::map<Address, bool> aclMap;
    auto&& out = asBytes(std::string(entry->getField(0)));
    codec::scale::decode(aclMap, gsl::make_span(out));
    if (aclMap.find(_account) == aclMap.end())
    {
        // can't find account in map
        // if white list mode, return false
        // if black list mode, return true
        return type == (int)AuthType::BLACK_LIST_MODE;
    }
    if (type == (int)AuthType::BLACK_LIST_MODE)
    {
        return !aclMap.at(_account);
    }
    return aclMap.at(_account);
}