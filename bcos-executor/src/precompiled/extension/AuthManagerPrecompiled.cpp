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
#include "ContractAuthMgrPrecompiled.h"
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
const char* const AUTH_METHOD_GET_AUTH = "getMethodAuth(string,bytes4)";
/// evm
const char* const AUTH_METHOD_GET_ADMIN_ADD = "getAdmin(address)";
const char* const AUTH_METHOD_SET_ADMIN_ADD = "resetAdmin(address,address)";
const char* const AUTH_METHOD_SET_AUTH_TYPE_ADD = "setMethodAuthType(address,bytes4,uint8)";
const char* const AUTH_METHOD_OPEN_AUTH_ADD = "openMethodAuth(address,bytes4,address)";
const char* const AUTH_METHOD_CLOSE_AUTH_ADD = "closeMethodAuth(address,bytes4,address)";
const char* const AUTH_METHOD_CHECK_AUTH_ADD = "checkMethodAuth(address,bytes4,address)";
const char* const AUTH_METHOD_GET_AUTH_ADD = "getMethodAuth(address,bytes4)";

const char* const AUTH_METHOD_SET_CONTRACT = "setContractStatus(address,bool)";
const char* const AUTH_METHOD_GET_CONTRACT = "contractAvailable(address)";

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
    name2Selector[AUTH_METHOD_SET_CONTRACT] = getFuncSelector(AUTH_METHOD_SET_CONTRACT, _hashImpl);
    name2Selector[AUTH_METHOD_GET_CONTRACT] = getFuncSelector(AUTH_METHOD_GET_CONTRACT, _hashImpl);
    name2Selector[AUTH_METHOD_GET_AUTH_ADD] = getFuncSelector(AUTH_METHOD_GET_AUTH_ADD, _hashImpl);

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
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    // parse function name
    uint32_t func = getParamFunc(_callParameters->input());

    /// directly passthrough data to call
    if (func == name2Selector[AUTH_METHOD_GET_ADMIN] ||
        func == name2Selector[AUTH_METHOD_GET_ADMIN_ADD])
    {
        getAdmin(_executive, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_ADMIN] ||
             func == name2Selector[AUTH_METHOD_SET_ADMIN_ADD])
    {
        resetAdmin(_executive, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_AUTH_TYPE] ||
             func == name2Selector[AUTH_METHOD_SET_AUTH_TYPE_ADD])
    {
        setMethodAuthType(_executive, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_OPEN_AUTH] ||
             func == name2Selector[AUTH_METHOD_OPEN_AUTH_ADD] ||
             func == name2Selector[AUTH_METHOD_CLOSE_AUTH] ||
             func == name2Selector[AUTH_METHOD_CLOSE_AUTH_ADD])
    {
        setMethodAuth(_executive, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_CHECK_AUTH] ||
             func == name2Selector[AUTH_METHOD_CHECK_AUTH_ADD])
    {
        checkMethodAuth(_executive, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_GET_AUTH] ||
             func == name2Selector[AUTH_METHOD_GET_AUTH_ADD])
    {
        getMethodAuth(_executive, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_GET_DEPLOY_TYPE])
    {
        getDeployType(_executive, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_DEPLOY_TYPE])
    {
        setDeployType(_executive, _callParameters);
    }
    else if (func == name2Selector[AUTH_OPEN_DEPLOY_ACCOUNT] ||
             func == name2Selector[AUTH_OPEN_DEPLOY_ACCOUNT_ADD])
    {
        setDeployAuth(_executive, false, _callParameters);
    }
    else if (func == name2Selector[AUTH_CLOSE_DEPLOY_ACCOUNT] ||
             func == name2Selector[AUTH_CLOSE_DEPLOY_ACCOUNT_ADD])
    {
        setDeployAuth(_executive, true, _callParameters);
    }
    else if (func == name2Selector[AUTH_CHECK_DEPLOY_ACCESS] ||
             func == name2Selector[AUTH_CHECK_DEPLOY_ACCESS_ADD])
    {
        hasDeployAuth(_executive, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_SET_CONTRACT])
    {
        setContractStatus(_executive, _callParameters);
    }
    else if (func == name2Selector[AUTH_METHOD_GET_CONTRACT])
    {
        contractAvailable(_executive, _callParameters);
    }
    else
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("AuthManagerPrecompiled")
                              << LOG_DESC("call undefined function") << LOG_KV("func", func);
        BOOST_THROW_EXCEPTION(
            bcos::protocol::PrecompiledError("AuthManagerPrecompiled call undefined function!"));
    }
    return _callParameters;
}

