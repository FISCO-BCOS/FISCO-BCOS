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

#include <utility>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::protocol;

/// contract ACL
/// wasm
constexpr const char* const CONTRACT_AUTH_METHOD_GET_ADMIN = "getAdmin(string)";
constexpr const char* const CONTRACT_AUTH_METHOD_SET_ADMIN = "resetAdmin(string,string)";
constexpr const char* const CONTRACT_AUTH_METHOD_SET_AUTH_TYPE = "setMethodAuthType(string,bytes4,uint8)";
constexpr const char* const CONTRACT_AUTH_METHOD_OPEN_AUTH = "openMethodAuth(string,bytes4,string)";
constexpr const char* const CONTRACT_AUTH_METHOD_CLOSE_AUTH = "closeMethodAuth(string,bytes4,string)";
constexpr const char* const CONTRACT_AUTH_METHOD_CHECK_AUTH = "checkMethodAuth(string,bytes4,string)";
constexpr const char* const CONTRACT_AUTH_METHOD_GET_AUTH = "getMethodAuth(string,bytes4)";
/// evm
constexpr const char* const CONTRACT_AUTH_METHOD_GET_ADMIN_ADD = "getAdmin(address)";
constexpr const char* const CONTRACT_AUTH_METHOD_SET_ADMIN_ADD = "resetAdmin(address,address)";
constexpr const char* const CONTRACT_AUTH_METHOD_SET_AUTH_TYPE_ADD =
    "setMethodAuthType(address,bytes4,uint8)";
constexpr const char* const CONTRACT_AUTH_METHOD_OPEN_AUTH_ADD = "openMethodAuth(address,bytes4,address)";
constexpr const char* const CONTRACT_AUTH_METHOD_CLOSE_AUTH_ADD = "closeMethodAuth(address,bytes4,address)";
constexpr const char* const CONTRACT_AUTH_METHOD_CHECK_AUTH_ADD = "checkMethodAuth(address,bytes4,address)";
constexpr const char* const CONTRACT_AUTH_METHOD_GET_AUTH_ADD = "getMethodAuth(address,bytes4)";

/// contract status
constexpr const char* const CONTRACT_AUTH_METHOD_SET_CONTRACT = "setContractStatus(address,bool)";
constexpr const char* const CONTRACT_AUTH_METHOD_SET_CONTRACT_32 = "setContractStatus(address,uint8)";
constexpr const char* const CONTRACT_AUTH_METHOD_GET_CONTRACT = "contractAvailable(address)";

