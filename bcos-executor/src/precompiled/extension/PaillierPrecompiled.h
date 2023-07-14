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
 *  @file PaillierPrecompiled.h
 *  @author shawnhe
 *  @date 20190808
 *  @author xingqiangbai
 *  @date 20230710
 */


#pragma once
#include "../../vm/Precompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"

class CallPaillier;
namespace bcos::precompiled
{


class PaillierPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<PaillierPrecompiled>;
    PaillierPrecompiled(const crypto::Hash::Ptr& _hashImpl);
    ~PaillierPrecompiled() override= default;

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) override;
     bool isParallelPrecompiled() override { return true; }

     std::vector<std::string> getParallelTag(bytesConstRef, bool) override { return {}; }
private:
    std::shared_ptr<CallPaillier> m_callPaillier;
};

}  // namespace bcos::precompiled
