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
/** @file Metric.cpp
 *  @author xingqiangbai
 *  @date 20200921
 */
#include "Metric.h"
#include <limits>

namespace wasm
{
InstructionTable GetInstructionTable()
{
    auto defaultInstructionTable = InstructionTable{};
#define WABT_OPCODE(rtype, type1, type2, type3, mem_size, prefix, code, Name, text, decomp) \
    defaultInstructionTable[Instruction::Enum::Name] =                                      \
        Instruction{text, code, std::numeric_limits<uint32_t>::max()};
#include "src/opcode.def"
#undef WABT_OPCODE
    // Only allow instructions in wasm v1.0 standard
    // https://www.w3.org/TR/2019/REC-wasm-core-1-20191205/
    // http://webassembly.org.cn/docs/binary-encoding/

    // Registers
    defaultInstructionTable[Instruction::Enum::LocalGet].Cost = 3;
    defaultInstructionTable[Instruction::Enum::LocalSet].Cost = 3;
    defaultInstructionTable[Instruction::Enum::LocalTee].Cost = 3;
    defaultInstructionTable[Instruction::Enum::GlobalGet].Cost = 3;
    defaultInstructionTable[Instruction::Enum::GlobalSet].Cost = 3;

    // Memory
    defaultInstructionTable[Instruction::Enum::I32Load].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I32Load8S].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I32Load8U].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I32Load16S].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I32Load16U].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I64Load].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I64Load8S].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I64Load8U].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I64Load16S].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I64Load16U].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I64Load32S].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I64Load32U].Cost = 3;

    defaultInstructionTable[Instruction::Enum::I32Store].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I32Store8].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I32Store16].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I64Store].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I64Store8].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I64Store16].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I64Store32].Cost = 3;

    defaultInstructionTable[Instruction::Enum::MemorySize].Cost = 3;
    defaultInstructionTable[Instruction::Enum::MemoryGrow].Cost = 5 * 64 * 1024;

    // Flow Control
    defaultInstructionTable[Instruction::Enum::Unreachable].Cost = 0;
    defaultInstructionTable[Instruction::Enum::Nop].Cost = 0;
    defaultInstructionTable[Instruction::Enum::Block].Cost = 0;
    defaultInstructionTable[Instruction::Enum::Loop].Cost = 0;
    defaultInstructionTable[Instruction::Enum::If].Cost = 0;
    defaultInstructionTable[Instruction::Enum::Else].Cost = 2;
    // defaultInstructionTable[Instruction::Enum::Try].Cost = 0;
    // defaultInstructionTable[Instruction::Enum::Catch].Cost = 0;
    // defaultInstructionTable[Instruction::Enum::Throw].Cost = 0;
    // defaultInstructionTable[Instruction::Enum::Rethrow].Cost = 0;
    // defaultInstructionTable[Instruction::Enum::BrOnExn].Cost = 0;
    defaultInstructionTable[Instruction::Enum::End].Cost = 0;
    defaultInstructionTable[Instruction::Enum::Br].Cost = 2;
    defaultInstructionTable[Instruction::Enum::BrIf].Cost = 3;
    defaultInstructionTable[Instruction::Enum::BrTable].Cost = 2;
    defaultInstructionTable[Instruction::Enum::Return].Cost = 2;

    // Calls
    defaultInstructionTable[Instruction::Enum::Call].Cost = 3;
    defaultInstructionTable[Instruction::Enum::CallIndirect].Cost = 2;
    // defaultInstructionTable[Instruction::Enum::ReturnCall].Cost = 2;
    // defaultInstructionTable[Instruction::Enum::ReturnCallIndirect].Cost = 2;

    // Constants
    defaultInstructionTable[Instruction::Enum::I32Const].Cost = 0;
    defaultInstructionTable[Instruction::Enum::I64Const].Cost = 0;
    // defaultInstructionTable[Instruction::Enum::F32Const].Cost = 0;
    // defaultInstructionTable[Instruction::Enum::F64Const].Cost = 0;

    // 32-bit Integer operators
    defaultInstructionTable[Instruction::Enum::I32Clz].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I32Ctz].Cost = 6;
    defaultInstructionTable[Instruction::Enum::I32Popcnt].Cost = 3;

    defaultInstructionTable[Instruction::Enum::I32Add].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I32Sub].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I32Mul].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I32DivS].Cost = 80;
    defaultInstructionTable[Instruction::Enum::I32DivU].Cost = 80;
    defaultInstructionTable[Instruction::Enum::I32RemS].Cost = 80;
    defaultInstructionTable[Instruction::Enum::I32RemU].Cost = 80;
    defaultInstructionTable[Instruction::Enum::I32And].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I32Or].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I32Xor].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I32Shl].Cost = 2;
    defaultInstructionTable[Instruction::Enum::I32ShrS].Cost = 2;
    defaultInstructionTable[Instruction::Enum::I32ShrU].Cost = 2;
    defaultInstructionTable[Instruction::Enum::I32Rotl].Cost = 2;
    defaultInstructionTable[Instruction::Enum::I32Rotr].Cost = 2;
    defaultInstructionTable[Instruction::Enum::I32Eqz].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I32Eq].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I32Ne].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I32LtS].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I32LtU].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I32GtS].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I32GtU].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I32LeS].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I32LeU].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I32GeS].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I32GeU].Cost = 1;

    // 64-bit Integer operators
    defaultInstructionTable[Instruction::Enum::I64Clz].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I64Ctz].Cost = 6;
    defaultInstructionTable[Instruction::Enum::I64Popcnt].Cost = 3;

    defaultInstructionTable[Instruction::Enum::I64Add].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I64Sub].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I64Mul].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I64DivS].Cost = 80;
    defaultInstructionTable[Instruction::Enum::I64DivU].Cost = 80;
    defaultInstructionTable[Instruction::Enum::I64RemS].Cost = 80;
    defaultInstructionTable[Instruction::Enum::I64RemU].Cost = 80;
    defaultInstructionTable[Instruction::Enum::I64And].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I64Or].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I64Xor].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I64Shl].Cost = 2;
    defaultInstructionTable[Instruction::Enum::I64ShrS].Cost = 2;
    defaultInstructionTable[Instruction::Enum::I64ShrU].Cost = 2;
    defaultInstructionTable[Instruction::Enum::I64Rotl].Cost = 2;
    defaultInstructionTable[Instruction::Enum::I64Rotr].Cost = 2;
    defaultInstructionTable[Instruction::Enum::I64Eqz].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I64Eq].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I64Ne].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I64LtS].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I64LtU].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I64GtS].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I64GtU].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I64LeS].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I64LeU].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I64GeS].Cost = 1;
    defaultInstructionTable[Instruction::Enum::I64GeU].Cost = 1;

    // Datatype conversions
    defaultInstructionTable[Instruction::Enum::I32WrapI64].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I64ExtendI32S].Cost = 3;
    defaultInstructionTable[Instruction::Enum::I64ExtendI32U].Cost = 3;

    // Type-parametric operators
    defaultInstructionTable[Instruction::Enum::Drop].Cost = 3;
    defaultInstructionTable[Instruction::Enum::Select].Cost = 3;

    return defaultInstructionTable;
}

}  // namespace wasm
