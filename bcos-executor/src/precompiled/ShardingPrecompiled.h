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
 * @file ShardPrecompiled.h
 * @author: JimmyShi22
 * @date 2022-12-27
 */

#pragma once
#include "../vm/Precompiled.h"
#include "BFSPrecompiled.h"

namespace bcos::precompiled
{
class ShardingPrecompiled : public bcos::precompiled::BFSPrecompiled
{
public:
    using Ptr = std::shared_ptr<ShardingPrecompiled>;
    ShardingPrecompiled(crypto::Hash::Ptr _hashImpl);
    ~ShardingPrecompiled() override = default;
    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) override;

private:
    void makeShard(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);
    void linkShard(const std::shared_ptr<executor::TransactionExecutive>& _executive,
        PrecompiledExecResult::Ptr const& _callParameters);

    const char* getThisAddress(bool _isWasm) override
    {
        return _isWasm ? SHARDING_PRECOMPILED_NAME : SHARDING_PRECOMPILED_ADDRESS;
    }

    std::string_view getLinkRootDir() override { return executor::USER_SHARD_PREFIX; }
    // only for internal call
    // void addContract2Shard(address, shardPath) {
};
}  // namespace bcos::precompiled