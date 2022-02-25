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
 * @file DagTransferPrecompiled.h
 * @author: kyonRay
 * @date 2021-05-30
 */

#pragma once
#include "../../vm/Precompiled.h"
#include "../Common.h"
#include <bcos-framework/interfaces/storage/Table.h>

namespace bcos
{
namespace precompiled
{
class DagTransferPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<DagTransferPrecompiled>;
    DagTransferPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~DagTransferPrecompiled(){};

    std::string toString() override;

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string& _origin, const std::string& _sender) override;

public:
    // is this precompiled need parallel processing, default false.
    bool isParallelPrecompiled() override { return true; }
    std::vector<std::string> getParallelTag(bytesConstRef param, bool _isWasm) override;

public:
    void userAddCall(std::shared_ptr<executor::TransactionExecutive> _executive,
        bytesConstRef _data, std::string const& _origin, bytes& _out);
    void userSaveCall(std::shared_ptr<executor::TransactionExecutive> _executive,
        bytesConstRef _data, std::string const& _origin, bytes& _out);
    void userDrawCall(std::shared_ptr<executor::TransactionExecutive> _executive,
        bytesConstRef _data, std::string const& _origin, bytes& _out);
    void userBalanceCall(std::shared_ptr<executor::TransactionExecutive> _executive,
        bytesConstRef _data, bytes& _out);
    void userTransferCall(std::shared_ptr<executor::TransactionExecutive> _executive,
        bytesConstRef _data, std::string const& _origin, bytes& _out);
};

}  // namespace precompiled

}  // namespace bcos