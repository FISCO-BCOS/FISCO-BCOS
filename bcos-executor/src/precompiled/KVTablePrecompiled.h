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
 * @file KVTablePrecompiled.h
 * @author: kyonRay
 * @date 2021-05-27
 */

#pragma once

#include "../vm/Precompiled.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/interfaces/storage/Table.h>

namespace bcos::precompiled
{
#if 0
contract KVTableFactory {
    function openTable(string) public constant returns (KVTable);
    function createTable(string, string, string) public returns (bool,int);
}
#endif

class KVTablePrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<KVTablePrecompiled>;
    KVTablePrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~KVTablePrecompiled(){};

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) override;

private:
    void get(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
        const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);
    void set(const std::string& tableName,
        const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
        const std::shared_ptr<PrecompiledExecResult>& callResult,
        const PrecompiledGas::Ptr& gasPricer);
};

}  // namespace bcos::precompiled