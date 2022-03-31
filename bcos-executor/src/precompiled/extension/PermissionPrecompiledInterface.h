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
 * @file PermissionPrecompiledInterface.h
 * @author: kyonRay
 * @date 2021-09-08
 */

#pragma once
#include "../../vm/Precompiled.h"
#include "../Common.h"

namespace bcos::precompiled
{
const char* const PER_METHOD_LOGIN = "login(string,string[])";
const char* const PER_METHOD_LOGOUT = "logout(string,string[])";
const char* const PER_METHOD_CREATE = "create(string,string,string,bytes)";
const char* const PER_METHOD_CALL = "call(string,string,string,bytes)";
const char* const PER_METHOD_SEND = "sendTransaction(string,string,string,bytes)";
struct PermissionRet
{
    using Ptr = std::shared_ptr<PermissionRet>;
    s256 code;
    std::string message;
    std::string path;
    PermissionRet(s256&& _code, std::string&& _msg) : code(_code), message(_msg) {}
    PermissionRet(s256&& _code, std::string&& _msg, std::string&& _path)
      : code(_code), message(_msg), path(_path)
    {}
};
class PermissionPrecompiledInterface : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<PermissionPrecompiledInterface>;
    PermissionPrecompiledInterface(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl){};
    virtual ~PermissionPrecompiledInterface(){};

    std::shared_ptr<PrecompiledExecResult> call(std::shared_ptr<executor::TransactionExecutive>,
        bytesConstRef, const std::string&, const std::string&, int64_t) override
    {
        return nullptr;
    }

    virtual PermissionRet::Ptr login(
        const std::string& nonce, const std::vector<std::string>& params) = 0;

    virtual PermissionRet::Ptr logout(
        const std::string& path, const std::vector<std::string>& params) = 0;

    virtual PermissionRet::Ptr create(const std::string& userPath, const std::string& origin,
        const std::string& to, bytesConstRef params) = 0;

    virtual PermissionRet::Ptr call(const std::string& userPath, const std::string& origin,
        const std::string& to, bytesConstRef params) = 0;

    virtual PermissionRet::Ptr sendTransaction(const std::string& userPath,
        const std::string& origin, const std::string& to, bytesConstRef params) = 0;
};
}  // namespace bcos::precompiled
