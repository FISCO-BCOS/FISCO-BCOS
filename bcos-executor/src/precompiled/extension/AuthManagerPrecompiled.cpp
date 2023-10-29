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
#include "libinitializer/AuthInitializer.h"
#include <bcos-tool/BfsFileFactory.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/serialization/vector.hpp>

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
const char* const AUTH_METHOD_SET_CONTRACT_32 = "setContractStatus(address,uint8)";
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
const char* const AUTH_INIT = "initAuth(string)";

AuthManagerPrecompiled::AuthManagerPrecompiled(crypto::Hash::Ptr _hashImpl, bool _isWasm)
  : Precompiled(_hashImpl)
{
    const auto* getAdminStr = _isWasm ? AUTH_METHOD_GET_ADMIN : AUTH_METHOD_GET_ADMIN_ADD;
    registerFunc(getAdminStr, [this](auto&& _executive, auto&& _callParameters) {
        getAdmin(std::forward<decltype(_executive)>(_executive),
            std::forward<decltype(_callParameters)>(_callParameters));
    });

    const auto* resetAdminStr = _isWasm ? AUTH_METHOD_SET_ADMIN : AUTH_METHOD_SET_ADMIN_ADD;
    registerFunc(resetAdminStr, [this](auto&& _executive, auto&& _callParameters) {
        resetAdmin(std::forward<decltype(_executive)>(_executive),
            std::forward<decltype(_callParameters)>(_callParameters));
    });

    const auto* setMethodAuthTypeStr =
        _isWasm ? AUTH_METHOD_SET_AUTH_TYPE : AUTH_METHOD_SET_AUTH_TYPE_ADD;
    registerFunc(setMethodAuthTypeStr, [this](auto&& _executive, auto&& _callParameters) {
        setMethodAuthType(std::forward<decltype(_executive)>(_executive),
            std::forward<decltype(_callParameters)>(_callParameters));
    });

    const auto* openMethodAuthStr = _isWasm ? AUTH_METHOD_OPEN_AUTH : AUTH_METHOD_OPEN_AUTH_ADD;
    registerFunc(openMethodAuthStr, [this](auto&& _executive, auto&& _callParameters) {
        setMethodAuth(std::forward<decltype(_executive)>(_executive),
            std::forward<decltype(_callParameters)>(_callParameters));
    });

    const auto* closeMethodAuthStr = _isWasm ? AUTH_METHOD_CLOSE_AUTH : AUTH_METHOD_CLOSE_AUTH_ADD;
    registerFunc(closeMethodAuthStr, [this](auto&& _executive, auto&& _callParameters) {
        setMethodAuth(std::forward<decltype(_executive)>(_executive),
            std::forward<decltype(_callParameters)>(_callParameters));
    });

    const auto* checkMethodAuthStr = _isWasm ? AUTH_METHOD_CHECK_AUTH : AUTH_METHOD_CHECK_AUTH_ADD;
    registerFunc(checkMethodAuthStr, [this](auto&& _executive, auto&& _callParameters) {
        checkMethodAuth(std::forward<decltype(_executive)>(_executive),
            std::forward<decltype(_callParameters)>(_callParameters));
    });

    const auto* getMethodAuthStr = _isWasm ? AUTH_METHOD_GET_AUTH : AUTH_METHOD_GET_AUTH_ADD;
    registerFunc(getMethodAuthStr, [this](auto&& _executive, auto&& _callParameters) {
        getMethodAuth(std::forward<decltype(_executive)>(_executive),
            std::forward<decltype(_callParameters)>(_callParameters));
    });

    registerFunc(
        AUTH_METHOD_SET_CONTRACT_32,
        [this](auto&& _executive, auto&& _callParameters) {
            setContractStatus(std::forward<decltype(_executive)>(_executive),
                std::forward<decltype(_callParameters)>(_callParameters));
        },
        protocol::BlockVersion::V3_2_VERSION);

    registerFunc(AUTH_METHOD_SET_CONTRACT, [this](auto&& _executive, auto&& _callParameters) {
        setContractStatus(std::forward<decltype(_executive)>(_executive),
            std::forward<decltype(_callParameters)>(_callParameters));
    });
    registerFunc(AUTH_METHOD_GET_CONTRACT, [this](auto&& _executive, auto&& _callParameters) {
        contractAvailable(std::forward<decltype(_executive)>(_executive),
            std::forward<decltype(_callParameters)>(_callParameters));
    });

    /// deploy
    registerFunc(AUTH_METHOD_GET_DEPLOY_TYPE, [this](auto&& _executive, auto&& _callParameters) {
        getDeployType(std::forward<decltype(_executive)>(_executive),
            std::forward<decltype(_callParameters)>(_callParameters));
    });
    registerFunc(AUTH_METHOD_SET_DEPLOY_TYPE, [this](auto&& _executive, auto&& _callParameters) {
        setDeployType(std::forward<decltype(_executive)>(_executive),
            std::forward<decltype(_callParameters)>(_callParameters));
    });
    const auto* openDeployAccountStr =
        _isWasm ? AUTH_OPEN_DEPLOY_ACCOUNT : AUTH_OPEN_DEPLOY_ACCOUNT_ADD;
    registerFunc(openDeployAccountStr, [this](auto&& _executive, auto&& _callParameters) {
        openDeployAuth(std::forward<decltype(_executive)>(_executive),
            std::forward<decltype(_callParameters)>(_callParameters));
    });
    const auto* closeDeployAccountStr =
        _isWasm ? AUTH_CLOSE_DEPLOY_ACCOUNT : AUTH_CLOSE_DEPLOY_ACCOUNT_ADD;
    registerFunc(closeDeployAccountStr, [this](auto&& _executive, auto&& _callParameters) {
        closeDeployAuth(std::forward<decltype(_executive)>(_executive),
            std::forward<decltype(_callParameters)>(_callParameters));
    });
    const auto* checkDeployAuthStr =
        _isWasm ? AUTH_CHECK_DEPLOY_ACCESS : AUTH_CHECK_DEPLOY_ACCESS_ADD;
    registerFunc(checkDeployAuthStr, [this](auto&& _executive, auto&& _callParameters) {
        hasDeployAuth(std::forward<decltype(_executive)>(_executive),
            std::forward<decltype(_callParameters)>(_callParameters));
    });

    registerFunc(
        AUTH_INIT,
        [this](auto&& _executive, auto&& _callParameters) {
            initAuth(std::forward<decltype(_executive)>(_executive),
                std::forward<decltype(_callParameters)>(_callParameters));
        },
        protocol::BlockVersion::V3_3_VERSION);
}

