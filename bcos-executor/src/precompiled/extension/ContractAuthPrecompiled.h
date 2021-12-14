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
 * @file ContractAuthPrecompiled.h
 * @author: kyonRay
 * @date 2021-10-09
 */

#pragma once
#include "../../vm/Precompiled.h"
#include "../Common.h"
#include "../Utilities.h"
#include <bcos-framework/interfaces/executor/PrecompiledTypeDef.h>

namespace bcos::precompiled
{
using MethodAuthMap = std::map<bytes, std::map<bcos::Address, bool>>;

enum AuthType : int
{
    WHITE_LIST_MODE = 1,
    BLACK_LIST_MODE = 2
};

class ContractAuthPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<ContractAuthPrecompiled>;
    ContractAuthPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~ContractAuthPrecompiled(){};

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string& _origin, const std::string& _sender) override;

    bool checkMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string& path, bytesRef func, const Address& account);

    bool checkDeployAuth(
        const std::shared_ptr<executor::TransactionExecutive>& _executive, const Address& account);

private:
    void getAdmin(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);

    void resetAdmin(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const std::string& _sender, const PrecompiledGas::Ptr& gasPricer);

    void setMethodAuthType(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const std::string& _sender, const PrecompiledGas::Ptr& gasPricer);

    void openMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const std::string& _sender, const PrecompiledGas::Ptr& gasPricer);

    void closeMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const std::string& _sender, const PrecompiledGas::Ptr& gasPricer);

    bool checkMethodAuth(
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data);

    void setMethodAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        bool _isClose, const std::string& _sender, const PrecompiledGas::Ptr& gasPricer);

    void getDeployType(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);

    void setDeployType(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const std::string& _sender, const PrecompiledGas::Ptr& gasPricer);

    void openDeployAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const std::string& _sender, const PrecompiledGas::Ptr& gasPricer);

    void closeDeployAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const std::string& _sender, const PrecompiledGas::Ptr& gasPricer);

    void hasDeployAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult);

    void setDeployAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        bool _isClose, const std::string& _sender, const PrecompiledGas::Ptr& gasPricer);

    s256 getMethodAuthType(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string& _path, bytesConstRef _func);

    std::string getContractAdmin(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string& _path);

    u256 getDeployAuthType(const std::shared_ptr<executor::TransactionExecutive>& _executive);

    inline bool checkSender(std::string_view _sender)
    {
        return _sender == precompiled::AUTH_COMMITTEE_ADDRESS;
    }

    inline std::string getAuthTableName(const std::string& _name)
    {
        return "/apps/" + _name + executor::CONTRACT_SUFFIX;
    }
};
}  // namespace bcos::precompiled