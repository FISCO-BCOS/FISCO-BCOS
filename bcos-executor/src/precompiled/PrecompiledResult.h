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
 * @file PrecompiledResult.h
 * @author: kyonRay
 * @date 2021-05-25
 */
#pragma once
#include "PrecompiledGas.h"
#include <bcos-framework/libutilities/Common.h>

namespace bcos
{
namespace precompiled
{
struct PrecompiledExecResult
{
    using Ptr = std::shared_ptr<PrecompiledExecResult>;
    PrecompiledExecResult() = default;
    ~PrecompiledExecResult() {}
    bytes const& execResult() const { return m_execResult; }
    bytes& mutableExecResult() { return m_execResult; }
    void setExecResult(bytes const& _execResult) { m_execResult = _execResult; }
    void setGas(int64_t _gas) { m_gas = _gas; }
    bytes m_execResult;
    int64_t m_gas;
};
}  // namespace precompiled
}  // namespace bcos
