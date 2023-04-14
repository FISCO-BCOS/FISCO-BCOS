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
 * @file ContractAuthMgrPrecompiled.h
 * @author: kyonGuo
 * @date 2022/4/15
 */

#pragma once

#include "../../vm/Precompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <bcos-framework/executor/PrecompiledTypeDef.h>

namespace bcos::precompiled
{
/// func => (address, access)
using MethodAuthMap = std::map<bytes, std::map<std::string, bool>>;

enum AuthType : int
{
    WHITE_LIST_MODE = 1,
    BLACK_LIST_MODE = 2
};

class ContractAuthMgrPrecompiled : public bcos::precompiled::Precompiled
{
public:
    ContractAuthMgrPrecompiled(crypto::Hash::Ptr _hashImpl, bool _isWasm);
    ~ContractAuthMgrPrecompiled() override = default;

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) override;

    bool checkMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string_view& path, bytesRef func, const std::string& account);

    int32_t getContractStatus(
        const std::shared_ptr<executor::TransactionExecutive>& _executive, std::string_view _path);

private:
    void getAdmin(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    void resetAdmin(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    void setMethodAuthType(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    void checkMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    void getMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    inline void closeMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters) const
    {
        setMethodAuth(_executive, true, _callParameters);
    }

    inline void openMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters) const
    {
        setMethodAuth(_executive, false, _callParameters);
    }

    void setMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bool _isClose, PrecompiledExecResult::Ptr const& _callParameters) const;

    void setContractStatus(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    void setContractStatus32(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    void contractAvailable(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    int32_t getMethodAuthType(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string& _path, bytesConstRef _func) const;

    MethodAuthMap getMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string& path, int32_t authType) const;

    inline std::string getAuthTableName(std::string_view _name) const
    {
        std::string tableName;
        tableName.reserve(
            executor::USER_APPS_PREFIX.size() + _name.size() + executor::CONTRACT_SUFFIX.size());
        // /apps/ + name + _accessAuth
        tableName.append(executor::USER_APPS_PREFIX);
        tableName.append(_name);
        tableName.append(executor::CONTRACT_SUFFIX);
        return tableName;
    }
};
}  // namespace bcos::precompiled