void AuthManagerPrecompiled::getAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)

{
    bytesConstRef data = getParamData(_callParameters->input());
    std::string path;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (blockContext->isWasm())
    {
        codec.decode(data, path);
    }
    else
    {
        Address contractAddress;
        codec.decode(data, contractAddress);
        path = contractAddress.hex();
    }

    std::string adminStr = getContractAdmin(_executive, path, _callParameters);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("getAdmin success")
                           << LOG_KV("contractPath", path) << LOG_KV("admin", adminStr);
    _callParameters->setExecResult(
        blockContext->isWasm() ? codec.encode(adminStr) : codec.encode(Address(adminStr)));
}

void AuthManagerPrecompiled::resetAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)

{
    std::string path;
    std::string admin;
    bytesConstRef data = getParamData(_callParameters->input());
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        Address adminAddress;
        codec.decode(data, contractAddress, adminAddress);
        path = contractAddress.hex();
        admin = adminAddress.hex();
    }
    else
    {
        codec.decode(data, path, admin);
    }
    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext->number())
                           << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("resetAdmin")
                           << LOG_KV("path", path) << LOG_KV("admin", admin);
    if (!checkSenderFromAuth(_callParameters->m_sender))
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("AuthManagerPrecompiled")
                              << LOG_DESC("sender is not from sys") << LOG_KV("path", path)
                              << LOG_KV("sender", _callParameters->m_sender);
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }
    auto newParams =
        codec.encode(std::string(AUTH_CONTRACT_MGR_ADDRESS), _callParameters->input().toBytes());
    std::string authMgrAddress = blockContext->isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    auto response =
        externalRequest(_executive, ref(newParams), _callParameters->m_origin, authMgrAddress, path,
            _callParameters->m_staticCall, _callParameters->m_create, _callParameters->m_gas, true);
    _callParameters->setExternalResult(std::move(response));
}

void AuthManagerPrecompiled::setMethodAuthType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    std::string path;
    string32 _func;
    string32 _type;
    bytesConstRef data = getParamData(_callParameters->input());
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    auto beginT = utcTime();
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        codec.decode(data, contractAddress, _func, _type);
        path = contractAddress.hex();
    }
    else
    {
        codec.decode(data, path, _func, _type);
    }
    auto admin = getContractAdmin(_executive, path, _callParameters);
    if (_callParameters->m_sender != admin)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("AuthManagerPrecompiled")
                              << LOG_DESC("Permission denied, only admin can set contract access.")
                              << LOG_KV("address", path)
                              << LOG_KV("sender", _callParameters->m_sender);
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }
    auto newParams =
        codec.encode(std::string(AUTH_CONTRACT_MGR_ADDRESS), _callParameters->input().toBytes());
    std::string authMgrAddress = blockContext->isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;
    auto response =
        externalRequest(_executive, ref(newParams), _callParameters->m_origin, authMgrAddress, path,
            _callParameters->m_staticCall, _callParameters->m_create, _callParameters->m_gas, true);
    auto finishedET = utcTime() - beginT;
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("AuthManagerPrecompiled") << "setMethodAuthType finished"
                           << LOG_KV("setPath", path) << LOG_KV("finishedET", finishedET);
    _callParameters->setExternalResult(std::move(response));
}