std::shared_ptr<PrecompiledExecResult> AuthManagerPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    // parse function name
    uint32_t func = getParamFunc(_callParameters->input());
    const auto& blockContext = _executive->blockContext();

    /// directly passthrough data to call
    auto selector = selector2Func.find(func);
    if (selector != selector2Func.end())
    {
        auto& [minVersion, execFunc] = selector->second;
        if (versionCompareTo(blockContext.blockVersion(), minVersion) >= 0)
        {
            execFunc(_executive, _callParameters);

            if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
            {
                PRECOMPILED_LOG(TRACE)
                    << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("call function")
                    << LOG_KV("func", func) << LOG_KV("minVersion", minVersion);
            }
            return _callParameters;
        }
    }
    PRECOMPILED_LOG(INFO) << LOG_BADGE("AuthManagerPrecompiled")
                          << LOG_DESC("call undefined function") << LOG_KV("func", func);
    BOOST_THROW_EXCEPTION(
        bcos::protocol::PrecompiledError("AuthManagerPrecompiled call undefined function!"));
}

void AuthManagerPrecompiled::getAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)

{
    bytesConstRef data = getParamData(_callParameters->input());
    std::string path;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (blockContext.isWasm())
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
        blockContext.isWasm() ? codec.encode(adminStr) : codec.encode(Address(adminStr)));
}

void AuthManagerPrecompiled::resetAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)

{
    std::string path;
    std::string admin;
    bytesConstRef data = getParamData(_callParameters->input());
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (!blockContext.isWasm())
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
    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number())
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
    std::string authMgrAddress = blockContext.isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    auto response = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        authMgrAddress, path, _callParameters->m_staticCall, _callParameters->m_create,
        _callParameters->m_gasLeft, true);
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
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    auto beginT = utcTime();
    if (!blockContext.isWasm())
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
    std::string authMgrAddress = blockContext.isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;
    auto response = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        authMgrAddress, path, _callParameters->m_staticCall, _callParameters->m_create,
        _callParameters->m_gasLeft, true);
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
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (!blockContext.isWasm())
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
    std::string authMgrAddress = blockContext.isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    auto response = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        authMgrAddress, path, _callParameters->m_staticCall, _callParameters->m_create,
        _callParameters->m_gasLeft, true);

    _callParameters->setExternalResult(std::move(response));
}

