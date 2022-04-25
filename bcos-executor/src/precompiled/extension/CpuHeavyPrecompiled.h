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
 * @file CpuHeavyPrecompiled.h
 * @author: jimmyshi
 * @date 2022-02-23
 */

#pragma once

#include "../../vm/Precompiled.h"
#include "../Common.h"

namespace bcos
{
namespace precompiled
{
class CpuHeavyPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<CpuHeavyPrecompiled>;

    CpuHeavyPrecompiled(crypto::Hash::Ptr _hashImpl);

    virtual ~CpuHeavyPrecompiled(){};

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string& _origin, const std::string& _sender, int64_t gasLeft) override;

    // is this precompiled need parallel processing, default false.
    virtual bool isParallelPrecompiled() override { return true; }

    virtual std::vector<std::string> getParallelTag(bytesConstRef param, bool _isWasm) override
    {
        (void)param;
        (void)_isWasm;
        return std::vector<std::string>();
    };

    static std::string getAddress(unsigned int id)
    {
        u160 address = u160("0x5200");
        address += id;
        h160 addressBytes = h160(address);
        return addressBytes.hex();
    }

    static void registerPrecompiled(
        std::shared_ptr<std::map<std::string, std::shared_ptr<precompiled::Precompiled>>>
            registeredMap,
        crypto::Hash::Ptr _hashImpl)
    {
        for (int id = 0; id < 128; id++)
        {
            std::string address = getAddress(id);
            BCOS_LOG(INFO) << LOG_BADGE("CpuHeavy") << "Register CpuHeavyPrecompiled "
                           << LOG_KV("address", address);
            registeredMap->insert({std::move(address),
                std::make_shared<precompiled::CpuHeavyPrecompiled>(_hashImpl)});
        }
    }
};
}  // namespace precompiled
}  // namespace bcos
