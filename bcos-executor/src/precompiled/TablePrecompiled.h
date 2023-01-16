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
 * @file TablePrecompiled.h
 * @author: kyonGuo
 * @date 2022/4/20
 */

#pragma once

#include "../vm/Precompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/precompiled/common/Condition.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/storage/Table.h>

namespace bcos::precompiled
{
class TablePrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<TablePrecompiled>;
    TablePrecompiled(crypto::Hash::Ptr _hashImpl);
    ~TablePrecompiled() override = default;

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) override;

private:
    using CRUDParams = std::function<void(const std::string&,
        const std::shared_ptr<executor::TransactionExecutive>&, bytesConstRef&,
        const PrecompiledGas::Ptr&, PrecompiledExecResult::Ptr const&)>;

    void selectByKey(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void selectByCondition(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void selectByConditionV32(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void count(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void countV32(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void insert(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void updateByKey(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void updateByCondition(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void updateByConditionV32(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void removeByKey(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void removeByCondition(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void removeByConditionV32(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void buildKeyCondition(std::shared_ptr<storage::Condition>& keyCondition,
        const std::vector<precompiled::ConditionTuple>& conditions,
        const precompiled::LimitTuple& limit) const;
    bool buildConditions(std::optional<precompiled::Condition>& valueCondition,
        const std::vector<precompiled::ConditionTupleV320>& conditions, 
        const precompiled::LimitTuple& limit, precompiled::TableInfoTupleV320& tableInfo) const;
    void desc(precompiled::TableInfo& _tableInfo, const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters, bool withKeyOrder) const;
    bool isNumericalOrder(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledExecResult::Ptr& _callParameters, const std::string& _tableName);
    static bool isNumericalOrder(const TableInfoTupleV320& tableInfo);

    inline void registerFunc(uint32_t _selector, CRUDParams _func,
        protocol::BlockVersion _minVersion = protocol::BlockVersion::V3_0_VERSION)
    {
        selector2Func.insert({_selector, {_minVersion, std::move(_func)}});
    }
    std::unordered_map<uint32_t, std::pair<protocol::BlockVersion, CRUDParams>> selector2Func;
};
}  // namespace bcos::precompiled