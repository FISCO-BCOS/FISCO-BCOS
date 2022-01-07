/*
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
 * @brief factory of vm
 * @file VMFactory.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#pragma once
#include <memory>
#include <string>
#include <vector>

namespace bcos
{
namespace executor
{
class VMInstance;
enum class VMKind
{
    evmone,
    BcosWasm,
    DLL
};

class VMFactory
{
public:
    VMFactory() = delete;
    ~VMFactory() = delete;

    /// Creates a VM instance of the global kind.
    static VMInstance create();

    /// Creates a VM instance of the kind provided.
    static VMInstance create(VMKind _kind);
};
}  // namespace executor
}  // namespace bcos