void AuthManagerPrecompiled::checkMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    std::string path;
    string32 _func;
    std::string account;
    bytesConstRef data = getParamData(_callParameters->input());
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        Address accountAddress;
        codec.decode(data, contractAddress, _func, accountAddress);
        path = contractAddress.hex();
        account = accountAddress.hex();
    }
    else
    {
        codec.decode(data, path, _func, account);
    }
    auto newParams =
        codec.encode(std::string(AUTH_CONTRACT_MGR_ADDRESS), _callParameters->input().toBytes());
    std::string authMgrAddress = blockContext->isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    auto response =
        externalRequest(_executive, ref(newParams), _callParameters->m_origin, authMgrAddress, path,
            _callParameters->m_staticCall, _callParameters->m_create, _callParameters->m_gas, true);

    _callParameters->setExternalResult(std::move(response));
}

void AuthManagerPrecompiled::getMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    std::string path;
    string32 _func;
    bytesConstRef data = getParamData(_callParameters->input());
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        codec.decode(data, contractAddress, _func);
        path = contractAddress.hex();
    }
    else
    {
        codec.decode(data, path, _func);
    }
    auto newParams =
        codec.encode(std::string(AUTH_CONTRACT_MGR_ADDRESS), _callParameters->input().toBytes());
    std::string authMgrAddress = blockContext->isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    auto response =
        externalRequest(_executive, ref(newParams), _callParameters->m_origin, authMgrAddress, path,
            _callParameters->m_staticCall, _callParameters->m_create, _callParameters->m_gas, true);

    _callParameters->setExternalResult(std::move(response));
}

void AuthManagerPrecompiled::setMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    std::string path;
    std::string account;
    string32 _func;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    bytesConstRef data = getParamData(_callParameters->input());
    auto recordT = utcTime();
    if (!blockContext->isWasm())
    {
        Address contractAddress;
        Address accountAddress;
        codec.decode(data, contractAddress, _func, accountAddress);
        path = contractAddress.hex();
        account = accountAddress.hex();
    }
    else
    {
        codec.decode(data, path, _func, account);
    }
    auto admin = getContractAdmin(_executive, path, _callParameters);
    if (_callParameters->m_sender != admin)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("AuthManagerPrecompiled")
                              << LOG_DESC("Permission denied, only admin can set contract access.")
                              << LOG_KV("address", path)
                              << LOG_KV("sender", _callParameters->m_sender);
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }
    auto newParams =
        codec.encode(std::string(AUTH_CONTRACT_MGR_ADDRESS), _callParameters->input().toBytes());
    std::string authMgrAddress = blockContext->isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;
    auto response =
        externalRequest(_executive, ref(newParams), _callParameters->m_origin, authMgrAddress, path,
            _callParameters->m_staticCall, _callParameters->m_create, _callParameters->m_gas, true);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("AuthManagerPrecompiled") << "setMethodAuth finished"
                           << LOG_KV("setPath", path) << LOG_KV("execT", (utcTime() - recordT));
    _callParameters->setExternalResult(std::move(response));
}

void AuthManagerPrecompiled::setContractStatus(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    std::string address;
    bool isFreeze = false;
    bytesConstRef data = getParamData(_callParameters->input());
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (blockContext->isWasm())
    {
        codec.decode(data, address, isFreeze);
    }
    else
    {
        Address contractAddress;
        codec.decode(data, contractAddress, isFreeze);
        address = contractAddress.hex();
    }
    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext->number())
                           << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("setContractStatus")
                           << LOG_KV("address", address) << LOG_KV("isFreeze", isFreeze);

    /// check sender is contract admin
    auto admin = getContractAdmin(_executive, address, _callParameters);
    if (_callParameters->m_sender != admin)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("AuthManagerPrecompiled")
                              << LOG_DESC("Permission denied, only admin can set contract access.")
                              << LOG_KV("address", address)
                              << LOG_KV("sender", _callParameters->m_sender);
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }
    auto newParams =
        codec.encode(std::string(AUTH_CONTRACT_MGR_ADDRESS), _callParameters->input().toBytes());
    std::string authMgrAddress = blockContext->isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    auto response = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        authMgrAddress, address, _callParameters->m_staticCall, _callParameters->m_create,
        _callParameters->m_gas, true);

    _callParameters->setExternalResult(std::move(response));
}