void AuthManagerPrecompiled::getMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    std::string path;
    string32 _func;
    bytesConstRef data = getParamData(_callParameters->input());
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (!blockContext.isWasm())
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
    std::string authMgrAddress = blockContext.isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    auto response = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        authMgrAddress, path, _callParameters->m_staticCall, _callParameters->m_create,
        _callParameters->m_gasLeft, true);

    _callParameters->setExternalResult(std::move(response));
}

void AuthManagerPrecompiled::setMethodAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    std::string path;
    std::string account;
    string32 _func;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    bytesConstRef data = getParamData(_callParameters->input());
    auto recordT = utcTime();
    if (!blockContext.isWasm())
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
    std::string authMgrAddress = blockContext.isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;
    auto response = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        authMgrAddress, path, _callParameters->m_staticCall, _callParameters->m_create,
        _callParameters->m_gasLeft, true);
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
    uint8_t status = 0;
    bytesConstRef data = getParamData(_callParameters->input());
    auto func = getParamFunc(_callParameters->input());
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (func == getFuncSelector(AUTH_METHOD_SET_CONTRACT, m_hashImpl))
    {
        Address contractAddress;
        codec.decode(data, contractAddress, isFreeze);
        address = contractAddress.hex();
    }
    else if (func == getFuncSelector(AUTH_METHOD_SET_CONTRACT_32, m_hashImpl))
    {
        Address contractAddress;
        codec.decode(data, contractAddress, status);
        address = contractAddress.hex();
    }
    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number())
                           << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("setContractStatus")
                           << LOG_KV("address", address) << LOG_KV("isFreeze", isFreeze)
                           << LOG_KV("status", std::to_string(status));

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
    std::string authMgrAddress = blockContext.isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    auto response = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        authMgrAddress, address, _callParameters->m_staticCall, _callParameters->m_create,
        _callParameters->m_gasLeft, true);

    _callParameters->setExternalResult(std::move(response));
}

void AuthManagerPrecompiled::contractAvailable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    std::string address;
    bytesConstRef data = getParamData(_callParameters->input());
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (blockContext.isWasm())
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
    std::string authMgrAddress = blockContext.isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    auto response = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        authMgrAddress, address, _callParameters->m_staticCall, _callParameters->m_create,
        _callParameters->m_gasLeft, true);

    _callParameters->setExternalResult(std::move(response));
}

std::string AuthManagerPrecompiled::getContractAdmin(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const std::string& _to,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());

    std::string authMgrAddress = blockContext.isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    bytes selector = blockContext.isWasm() ?
                         codec.encodeWithSig(AUTH_METHOD_GET_ADMIN, _to) :
                         codec.encodeWithSig(AUTH_METHOD_GET_ADMIN_ADD, Address(_to));
    auto data = codec.encode(std::string(AUTH_CONTRACT_MGR_ADDRESS), selector);
    auto response =
        externalRequest(_executive, ref(data), _callParameters->m_origin, authMgrAddress, _to,
            _callParameters->m_staticCall, false, _callParameters->m_gasLeft, true);

    if (response->status != (int32_t)protocol::TransactionStatus::None)
    {
        PRECOMPILED_LOG(DEBUG) << "Can't get contract admin, check the contract existence."
                               << LOG_KV("address", _to);
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError("Please check the existence of contract."));
    }
    std::string admin;

    codec.decode(ref(response->data), admin);

    return admin;
}

