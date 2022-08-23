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
    void createTable(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void createKVTable(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void appendColumns(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void openTable(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
    void desc(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const PrecompiledGas::Ptr& gasPricer, PrecompiledExecResult::Ptr const& _callParameters);
};
}  // namespace bcos::precompiled
