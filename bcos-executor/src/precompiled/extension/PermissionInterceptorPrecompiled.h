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
 * @file PermissionInterceptorPrecompiled.h
 * @author: kyonGuo
 * @date 2022/4/14
 */

#include "PermissionPrecompiledInterface.h"

namespace bcos::precompiled
{
class PermissionInterceptorPrecompiled : public PermissionPrecompiledInterface
{
public:
    PermissionInterceptorPrecompiled(crypto::Hash::Ptr _hashImpl);
    ~PermissionInterceptorPrecompiled() = default;
    std::shared_ptr<PrecompiledExecResult> call(std::shared_ptr<executor::TransactionExecutive>,
        bytesConstRef, const std::string&, const std::string&, int64_t) override;

    /// not support now
    PermissionRet::Ptr login(const std::string&, const std::vector<std::string>&) override
    {
        return nullptr;
    }

    /// not support now
    PermissionRet::Ptr logout(const std::string&, const std::vector<std::string>&) override
    {
        return nullptr;
    }
    PermissionRet::Ptr create(const std::string& userPath, const std::string& origin,
        const std::string& to, bytesConstRef params) override;

    /// call not carry sender now
    PermissionRet::Ptr call(
        const std::string&, const std::string&, const std::string&, bytesConstRef) override
    {
        return nullptr;
    }
    PermissionRet::Ptr sendTransaction(const std::string& userPath, const std::string& origin,
        const std::string& to, bytesConstRef params) override;
};
}  // namespace bcos::precompiled