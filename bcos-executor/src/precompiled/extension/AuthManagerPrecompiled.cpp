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
 * @file AuthManagerPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-10-09
 */

#include "AuthManagerPrecompiled.h"
#include "../../vm/HostContext.h"
#include <boost/core/ignore_unused.hpp>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;

/// wasm
const char* const AUTH_METHOD_GET_ADMIN = "getAdmin(string)";
const char* const AUTH_METHOD_SET_ADMIN = "resetAdmin(string,string)";
const char* const AUTH_METHOD_SET_AUTH_TYPE = "setMethodAuthType(string,bytes4,uint8)";
const char* const AUTH_METHOD_OPEN_AUTH = "openMethodAuth(string,bytes4,string)";
const char* const AUTH_METHOD_CLOSE_AUTH = "closeMethodAuth(string,bytes4,string)";
const char* const AUTH_METHOD_CHECK_AUTH = "checkMethodAuth(string,bytes4,string)";
/// evm
const char* const AUTH_METHOD_GET_ADMIN_ADD = "getAdmin(address)";
const char* const AUTH_METHOD_SET_ADMIN_ADD = "resetAdmin(address,address)";
const char* const AUTH_METHOD_SET_AUTH_TYPE_ADD = "setMethodAuthType(address,bytes4,uint8)";
const char* const AUTH_METHOD_OPEN_AUTH_ADD = "openMethodAuth(address,bytes4,address)";
const char* const AUTH_METHOD_CLOSE_AUTH_ADD = "closeMethodAuth(address,bytes4,address)";
const char* const AUTH_METHOD_CHECK_AUTH_ADD = "checkMethodAuth(address,bytes4,address)";
const char* const AUTH_METHOD_SET_CONTRACT = "setContractStatus(address,bool)";

/// deploy
const char* const AUTH_METHOD_GET_DEPLOY_TYPE = "deployType()";
const char* const AUTH_METHOD_SET_DEPLOY_TYPE = "setDeployAuthType(uint8)";
/// wasm
const char* const AUTH_OPEN_DEPLOY_ACCOUNT = "openDeployAuth(string)";
const char* const AUTH_CLOSE_DEPLOY_ACCOUNT = "closeDeployAuth(string)";
const char* const AUTH_CHECK_DEPLOY_ACCESS = "hasDeployAuth(string)";
/// evm
const char* const AUTH_OPEN_DEPLOY_ACCOUNT_ADD = "openDeployAuth(address)";
const char* const AUTH_CLOSE_DEPLOY_ACCOUNT_ADD = "closeDeployAuth(address)";
const char* const AUTH_CHECK_DEPLOY_ACCESS_ADD = "hasDeployAuth(address)";

