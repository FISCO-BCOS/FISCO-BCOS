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

#pragma once

#include "../executive/TransactionExecutive.h"
#include "../vm/Precompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/storage/Table.h>

namespace bcos::precompiled
{
class TableManagerPrecompiled : public Precompiled
{
public:
    using Ptr = std::shared_ptr<TableManagerPrecompiled>;
    TableManagerPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~TableManagerPrecompiled() = default;

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) override;

private:
    using CRUDParams = std::function<void(const std::shared_ptr<executor::TransactionExecutive>&,
        const PrecompiledGas::Ptr&, PrecompiledExecResult::Ptr const&)>;
    void createTable(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void createTableV32(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void createKVTable(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void appendColumns(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void openTable(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void desc(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void descWithKeyOrder(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void externalCreateTable(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters,
        const std::string& tableName, const CodecWrapper& codec,
        const std::string& valueField) const;

    inline void registerFunc(uint32_t _selector, CRUDParams _func,
        protocol::BlockVersion _minVersion = protocol::BlockVersion::V3_0_VERSION)
    {
        selector2Func.insert({_selector, {_minVersion, std::move(_func)}});
    }
    std::unordered_map<uint32_t, std::pair<protocol::BlockVersion, CRUDParams>> selector2Func;
};
}  // namespace bcos::precompiled