void AuthManagerPrecompiled::contractAvailable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    std::string address;
    bytesConstRef data = getParamData(_callParameters->input());
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (blockContext->isWasm())
    {
        codec.decode(data, address);
    }
    else
    {
        Address contractAddress;
        codec.decode(data, contractAddress);
        address = contractAddress.hex();
    }
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("contractAvailable")
                           << LOG_KV("address", address);

    auto newParams =
        codec.encode(std::string(AUTH_CONTRACT_MGR_ADDRESS), _callParameters->input().toBytes());
    std::string authMgrAddress = blockContext->isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    auto response = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        authMgrAddress, address, _callParameters->m_staticCall, _callParameters->m_create,
        _callParameters->m_gas, true);

    _callParameters->setExternalResult(std::move(response));
}

std::string AuthManagerPrecompiled::getContractAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const std::string& _to,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());

    std::string authMgrAddress = blockContext->isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    bytes selector = blockContext->isWasm() ?
                         codec.encodeWithSig(AUTH_METHOD_GET_ADMIN, _to) :
                         codec.encodeWithSig(AUTH_METHOD_GET_ADMIN_ADD, Address(_to));
    auto data = codec.encode(std::string(AUTH_CONTRACT_MGR_ADDRESS), selector);
    auto response = externalRequest(_executive, ref(data), _callParameters->m_origin,
        authMgrAddress, _to, _callParameters->m_staticCall, false, _callParameters->m_gas, true);

    if (response->status != (int32_t)protocol::TransactionStatus::None)
    {
        PRECOMPILED_LOG(DEBUG) << "Can't get contract admin, check the contract existence."
                               << LOG_KV("address", _to);
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError("Please check the existence of contract."));
    }
    std::string admin = "";

    codec.decode(ref(response->data), admin);

    return admin;
}

u256 AuthManagerPrecompiled::getDeployAuthType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive)
{
    auto table = _executive->storage().openTable("/apps");
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
    const PrecompiledExecResult::Ptr& _callParameters)


{
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());

    u256 type = getDeployAuthType(_executive);
    _callParameters->setExecResult(codec.encode(type));
}

void AuthManagerPrecompiled::setDeployType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    string32 _type;
    bytesConstRef data = getParamData(_callParameters->input());
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(data, _type);
    if (!checkSenderFromAuth(_callParameters->m_sender))
    {
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }
    u256 type = _type[_type.size() - 1];
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("setDeployType")
                           << LOG_KV("type", type);
    if (type > 2)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("AuthManagerPrecompiled")
                               << LOG_DESC("deploy auth type must be 1 or 2.");
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_TABLE_ERROR_AUTH_TYPE, codec);
        return;
    }
    auto table = _executive->storage().openTable("/apps");
    Entry entry;
    entry.importFields({boost::lexical_cast<std::string>(type)});
    table->setRow(FS_ACL_TYPE, std::move(entry));

    getErrorCodeOut(_callParameters->mutableExecResult(), CODE_SUCCESS, codec);
}

void AuthManagerPrecompiled::setDeployAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bool _isClose,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    std::string account;
    bytesConstRef data = getParamData(_callParameters->input());
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (blockContext->isWasm())
    {
        codec.decode(data, account);
    }
    else
    {
        Address accountAddress;
        codec.decode(data, accountAddress);
        account = accountAddress.hex();
    }
    if (!checkSenderFromAuth(_callParameters->m_sender))
    {
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
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

    getErrorCodeOut(_callParameters->mutableExecResult(), CODE_SUCCESS, codec);
}

void AuthManagerPrecompiled::hasDeployAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    std::string account;
    bytesConstRef data = getParamData(_callParameters->input());
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    if (blockContext->isWasm())
    {
        codec.decode(data, account);
    }
    else
    {
        Address accountAddress;
        codec.decode(data, accountAddress);
        account = accountAddress.hex();
    }
    _callParameters->setExecResult(codec.encode(checkDeployAuth(_executive, account)));
}

bool AuthManagerPrecompiled::checkDeployAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const std::string& _account)
{
    auto table = _executive->storage().openTable("/apps");
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