AuthManagerPrecompiled::AuthManagerPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
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
    name2Selector[AUTH_METHOD_SET_CONTRACT] = getFuncSelector(AUTH_METHOD_SET_CONTRACT, _hashImpl);

    /// deploy
    name2Selector[AUTH_METHOD_GET_DEPLOY_TYPE] =
        getFuncSelector(AUTH_METHOD_GET_DEPLOY_TYPE, _hashImpl);
    name2Selector[AUTH_METHOD_SET_DEPLOY_TYPE] =
        getFuncSelector(AUTH_METHOD_SET_DEPLOY_TYPE, _hashImpl);

    /// wasm deploy auth
    name2Selector[AUTH_OPEN_DEPLOY_ACCOUNT] = getFuncSelector(AUTH_OPEN_DEPLOY_ACCOUNT, _hashImpl);
    name2Selector[AUTH_CLOSE_DEPLOY_ACCOUNT] =
        getFuncSelector(AUTH_CLOSE_DEPLOY_ACCOUNT, _hashImpl);
    name2Selector[AUTH_CHECK_DEPLOY_ACCESS] = getFuncSelector(AUTH_CHECK_DEPLOY_ACCESS, _hashImpl);
    /// evm deploy auth
    name2Selector[AUTH_OPEN_DEPLOY_ACCOUNT_ADD] =
        getFuncSelector(AUTH_OPEN_DEPLOY_ACCOUNT_ADD, _hashImpl);
    name2Selector[AUTH_CLOSE_DEPLOY_ACCOUNT_ADD] =
        getFuncSelector(AUTH_CLOSE_DEPLOY_ACCOUNT_ADD, _hashImpl);
    name2Selector[AUTH_CHECK_DEPLOY_ACCESS_ADD] =
        getFuncSelector(AUTH_CHECK_DEPLOY_ACCESS_ADD, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> AuthManagerPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef data,
    const std::string& _origin, const std::string& _sender, int64_t _gasLeft)
{
    // parse function name
    uint32_t func = getParamFunc(data);
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    gasPricer->setMemUsed(data.size());

    /// directly passthrough data to call
    if (func == name2Selector[AUTH_METHOD_GET_ADMIN] ||
        func == name2Selector[AUTH_METHOD_GET_ADMIN_ADD])
    {
        getAdmin(_executive, data, callResult, gasPricer, _origin, _gasLeft);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_ADMIN] ||
             func == name2Selector[AUTH_METHOD_SET_ADMIN_ADD])
    {
        resetAdmin(_executive, data, callResult, _origin, _sender, gasPricer, _gasLeft);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_AUTH_TYPE] ||
             func == name2Selector[AUTH_METHOD_SET_AUTH_TYPE_ADD])
    {
        setMethodAuthType(_executive, data, callResult, _origin, _sender, gasPricer, _gasLeft);
    }
    else if (func == name2Selector[AUTH_METHOD_OPEN_AUTH] ||
             func == name2Selector[AUTH_METHOD_OPEN_AUTH_ADD] ||
             func == name2Selector[AUTH_METHOD_CLOSE_AUTH] ||
             func == name2Selector[AUTH_METHOD_CLOSE_AUTH_ADD])
    {
        setMethodAuth(_executive, data, callResult, _origin, _sender, gasPricer, _gasLeft);
    }
    else if (func == name2Selector[AUTH_METHOD_CHECK_AUTH] ||
             func == name2Selector[AUTH_METHOD_CHECK_AUTH_ADD])
    {
        checkMethodAuth(_executive, data, callResult, gasPricer, _origin, _gasLeft);
    }
    else if (func == name2Selector[AUTH_METHOD_GET_DEPLOY_TYPE])
    {
        getDeployType(_executive, callResult, gasPricer);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_DEPLOY_TYPE])
    {
        setDeployType(_executive, data, callResult, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_OPEN_DEPLOY_ACCOUNT] ||
             func == name2Selector[AUTH_OPEN_DEPLOY_ACCOUNT_ADD])
    {
        setDeployAuth(_executive, data, callResult, false, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_CLOSE_DEPLOY_ACCOUNT] ||
             func == name2Selector[AUTH_CLOSE_DEPLOY_ACCOUNT_ADD])
    {
        setDeployAuth(_executive, data, callResult, true, _sender, gasPricer);
    }
    else if (func == name2Selector[AUTH_CHECK_DEPLOY_ACCESS] ||
             func == name2Selector[AUTH_CHECK_DEPLOY_ACCESS_ADD])
    {
        hasDeployAuth(_executive, data, callResult);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_CONTRACT])
    {
        setContractStatus(_executive, data, callResult, _origin, _sender, gasPricer, _gasLeft);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("AuthManagerPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

void AuthManagerPrecompiled::getAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _params,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer,
    const std::string& _origin, int64_t _gasLeft)
{
    bytesConstRef data = getParamData(_params);
    std::string path;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
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
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("getAdmin")
                           << LOG_KV("path", path);

    std::string adminStr =
        getContractAdmin(_executive, _origin, path, _gasLeft - gasPricer->calTotalGas());
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("getAdmin success")
                           << LOG_KV("admin", adminStr);
    gasPricer->updateMemUsed(1);
    callResult->setExecResult(
        blockContext->isWasm() ? codec->encode(adminStr) : codec->encode(Address(adminStr)));
}

void AuthManagerPrecompiled::resetAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _params,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _origin,
    const std::string& _sender, const PrecompiledGas::Ptr& gasPricer, int64_t _gasLeft)
{
    std::string path;
    std::string admin;
    bytesConstRef data = getParamData(_params);
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        Address adminAddress;
        codec->decode(data, contractAddress, adminAddress);
        path = contractAddress.hex();
        admin = adminAddress.hex();
    }
    else
    {
        codec->decode(data, path, admin);
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("resetAdmin")
                           << LOG_KV("path", path) << LOG_KV("admin", admin);
    if (!checkSenderFromAuth(_sender))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("AuthManagerPrecompiled")
                               << LOG_DESC("sender is not from sys") << LOG_KV("path", path)
                               << LOG_KV("sender", _sender);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, *codec);
        return;
    }
    auto newParams = codec->encode(std::string(AUTH_CONTRACT_MGR_ADDRESS), _params.toBytes());
    std::string authMgrAddress = blockContext->isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    auto response = externalRequest(_executive, ref(newParams), _origin, authMgrAddress, path,
        false, false, _gasLeft - gasPricer->calTotalGas(), true);
    callResult->setExternalResult(std::move(response));
    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Set);
}