u256 AuthManagerPrecompiled::getDeployAuthType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive)
{
    std::string typeStr = "";
    if (_executive->blockContext().blockVersion() >=
        static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION))
    {
        auto entry = _executive->storage().getRow(tool::FS_ROOT, tool::FS_APPS.substr(1));
        // apps must exist
        if (!entry) [[unlikely]]
        {
            PRECOMPILED_LOG(FATAL)
                << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("apps not exist");
            return {};
        }
        auto fields = entry->getObject<std::vector<std::string>>();
        typeStr.assign(fields[2]);
    }
    else
    {
        auto entry = _executive->storage().getRow(tool::FS_APPS, tool::FS_ACL_TYPE);
        // entry must exist
        if (entry) [[likely]]
        {
            typeStr.assign(entry->get());
        }
        else if (_executive->blockContext().blockVersion() <=>
                     protocol::BlockVersion::V3_0_VERSION ==
                 0) [[unlikely]]
        {
            // Note: when 3.0.0 -> 3.1.0 upgrade tx concurrent with deploy contract tx,
            // the deploy-contract tx will be failed, because the FS_APPS is not exist.
            // Try to read in 3.1.0 format.
            entry = _executive->storage().getRow(tool::FS_ROOT, tool::FS_APPS.substr(1));
            // apps must exist
            if (!entry) [[unlikely]]
            {
                PRECOMPILED_LOG(FATAL)
                    << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("apps not exist");
                return {};
            }
            auto fields = entry->getObject<std::vector<std::string>>();
            typeStr.assign(fields[2]);
        }
    }
    u256 type = 0;
    try
    {
        type = boost::lexical_cast<u256>(typeStr);
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
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());

    u256 type = getDeployAuthType(_executive);
    _callParameters->setExecResult(codec.encode(type));
}

void AuthManagerPrecompiled::setDeployType(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    string32 _type;
    bytesConstRef data = getParamData(_callParameters->input());
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(data, _type);
    if (!checkSenderFromAuth(_callParameters->m_sender))
    {
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }
    u256 type = _type[_type.size() - 1];
    PRECOMPILED_LOG(INFO) << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("setDeployType")
                          << LOG_KV("type", type);
    if (type > 2) [[unlikely]]
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("AuthManagerPrecompiled")
                              << LOG_DESC("deploy auth type must be 1 or 2.");
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_TABLE_ERROR_AUTH_TYPE, codec);
        return;
    }
    if (blockContext.blockVersion() >= static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION))
    {
        auto entry = _executive->storage().getRow(tool::FS_ROOT, tool::FS_APPS.substr(1));
        // apps must exist
        if (!entry) [[unlikely]]
        {
            PRECOMPILED_LOG(FATAL)
                << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("apps not exist");
        }
        auto fields = entry->getObject<std::vector<std::string>>();
        fields[2] = boost::lexical_cast<std::string>(type);
        entry->setObject(fields);
        _executive->storage().setRow(
            tool::FS_ROOT, tool::FS_APPS.substr(1), std::move(entry.value()));
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_SUCCESS, codec);
        return;
    }
    Entry entry;
    entry.importFields({boost::lexical_cast<std::string>(type)});
    _executive->storage().setRow(tool::FS_APPS, tool::FS_ACL_TYPE, std::move(entry));

    getErrorCodeOut(_callParameters->mutableExecResult(), CODE_SUCCESS, codec);
}

void AuthManagerPrecompiled::setDeployAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bool _isClose,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    std::string account;
    bytesConstRef data = getParamData(_callParameters->input());
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (blockContext.isWasm())
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
    PRECOMPILED_LOG(INFO) << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("setDeployAuth")
                          << LOG_KV("account", account) << LOG_KV("isClose", _isClose);
    auto type = getDeployAuthType(_executive);
    std::map<std::string, bool> aclMap;
    bool access = _isClose ? (type == (int)AuthType::BLACK_LIST_MODE) :
                             (type == (int)AuthType::WHITE_LIST_MODE);

    if (blockContext.blockVersion() >= static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION))
    {
        auto entry = _executive->storage().getRow(tool::FS_ROOT, tool::FS_APPS.substr(1));
        // apps must exist
        if (!entry) [[unlikely]]
        {
            PRECOMPILED_LOG(FATAL)
                << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("apps not exist");
        }
        auto fields = entry->getObject<std::vector<std::string>>();

        const auto insertIndex = (type == (int)AuthType::WHITE_LIST_MODE) ? 3 : 4;

        auto mapStr = std::string(fields.at(insertIndex));
        if (!mapStr.empty())
        {
            auto&& out = asBytes(mapStr);
            codec::scale::decode(aclMap, gsl::make_span(out));
        }
        // covered writing
        aclMap[account] = access;
        fields[insertIndex] = asString(codec::scale::encode(aclMap));
        entry->setObject(fields);

        _executive->storage().setRow(
            tool::FS_ROOT, tool::FS_APPS.substr(1), std::move(entry.value()));
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_SUCCESS, codec);
        return;
    }

    auto getAclStr =
        (type == (int)AuthType::BLACK_LIST_MODE) ? tool::FS_ACL_BLACK : tool::FS_ACL_WHITE;
    auto entry = _executive->storage().getRow(tool::FS_APPS, getAclStr);
    auto mapStr = std::string(entry->getField(0));
    if (!mapStr.empty())
    {
        auto&& out = asBytes(mapStr);
        codec::scale::decode(aclMap, gsl::make_span(out));
    }
    // covered writing
    aclMap[account] = access;
    entry->setField(0, asString(codec::scale::encode(aclMap)));
    _executive->storage().setRow(tool::FS_APPS, getAclStr, std::move(entry.value()));

    getErrorCodeOut(_callParameters->mutableExecResult(), CODE_SUCCESS, codec);
}

