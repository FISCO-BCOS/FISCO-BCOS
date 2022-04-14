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
 * @file TableManagerPrecompiled.h
 * @author: kyonGuo
 * @date 2022/4/8
 */

#include "../executive/TransactionExecutive.h"
#include "../vm/Precompiled.h"
#include "Common.h"
#include "Utilities.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/interfaces/storage/Table.h>

namespace bcos::precompiled
{
using TableInfoTuple = std::tuple<std::string, std::vector<std::string>>;
class TableManagerPrecompiled : public Precompiled
{
public:
    using Ptr = std::shared_ptr<TableManagerPrecompiled>;
    TableManagerPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~TableManagerPrecompiled() = default;

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string& _origin, const std::string& _sender, int64_t gasLeft) override;
    void createTable(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer, const std::string& _origin, int64_t gasLeft);
    void appendColumns(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);
    //     void select(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    //         bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    //         const PrecompiledGas::Ptr& gasPricer);
    //     void insert(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    //         bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    //         const PrecompiledGas::Ptr& gasPricer);
    //     void update(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    //         bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    //         const PrecompiledGas::Ptr& gasPricer);
    //     void remove(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    //         bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    //         const PrecompiledGas::Ptr& gasPricer);
    //     void desc(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    //         bytesConstRef& data, const std::shared_ptr<PrecompiledExecResult>& callResult,
    //         const PrecompiledGas::Ptr& gasPricer);
};
}  // namespace bcos::precompiled