void AuthManagerPrecompiled::setMethodAuthType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _params,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _origin,
    const std::string& _sender, const PrecompiledGas::Ptr& gasPricer, int64_t _gasLeft)
{
    std::string path;
    string32 _func;
    string32 _type;
    bytesConstRef data = getParamData(_params);
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
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
    auto admin = getContractAdmin(_executive, _origin, path, _gasLeft);
    if (_sender != admin)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("AuthManagerPrecompiled")
                               << LOG_DESC("Permission denied, only admin can set contract access.")
                               << LOG_KV("address", path) << LOG_KV("sender", _sender);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, *codec);
        return;
    }
    auto newParams = codec->encode(std::string(AUTH_CONTRACT_MGR_ADDRESS), _params.toBytes());
    std::string authMgrAddress = blockContext->isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    auto response = externalRequest(_executive, ref(newParams), _origin, authMgrAddress, path,
        false, false, _gasLeft - gasPricer->calTotalGas(), true);
    callResult->setExternalResult(std::move(response));
    gasPricer->updateMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Set);
}

void AuthManagerPrecompiled::checkMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _params,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer,
    const std::string& _origin, int64_t _gasLeft)
{
    std::string path;
    string32 _func;
    std::string account;
    bytesConstRef data = getParamData(_params);
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        Address accountAddress;
        codec->decode(data, contractAddress, _func, accountAddress);
        path = contractAddress.hex();
        account = accountAddress.hex();
    }
    else
    {
        codec->decode(data, path, _func, account);
    }
    auto newParams = codec->encode(std::string(AUTH_CONTRACT_MGR_ADDRESS), _params.toBytes());
    std::string authMgrAddress = blockContext->isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    auto response = externalRequest(_executive, ref(newParams), _origin, authMgrAddress, path, true,
        false, _gasLeft - gasPricer->calTotalGas(), true);

    gasPricer->updateMemUsed(response->data.size());
    callResult->setExternalResult(std::move(response));
}

void AuthManagerPrecompiled::setMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _params,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _origin,
    const std::string& _sender, const PrecompiledGas::Ptr& gasPricer, int64_t _gasLeft)
{
    std::string path;
    std::string account;
    string32 _func;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    bytesConstRef data = getParamData(_params);
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        Address accountAddress;
        codec->decode(data, contractAddress, _func, accountAddress);
        path = contractAddress.hex();
        account = accountAddress.hex();
    }
    else
    {
        codec->decode(data, path, _func, account);
    }
    auto admin = getContractAdmin(_executive, _origin, path, _gasLeft);
    if (_sender != admin)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractAuthPrecompiled")
                               << LOG_DESC("Permission denied, only admin can set contract access.")
                               << LOG_KV("address", path) << LOG_KV("sender", _sender);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, *codec);
        return;
    }
    auto newParams = codec->encode(std::string(AUTH_CONTRACT_MGR_ADDRESS), _params.toBytes());
    std::string authMgrAddress = blockContext->isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;
    auto response = externalRequest(_executive, ref(newParams), _origin, authMgrAddress, path,
        false, false, _gasLeft - gasPricer->calTotalGas(), true);
    gasPricer->updateMemUsed(response->data.size());
    gasPricer->appendOperation(InterfaceOpcode::Set);

    callResult->setExternalResult(std::move(response));
}

void AuthManagerPrecompiled::setContractStatus(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _params,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _origin,
    const std::string& _sender, const PrecompiledGas::Ptr& gasPricer, int64_t _gasLeft)
{
    std::string address;
    bool isFreeze = false;
    bytesConstRef data = getParamData(_params);
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    if (blockContext->isWasm())
    {
        codec->decode(data, address, isFreeze);
    }
    else
    {
        Address contractAddress;
        codec->decode(data, contractAddress, isFreeze);
        address = contractAddress.hex();
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("setContractStatus")
                           << LOG_KV("address", address) << LOG_KV("isFreeze", isFreeze);

    /// check sender is contract admin
    auto admin = getContractAdmin(_executive, _origin, address, _gasLeft);
    if (_sender != admin)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("AuthManagerPrecompiled")
                               << LOG_DESC("Permission denied, only admin can set contract access.")
                               << LOG_KV("address", address) << LOG_KV("sender", _sender);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, *codec);
        return;
    }
    auto newParams = codec->encode(std::string(AUTH_CONTRACT_MGR_ADDRESS), _params.toBytes());
    std::string authMgrAddress = blockContext->isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    auto response = externalRequest(_executive, ref(newParams), _origin, authMgrAddress, address,
        false, false, _gasLeft - gasPricer->calTotalGas(), true);
    gasPricer->updateMemUsed(response->data.size());
    gasPricer->appendOperation(InterfaceOpcode::Set);

    callResult->setExternalResult(std::move(response));
}

