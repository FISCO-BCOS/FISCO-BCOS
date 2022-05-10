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
#include "../Common.h"
#include "../Utilities.h"
#include <bcos-framework/interfaces/executor/PrecompiledTypeDef.h>

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
    ContractAuthMgrPrecompiled(crypto::Hash::Ptr _hashImpl);
    ~ContractAuthMgrPrecompiled() override = default;

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string& _origin, const std::string& _sender, int64_t gasLeft) override;

    bool checkMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string& path, bytesRef func, const std::string& account);

    bool checkContractAvailable(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string& _path);

private:
    void getAdmin(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);

    void resetAdmin(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);

    void setMethodAuthType(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);

    void checkMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);

    void getMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);

    void setMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        bool _isClose, const PrecompiledGas::Ptr& gasPricer);

    void setContractStatus(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);

    void contractAvailable(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);

    int32_t getMethodAuthType(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string& _path, bytesConstRef _func);

    MethodAuthMap getMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string& path, int32_t authType) const;

    std::string getContractAdmin(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string& _path);

    inline std::string getAuthTableName(const std::string& _name)
    {
        return executor::USER_APPS_PREFIX + _name + executor::CONTRACT_SUFFIX;
    }
};
}  // namespace bcos::precompiled