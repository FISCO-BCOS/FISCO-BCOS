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
 * @file BalancePrecompiled.h
 * @author: wenlinli
 * @date 2023-10-26
 */
#pragma once
#include "../../vm/Precompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <bcos-framework/executor/PrecompiledTypeDef.h>

namespace bcos::precompiled
{
class BalancePrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<BalancePrecompiled>;
    BalancePrecompiled(crypto::Hash::Ptr _hashImpl);
    ~BalancePrecompiled() override = default;

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) override;

private:
    void getBalance(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    void addBalance(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    void subBalance(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    void transfer(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    void registerCaller(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    void unregisterCaller(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    void listCaller(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    void createAccount(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters, const CodecWrapper& codec,
        std::string_view accountHex);

    std::string getContractTableName(
        const std::shared_ptr<executor::TransactionExecutive>& _executive,
        const std::string_view& _address);

    void checkOriginAuth(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters, const CodecWrapper& codec);

private:
    mutable std::map<std::string, u256> m_fakeBalancePrecompiled;
};
}  // namespace bcos::precompiled