std::string AuthManagerPrecompiled::getContractAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const std::string& _origin,
    const std::string& _to, int64_t _gasLeft)
{
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());

    std::string authMgrAddress = blockContext->isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    bytes selector = blockContext->isWasm() ?
                         codec->encodeWithSig(AUTH_METHOD_GET_ADMIN, _to) :
                         codec->encodeWithSig(AUTH_METHOD_GET_ADMIN_ADD, Address(_to));
    auto data = codec->encode(std::string(AUTH_CONTRACT_MGR_ADDRESS), selector);
    auto response = externalRequest(
        _executive, ref(data), _origin, authMgrAddress, _to, true, false, _gasLeft, true);

    std::string admin = "";

    codec->decode(ref(response->data), admin);

    return admin;
}

u256 AuthManagerPrecompiled::getDeployAuthType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive)
{
    auto lastStorage = _executive->lastStorage();

    PRECOMPILED_LOG(TRACE) << LOG_BADGE("getDeployAuthType")
                           << ((lastStorage) ? "use lastStorage" : "use latestStorage");
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

void AuthManagerPrecompiled::getDeployType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)

{
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());

    u256 type = getDeployAuthType(_executive);
    gasPricer->updateMemUsed(1);
    callResult->setExecResult(codec->encode(type));
}

void AuthManagerPrecompiled::setDeployType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _params,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const std::string& _sender,
    const PrecompiledGas::Ptr& gasPricer)
{
    string32 _type;
    bytesConstRef data = getParamData(_params);
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, _type);
    if (!checkSenderFromAuth(_sender))
    {
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, *codec);
        return;
    }
    u256 type = _type[_type.size() - 1];
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("setDeployType")
                           << LOG_KV("type", type);
    if (type > 2)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("AuthManagerPrecompiled")
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

void AuthManagerPrecompiled::setDeployAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _params,
    const std::shared_ptr<PrecompiledExecResult>& callResult, bool _isClose,
    const std::string& _sender, const PrecompiledGas::Ptr& gasPricer)
{
    std::string account;
    bytesConstRef data = getParamData(_params);
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    if (blockContext->isWasm())
    {
        codec->decode(data, account);
    }
    else
    {
        Address accountAddress;
        codec->decode(data, accountAddress);
        account = accountAddress.hex();
    }
    if (!checkSenderFromAuth(_sender))
    {
        getErrorCodeOut(callResult->mutableExecResult(), CODE_NO_AUTHORIZED, *codec);
        return;
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("setDeployAuth")
                           << LOG_KV("account", account) << LOG_KV("isClose", _isClose);
    auto table = _executive->storage().openTable("/apps");
    auto type = getDeployAuthType(_executive);
    auto getAclStr = (type == (int)AuthType::BLACK_LIST_MODE) ? FS_ACL_BLACK : FS_ACL_WHITE;

    std::map<std::string, bool> aclMap;
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

void AuthManagerPrecompiled::hasDeployAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& _params,
    const std::shared_ptr<PrecompiledExecResult>& callResult)
{
    std::string account;
    bytesConstRef data = getParamData(_params);
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    if (blockContext->isWasm())
    {
        codec->decode(data, account);
    }
    else
    {
        Address accountAddress;
        codec->decode(data, accountAddress);
        account = accountAddress.hex();
    }
    callResult->setExecResult(codec->encode(checkDeployAuth(_executive, account)));
}

bool AuthManagerPrecompiled::checkDeployAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const std::string& _account)
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
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("AuthManagerPrecompiled")
                               << LOG_DESC("not deploy acl exist, return by default")
                               << LOG_KV("aclType", type);
        // if entry still empty
        //      if white list mode, return false
        //      if black list mode， return true
        return type == (int)AuthType::BLACK_LIST_MODE;
    }
    std::map<std::string, bool> aclMap;
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