ContractAuthMgrPrecompiled::ContractAuthMgrPrecompiled(crypto::Hash::Ptr _hashImpl, bool _isWasm)
  : bcos::precompiled::Precompiled(std::move(_hashImpl))
{
    const auto* getAdminStr = _isWasm ? CONTRACT_AUTH_METHOD_GET_ADMIN : CONTRACT_AUTH_METHOD_GET_ADMIN_ADD;
    registerFunc(
        getFuncSelector(getAdminStr, _hashImpl), [this](auto&& _executive, auto&& _callParameters) {
            getAdmin(std::forward<decltype(_executive)>(_executive),
                std::forward<decltype(_callParameters)>(_callParameters));
        });

    const auto* resetAdminStr = _isWasm ? CONTRACT_AUTH_METHOD_SET_ADMIN : CONTRACT_AUTH_METHOD_SET_ADMIN_ADD;
    registerFunc(getFuncSelector(resetAdminStr, _hashImpl),
        [this](auto&& _executive, auto&& _callParameters) {
            resetAdmin(std::forward<decltype(_executive)>(_executive),
                std::forward<decltype(_callParameters)>(_callParameters));
        });

    const auto* setMethodAuthTypeStr =
        _isWasm ? CONTRACT_AUTH_METHOD_SET_AUTH_TYPE : CONTRACT_AUTH_METHOD_SET_AUTH_TYPE_ADD;
    registerFunc(getFuncSelector(setMethodAuthTypeStr, _hashImpl),
        [this](auto&& _executive, auto&& _callParameters) {
            setMethodAuthType(std::forward<decltype(_executive)>(_executive),
                std::forward<decltype(_callParameters)>(_callParameters));
        });

    const auto* openMethodAuthStr = _isWasm ? CONTRACT_AUTH_METHOD_OPEN_AUTH : CONTRACT_AUTH_METHOD_OPEN_AUTH_ADD;
    registerFunc(getFuncSelector(openMethodAuthStr, _hashImpl),
        [this](auto&& _executive, auto&& _callParameters) {
            openMethodAuth(std::forward<decltype(_executive)>(_executive),
                std::forward<decltype(_callParameters)>(_callParameters));
        });

    const auto* closeMethodAuthStr = _isWasm ? CONTRACT_AUTH_METHOD_CLOSE_AUTH : CONTRACT_AUTH_METHOD_CLOSE_AUTH_ADD;
    registerFunc(getFuncSelector(closeMethodAuthStr, _hashImpl),
        [this](auto&& _executive, auto&& _callParameters) {
            closeMethodAuth(std::forward<decltype(_executive)>(_executive),
                std::forward<decltype(_callParameters)>(_callParameters));
        });

    const auto* checkMethodAuthStr = _isWasm ? CONTRACT_AUTH_METHOD_CHECK_AUTH : CONTRACT_AUTH_METHOD_CHECK_AUTH_ADD;
    registerFunc(getFuncSelector(checkMethodAuthStr, _hashImpl),
        [this](auto&& _executive, auto&& _callParameters) {
            checkMethodAuth(std::forward<decltype(_executive)>(_executive),
                std::forward<decltype(_callParameters)>(_callParameters));
        });

    const auto* getMethodAuthStr = _isWasm ? CONTRACT_AUTH_METHOD_GET_AUTH : CONTRACT_AUTH_METHOD_GET_AUTH_ADD;
    registerFunc(getFuncSelector(getMethodAuthStr, _hashImpl),
        [this](auto&& _executive, auto&& _callParameters) {
            getMethodAuth(std::forward<decltype(_executive)>(_executive),
                std::forward<decltype(_callParameters)>(_callParameters));
        });

    registerFunc(
        getFuncSelector(CONTRACT_AUTH_METHOD_SET_CONTRACT_32, _hashImpl),
        [this](auto&& _executive, auto&& _callParameters) {
            setContractStatus32(std::forward<decltype(_executive)>(_executive),
                std::forward<decltype(_callParameters)>(_callParameters));
        },
        protocol::BlockVersion::V3_2_VERSION);

    registerFunc(getFuncSelector(CONTRACT_AUTH_METHOD_SET_CONTRACT, _hashImpl),
        [this](auto&& _executive, auto&& _callParameters) {
            setContractStatus(std::forward<decltype(_executive)>(_executive),
                std::forward<decltype(_callParameters)>(_callParameters));
        });
    registerFunc(getFuncSelector(CONTRACT_AUTH_METHOD_GET_CONTRACT, _hashImpl),
        [this](auto&& _executive, auto&& _callParameters) {
            contractAvailable(std::forward<decltype(_executive)>(_executive),
                std::forward<decltype(_callParameters)>(_callParameters));
        });
}

