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
 * @file HelloWorldPrecompiled.h
 * @author: kyonRay
 * @date 2021-05-30
 */

#pragma once

#include "../../vm/Precompiled.h"
#include "../Common.h"

namespace bcos
{
namespace precompiled
{
class HelloWorldPrecompiled : public bcos::precompiled::Precompiled
{
public:
    using Ptr = std::shared_ptr<HelloWorldPrecompiled>;
    HelloWorldPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~HelloWorldPrecompiled(){};

    std::string toString() override;

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string& _origin, const std::string& _sender) override;
};
}  // namespace precompiled
}  // namespace bcos
