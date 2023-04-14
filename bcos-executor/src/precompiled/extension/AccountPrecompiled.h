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
 * @file AccountPrecompiled.h
 * @author: kyonGuo
 * @date 2022/9/26
 */

#pragma once
#include "../../vm/Precompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <bcos-framework/executor/PrecompiledTypeDef.h>

namespace bcos::precompiled
{
class AccountPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<AccountPrecompiled>;
    AccountPrecompiled();
    ~AccountPrecompiled() override = default;

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) override;

    uint8_t getAccountStatus(const std::string& accountTable,
        const std::shared_ptr<executor::TransactionExecutive>& _executive) const;

private:
    void setAccountStatus(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
        PrecompiledExecResult::Ptr const& _callParameters) const;

    void getAccountStatus(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters) const;
};
}  // namespace bcos::precompiled