std::shared_ptr<PrecompiledExecResult> ContractAuthMgrPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    // parse function name
    uint32_t func = getParamFunc(_callParameters->input());

    const auto& blockContext = _executive->blockContext();
    const auto* authAddress = blockContext.isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    PRECOMPILED_LOG(TRACE) << BLOCK_NUMBER(blockContext.number())
                           << LOG_BADGE("ContractAuthMgrPrecompiled") << LOG_DESC("call")
                           << LOG_KV("func", func);

    if (_callParameters->m_sender != authAddress)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("ContractAuthMgrPrecompiled")
                              << LOG_DESC("sender is not from AuthManager")
                              << LOG_KV("sender", _callParameters->m_sender);
        BOOST_THROW_EXCEPTION(BCOS_ERROR(
            CODE_NO_AUTHORIZED, "ContractAuthMgrPrecompiled: sender is not from AuthManager"));
    }
    auto selector = selector2Func.find(func);
    if (selector != selector2Func.end())
    {
        auto& [minVersion, execFunc] = selector->second;
        if (versionCompareTo(blockContext.blockVersion(), minVersion) >= 0)
        {
            execFunc(_executive, _callParameters);

            if (c_fileLogLevel == LogLevel::TRACE)
            {
                PRECOMPILED_LOG(TRACE)
                    << LOG_BADGE("ContractAuthMgrPrecompiled") << LOG_DESC("call function")
                    << LOG_KV("func", func) << LOG_KV("minVersion", minVersion);
            }
            return _callParameters;
        }
    }
    PRECOMPILED_LOG(INFO) << LOG_BADGE("ContractAuthMgrPrecompiled")
                          << LOG_DESC("call undefined function") << LOG_KV("func", func);
    BOOST_THROW_EXCEPTION(
        bcos::protocol::PrecompiledError("ContractAuthMgrPrecompiled call undefined function!"));
}

void ContractAuthMgrPrecompiled::getAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    /// evm:  getAdmin(address) => string admin
    /// wasm: getAdmin(string)  => string admin

    std::string path;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (blockContext.isWasm())
    {
        codec.decode(_callParameters->params(), path);
    }
    else
    {
        Address contractAddress;
        codec.decode(_callParameters->params(), contractAddress);
        path = contractAddress.hex();
    }
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthMgrPrecompiled") << LOG_DESC("getAdmin")
                           << LOG_KV("path", path);
    path = getAuthTableName(path);

    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("contract ACL table not found") << LOG_KV("table", path);
        if (versionCompareTo(blockContext.blockVersion(), BlockVersion::V3_3_VERSION) >= 0)
        {
            // return empty admin by default
            _callParameters->setExecResult(codec.encode(std::string(EMPTY_ADDRESS)));
            return;
        }
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Contract address not found."));
    }
    auto entry = table->getRow(ADMIN_FIELD);
    if (!entry)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("contract ACL admin entry not found")
                               << LOG_KV("path", path);
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Contract Admin row not found."));
    }
    std::string adminStr = std::string(entry->getField(0));
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthMgrPrecompiled")
                           << LOG_DESC("getAdmin success") << LOG_KV("admin", adminStr);
    _callParameters->setExecResult(codec.encode(adminStr));
}

void ContractAuthMgrPrecompiled::resetAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    /// evm:  resetAdmin(address path,address admin) => int256
    /// wasm: resetAdmin(string  path,string  admin) => int256

    std::string address;
    std::string admin;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (!blockContext.isWasm())
    {
        Address contractAddress;
        Address adminAddress;
        codec.decode(_callParameters->params(), contractAddress, adminAddress);
        address = contractAddress.hex();
        admin = adminAddress.hex();
    }
    else
    {
        codec.decode(_callParameters->params(), address, admin);
    }
    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number())
                           << LOG_BADGE("ContractAuthMgrPrecompiled") << LOG_DESC("resetAdmin")
                           << LOG_KV("address", address) << LOG_KV("admin", admin);
    auto path = getAuthTableName(address);
    auto table = _executive->storage().openTable(path);
    if (!table || !table->getRow(ADMIN_FIELD))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("contract ACL table not found") << LOG_KV("path", path);
        if (versionCompareTo(blockContext.blockVersion(), BlockVersion::V3_3_VERSION) >= 0)
        {
            if (!_executive->storage().openTable(getContractTableName(USER_APPS_PREFIX, address)))
                [[unlikely]]
            {
                // not exist contract address
                BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Contract address not found."));
            }
            // exist contract but not init auth table
            table = _executive->storage().createTable(path, "value");
            Entry statusEntry;
            statusEntry.importFields({std::string{CONTRACT_NORMAL}});
            table->setRow(STATUS_FIELD, std::move(statusEntry));
        }
        else
        {
            BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Contract address not found."));
        }
    }
    auto newEntry = table->newEntry();
    newEntry.setField(SYS_VALUE, admin);
    table->setRow(ADMIN_FIELD, std::move(newEntry));
    getErrorCodeOut(_callParameters->mutableExecResult(), CODE_SUCCESS, codec);
}