void AuthManagerPrecompiled::hasDeployAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    std::string account;
    bytesConstRef data = getParamData(_callParameters->input());
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (blockContext.isWasm())
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
    auto type = getDeployAuthType(_executive);
    if (type == 0)
    {
        return true;
    }
    std::map<std::string, bool> aclMap;
    std::string aclMapStr;

    if (_executive->blockContext().blockVersion() >=
        static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION))
    {
        auto entry = _executive->storage().getRow(tool::FS_ROOT, tool::FS_APPS.substr(1));
        // apps must exist
        if (!entry) [[unlikely]]
        {
            PRECOMPILED_LOG(FATAL)
                << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("apps not exist");
        }
        auto fields = entry->getObject<std::vector<std::string>>();
        auto getAclIndex = (type == (int)AuthType::WHITE_LIST_MODE) ? 3 : 4;
        aclMapStr.assign(fields.at(getAclIndex));
    }
    else
    {
        auto getAclType =
            (type == (int)AuthType::WHITE_LIST_MODE) ? tool::FS_ACL_WHITE : tool::FS_ACL_BLACK;
        auto entry = _executive->storage().getRow(tool::FS_APPS, getAclType);
        aclMapStr.assign(entry->get());
    }

    if (aclMapStr.empty())
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("AuthManagerPrecompiled")
                               << LOG_DESC("not deploy acl exist, return by default")
                               << LOG_KV("aclType", type);
        // if entry still empty
        //      if white list mode, return false
        //      if black list mode， return true
        return type == (int)AuthType::BLACK_LIST_MODE;
    }
    auto&& out = asBytes(aclMapStr);
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

void AuthManagerPrecompiled::initAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    std::string account;
    bytesConstRef data = getParamData(_callParameters->input());
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(data, account);

    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                          << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("initAuth")
                          << LOG_KV("admin", account);

    // check auth contract exist
    auto table = _executive->storage().openTable(
        std::string(USER_SYS_PREFIX).append(AUTH_COMMITTEE_ADDRESS));
    if (table.has_value())
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("AuthManagerPrecompiled")
                              << LOG_DESC("Committee exists.");
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Committee contract already exist."));
    }

    std::string authMgrAddress = blockContext.isWasm() ? AUTH_MANAGER_NAME : AUTH_MANAGER_ADDRESS;

    std::vector<Address> initGovernors({Address(account)});
    std::vector<string32> weights({bcos::codec::toString32(h256(1))});
    bytes code;
    std::string_view bin = blockContext.hashHandler()->getHashImplType() == crypto::Sm3Hash ?
                               bcos::initializer::committeeSmBin :
                               bcos::initializer::committeeBin;
    code.reserve(bin.size() / 2);
    boost::algorithm::unhex(bin, std::back_inserter(code));
    bytes input = code + codec.encode(initGovernors, weights, codec::toString32(h256(0)),
                             codec::toString32(h256(0)));

    auto response = externalRequest(_executive, ref(input), _callParameters->m_origin,
        authMgrAddress, AUTH_COMMITTEE_ADDRESS, false, true, _callParameters->m_gasLeft, false);

    if (response->status != (int32_t)protocol::TransactionStatus::None)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("AuthManagerPrecompiled")
                              << LOG_DESC("init auth error.");
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("Create auth contract error."));
    }

    _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
}