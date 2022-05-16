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
 * @file AuthManagerPrecompiled.h
 * @author: kyonRay
 * @date 2021-10-09
 */

#pragma once
#include "../../vm/Precompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <bcos-framework/interfaces/executor/PrecompiledTypeDef.h>

namespace bcos::precompiled
{

class AuthManagerPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<AuthManagerPrecompiled>;
    AuthManagerPrecompiled(crypto::Hash::Ptr _hashImpl);
    ~AuthManagerPrecompiled() override = default;

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) override;

private:
    void getAdmin(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);

    void resetAdmin(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);

    void setMethodAuthType(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);

    void checkMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);

    void getMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);

    void setMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);

    void setContractStatus(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);

    void contractAvailable(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);

    void getDeployType(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);

    void setDeployType(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);

    void hasDeployAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    void setDeployAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bool _isClose, const PrecompiledGas::Ptr& gasPricer,
        PrecompiledExecResult::Ptr const& _callParameters);

    std::string getContractAdmin(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string& _origin, const std::string& _to, int64_t _gasLeft);

    u256 getDeployAuthType(const std::shared_ptr<executor::TransactionExecutive>& _executive);

    bool checkDeployAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string& account);
};
}  // namespace bcos::precompiled