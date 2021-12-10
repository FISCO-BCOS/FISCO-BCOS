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
 */
/** @file Metric.h
 *  @author xingqiangbai
 *  @date 20200921
 */
#pragma once
#include <array>
#include <string>

// #define WASM_FLOAT_ENABLE
namespace bcos
{
namespace wasm
{
struct Instruction
{
    std::string Name;
    uint8_t Opcode;
    uint32_t Cost;
    Instruction() = default;

    enum Enum : uint32_t
    {
#define WABT_OPCODE(rtype, type1, type2, type3, mem_size, prefix, code, Name, text, decomp) \
    Name = code,
#include "src/opcode.def"
#undef WABT_OPCODE
    };
};

using InstructionTable = std::array<Instruction, 256>;

InstructionTable GetInstructionTable();

}  // namespace wasm
}  // namespace bcos