void ContractAuthMgrPrecompiled::setMethodAuthType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    /// evm:  setMethodAuthType(address path, bytes4 func, uint8 type) => int256
    /// wasm: setMethodAuthType(string  path, bytes4 func, uint8 type) => int256

    std::string path;
    string32 _func;
    string32 _type;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (!blockContext.isWasm())
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
    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                          << LOG_BADGE("ContractAuthMgrPrecompiled")
                          << LOG_DESC("setMethodAuthType") << LOG_KV("path", path)
                          << LOG_KV("func", toHexStringWithPrefix(func))
                          << LOG_KV("type", (uint32_t)type);
    path = getAuthTableName(path);
    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("contract ACL table not found") << LOG_KV("path", path);
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
        return;
    }
    auto entry = table->getRow(METHOD_AUTH_TYPE);
    if (!entry)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("contract ACL table method_auth_type row not found")
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

    getErrorCodeOut(_callParameters->mutableExecResult(), CODE_SUCCESS, codec);
}

void ContractAuthMgrPrecompiled::checkMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    /// evm:  checkMethodAuth(address path, bytes4 func, address account) => bool
    /// wasm: checkMethodAuth(string  path, bytes4 func, string  account) => bool

    std::string path;
    string32 _func;
    std::string account;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (!blockContext.isWasm())
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
}

bool ContractAuthMgrPrecompiled::checkMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const std::string_view& _path, bytesRef func, const std::string& account)
{
    auto path = getAuthTableName(_path);
    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        // only precompiled contract in /sys/, or pre-built-in contract
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthMgrPrecompiled")
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
            PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthMgrPrecompiled")
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
    const PrecompiledExecResult::Ptr& _callParameters)

{
    /// evm:  getMethodAuth(address,bytes4) => (uint8,string[],string[])
    /// wasm: getMethodAuth(string,bytes4) => (uint8,string[],string[])

    std::string path;
    string32 _func;
    bytes funcBytes;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (!blockContext.isWasm())
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
    std::vector<std::string> accessList = {};
    std::vector<std::string> blockList = {};
    path = getAuthTableName(path);
    auto getMethodType = getMethodAuthType(_executive, path, ref(funcBytes));
    if (getMethodType < 0)
    {
        // no acl strategy
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("no acl strategy") << LOG_KV("path", path);
        _callParameters->setExecResult(
            codec.encode(uint8_t(0), std::move(accessList), std::move(blockList)));
        return;
    }
    auto&& authMap = getMethodAuth(_executive, path, getMethodType);

    auto it = authMap.find(funcBytes);
    if (it == authMap.end())
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("auth row not found, no method set acl")
                               << LOG_KV("path", path);
        // func not set acl, return empty
        _callParameters->setExecResult(
            codec.encode(uint8_t(getMethodType), std::move(accessList), std::move(blockList)));
    }
    else
    {
        for (auto& addressPair : it->second)
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
}

void ContractAuthMgrPrecompiled::setMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bool _isClose,
    const PrecompiledExecResult::Ptr& _callParameters) const

{
    std::string path;
    std::string account;
    string32 _func;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), path, _func, account);
    if (!blockContext.isWasm())
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
    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                          << LOG_BADGE("ContractAuthMgrPrecompiled") << LOG_DESC("setAuth")
                          << LOG_KV("path", path) << LOG_KV("func", toHexStringWithPrefix(func))
                          << LOG_KV("account", account) << LOG_KV("isClose", _isClose);
    path = getAuthTableName(path);
    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("setMethodAuth: contract ACL table not found")
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
    else [[unlikely]]
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("invalid auth type") << LOG_KV("path", path)
                               << LOG_KV("type", authType);
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_TABLE_ERROR_AUTH_TYPE, codec);
        return;
    }
    auto entry = table->getRow(getTypeStr);
    if (!entry)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthMgrPrecompiled")
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
            PRECOMPILED_LOG(INFO) << LOG_BADGE("ContractAuthMgrPrecompiled")
                                  << LOG_DESC("auth map parse failed") << LOG_KV("path", path);
            getErrorCodeOut(
                _callParameters->mutableExecResult(), CODE_TABLE_AUTH_ROW_NOT_EXIST, codec);
            return;
        }
    }
    entry->setField(SYS_VALUE, asString(codec::scale::encode(authMap)));
    table->setRow(getTypeStr, std::move(entry.value()));
    getErrorCodeOut(_callParameters->mutableExecResult(), CODE_SUCCESS, codec);
}

