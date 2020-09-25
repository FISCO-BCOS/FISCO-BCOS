/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file Metric.h
 *  @author xingqiangbai
 *  @date 20200921
 */
#pragma once
#include <string>
#include <array>

namespace wasm{

struct Instruction{
    std::string Name;
    uint8_t Opcode;
    uint32_t Cost;
    Instruction() = default;

  enum Enum : uint32_t {
#define WABT_OPCODE(rtype, type1, type2, type3, mem_size, prefix, code, Name, text, decomp) \
  Name=code,
#include "src/opcode.def"
#undef WABT_OPCODE
  };

};

using InstructionTable = std::array<Instruction, 256>;

InstructionTable GetInstructionTable();

}