int32_t ContractAuthMgrPrecompiled::getMethodAuthType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const std::string& _path,
    bytesConstRef _func) const
{
    auto table = _executive->storage().openTable(_path);
    if (!table || !table->getRow(METHOD_AUTH_TYPE) ||
        table->getRow(METHOD_AUTH_TYPE)->getField(SYS_VALUE).empty()) [[unlikely]]
    {
        PRECOMPILED_LOG(TRACE)
            << LOG_BADGE("ContractAuthMgrPrecompiled")
            << LOG_DESC("no acl strategy exist, should set the method access auth type firstly");
        return (int)CODE_TABLE_AUTH_TYPE_NOT_EXIST;
    }
    auto entry = table->getRow(METHOD_AUTH_TYPE);
    std::string authTypeStr = std::string(entry->getField(SYS_VALUE));
    std::map<bytes, uint8_t> authTypeMap;
    try
    {
        auto&& out = asBytes(authTypeStr);
        codec::scale::decode(authTypeMap, gsl::make_span(out));
        auto it = authTypeMap.find(_func.toBytes());
        if (it == authTypeMap.end())
        {
            return (int)CODE_TABLE_AUTH_TYPE_NOT_EXIST;
        }
        return it->second;
    }
    catch (...)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("decode method type failed");
        return (int)CODE_TABLE_AUTH_TYPE_DECODE_ERROR;
    }
}

MethodAuthMap ContractAuthMgrPrecompiled::getMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const std::string& path,
    int32_t getMethodType) const
{
    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        // only precompiled contract in /sys/, or pre-built-in contract
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("auth table not found.") << LOG_KV("path", path);
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Auth table not found"));
    }
    auto getTypeStr =
        getMethodType == (int)AuthType::WHITE_LIST_MODE ? METHOD_AUTH_WHITE : METHOD_AUTH_BLACK;

    auto entry = table->getRow(getTypeStr);
    MethodAuthMap authMap = {};
    if (!entry || entry->getField(SYS_VALUE).empty())
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthMgrPrecompiled")
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
    const PrecompiledExecResult::Ptr& _callParameters)

{
    /// setContractStatus(address _addr, bool isFreeze) => int256

    std::string address;
    bool isFreeze = false;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (blockContext.isWasm())
    {
        codec.decode(_callParameters->params(), address, isFreeze);
    }
    else
    {
        Address contractAddress;
        codec.decode(_callParameters->params(), contractAddress, isFreeze);
        address = contractAddress.hex();
    }
    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                          << LOG_BADGE("ContractAuthMgrPrecompiled")
                          << LOG_DESC("setContractStatus") << LOG_KV("address", address)
                          << LOG_KV("isFreeze", isFreeze);
    auto path = getAuthTableName(address);
    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("contract ACL table not found") << LOG_KV("path", path);
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
        return;
    }
    auto existEntry = table->getRow(STATUS_FIELD);
    if (versionCompareTo(blockContext.blockVersion(), BlockVersion::V3_2_VERSION) >= 0 &&
        existEntry.has_value())
    {
        auto existStatus = existEntry->get();
        // account already abolish, should not set any status to it
        if (existStatus == CONTRACT_ABOLISH)
        {
            PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                                  << LOG_BADGE("ContractAuthMgrPrecompiled")
                                  << LOG_DESC("contract already abolish, should not set any status")
                                  << LOG_KV("contract", path);
            BOOST_THROW_EXCEPTION(
                protocol::PrecompiledError("Contract already abolish, should not set any status."));
        }
    }
    auto status = isFreeze ? CONTRACT_FROZEN : CONTRACT_NORMAL;
    Entry entry = {};
    entry.importFields({std::string(status)});
    table->setRow("status", std::move(entry));
    getErrorCodeOut(_callParameters->mutableExecResult(), CODE_SUCCESS, codec);
}

void ContractAuthMgrPrecompiled::setContractStatus32(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    /// setContractStatus(address _addr, uint8) => int256
    std::string address;
    uint8_t status = 0;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (blockContext.isWasm())
    {
        codec.decode(_callParameters->params(), address, status);
    }
    else
    {
        Address contractAddress;
        codec.decode(_callParameters->params(), contractAddress, status);
        address = contractAddress.hex();
    }
    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                          << LOG_BADGE("ContractAuthMgrPrecompiled")
                          << LOG_DESC("setContractStatus") << LOG_KV("address", address)
                          << LOG_KV("status", std::to_string(status));
    auto path = getAuthTableName(address);
    auto table = _executive->storage().openTable(path);
    if (!table) [[unlikely]]
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("contract ACL table not found") << LOG_KV("path", path);
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_TABLE_NOT_EXIST, codec);
        return;
    }
    auto statusStr = StatusToString(status);
    if (statusStr.empty()) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Unrecognized status type."));
    }

    auto existEntry = table->getRow(STATUS_FIELD);
    if (existEntry.has_value())
    {
        auto existStatus = existEntry->get();
        // account already abolish, should not set any status to it
        if (existStatus == CONTRACT_ABOLISH && statusStr != CONTRACT_ABOLISH)
        {
            PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                                  << LOG_BADGE("ContractAuthMgrPrecompiled")
                                  << LOG_DESC("contract already abolish, should not set any status")
                                  << LOG_KV("contract", path) << LOG_KV("status", statusStr);
            BOOST_THROW_EXCEPTION(
                protocol::PrecompiledError("Contract already abolish, should not set any status."));
        }
    }

    Entry entry = {};
    entry.importFields({std::string(statusStr)});
    table->setRow(STATUS_FIELD, std::move(entry));
    getErrorCodeOut(_callParameters->mutableExecResult(), CODE_SUCCESS, codec);
}

void ContractAuthMgrPrecompiled::contractAvailable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    /// contractAvailable(address _addr) => bool

    std::string address;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (blockContext.isWasm())
    {
        codec.decode(_callParameters->params(), address);
    }
    else
    {
        Address contractAddress;
        codec.decode(_callParameters->params(), contractAddress);
        address = contractAddress.hex();
    }
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthMgrPrecompiled")
                           << LOG_DESC("contractAvailable") << LOG_KV("address", address);
    auto status = getContractStatus(_executive, address);
    // result !=0 && result != 1
    if (status < 0)
    {
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Cannot get contract status"));
    }
    bool result = (status == (uint8_t)ContractStatus::Available);
    _callParameters->setExecResult(codec.encode(result));
}

int32_t ContractAuthMgrPrecompiled::getContractStatus(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, std::string_view _path)
{
    auto path = getAuthTableName(_path);
    auto table = _executive->storage().openTable(path);
    if (!table)
    {
        // only precompiled contract in /sys/, or pre-built-in contract
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC("auth table not found, auth pass through by default.")
                               << LOG_KV("path", path);
        return (int)CODE_TABLE_NOT_EXIST;
    }
    auto entry = table->getRow(STATUS_FIELD);
    if (!entry)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthMgrPrecompiled")
                               << LOG_DESC(
                                      "auth status row not found, auth pass through by default.");
        return (int)CODE_TABLE_AUTH_ROW_NOT_EXIST;
    }
    auto status = entry->get();
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractAuthMgrPrecompiled")
                           << LOG_DESC("get contract status success") << LOG_KV("contract", path)
                           << LOG_KV("status", status);
    auto result = static_cast<uint8_t>(StatusFromString(status));
    return result;
}