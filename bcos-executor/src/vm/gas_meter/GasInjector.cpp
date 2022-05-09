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
/** @file GasInjector.cpp
 *  @author xingqiangbai
 *  @date 20200921
 */

#include "GasInjector.h"
#include "src/binary-reader-ir.h"
#include "src/binary-reader.h"
#include "src/binary-writer.h"
#include "src/cast.h"
#include "src/ir.h"
#include "src/stream.h"
#include <bcos-framework/interfaces/Common.h>
#include <iostream>

using namespace std;
using namespace wabt;

#define METER_LOG(LEVEL) BCOS_LOG(LEVEL) << "[METER]"

namespace bcos
{
namespace wasm
{
const char* const MODULE_NAME = "bcos";
const char* const OUT_OF_GAS_NAME = "outOfGas";
const char* const GLOBAL_GAS_NAME = "gas";

// write wasm will not use loc, so wrong loc doesn't matter
void GasInjector::InjectMeterExprList(ExprList* exprs, const ImportsInfo& info)
{
    auto insertPoint = exprs->begin();
    int64_t gasCost = 0;
    auto subAndCheck = [&](ExprList::iterator loc) {
        // sub
        auto sub = MakeUnique<BinaryExpr>(Opcode::I64Sub);
        exprs->insert(loc, std::move(sub));
        auto set = MakeUnique<GlobalSetExpr>(Var(info.globalGasIndex));
        exprs->insert(loc, std::move(set));
        // check if gas < 0, then call outOfGas
        auto getGas = MakeUnique<GlobalGetExpr>(Var(info.globalGasIndex));
        exprs->insert(loc, std::move(getGas));
        auto zero = MakeUnique<ConstExpr>(Const::I64(0));
        exprs->insert(loc, std::move(zero));
        auto i64le = MakeUnique<BinaryExpr>(Opcode::I64LeS);
        exprs->insert(loc, std::move(i64le));
        auto ifExpr = MakeUnique<IfExpr>();
        ifExpr->true_.decl.has_func_type = false;
        ifExpr->true_.decl.sig.param_types.clear();
        // ifExpr->true_.end_loc;
        auto voidType = Type(Type::Void);
        ifExpr->true_.decl.sig.result_types = voidType.GetInlineVector();
        auto outOfGas = MakeUnique<CallExpr>(Var(info.gasFuncIndex));
        ifExpr->true_.exprs.insert(ifExpr->true_.exprs.begin(), std::move(outOfGas));
        exprs->insert(loc, std::move(ifExpr));
    };
    auto insertUseGasLogic = [&](ExprList::iterator loc, int64_t& gas,
                                 ExprList::iterator current) -> ExprList::iterator {
        if (gas > 0)
        {
            auto getGas = MakeUnique<GlobalGetExpr>(Var(info.globalGasIndex));
            exprs->insert(loc, std::move(getGas));
            auto constGas = MakeUnique<ConstExpr>(Const::I64(gas));
            exprs->insert(loc, std::move(constGas));
            subAndCheck(loc);
            gas = 0;
        }
        return ++current;
    };
    auto isFloatOpcode = [](const Opcode& opcode) -> bool {
#ifdef WASM_FLOAT_ENABLE
        return false;
#endif
        uint32_t op = opcode.GetCode();
        if ((op >= 0x2A && op <= 0x2B) || (op >= 0x38 && op <= 0x39) ||
            (op >= 0x43 && op <= 0x44) || (op >= 0x5B && op <= 0x66) ||
            (op >= 0x8B && op <= 0xA6) || (op >= 0xB2 && op <= 0xBF))
        {
            METER_LOG(DEBUG) << LOG_BADGE("float instruction") << LOG_KV("name", opcode.GetName())
                             << LOG_KV("instruction", opcode.GetCode());
            return true;
        }
        return false;
    };
    for (auto it = exprs->begin(); it != exprs->end(); ++it)
    {
        switch (it->type())
        {
        case ExprType::Binary:
        {  // float op throw exception
            auto& op = cast<BinaryExpr>(&*it)->opcode;
            if (op.HasPrefix())
            {
                METER_LOG(WARNING) << LOG_BADGE("instruction has perfix");
            }
            if (isFloatOpcode(op))
            {
                METER_LOG(WARNING) << LOG_BADGE("invalid Binary instruction")
                                   << LOG_KV("instruction", op.GetName());
                throw InvalidInstruction(op.GetName(), ((Expr*)(&*it))->loc.offset);
            }
            gasCost += m_costTable[op.GetCode()].Cost;
            break;
        }
        case ExprType::Block:
        {
            auto& block = cast<BlockExpr>(&*it)->block;
            insertPoint = insertUseGasLogic(insertPoint, gasCost, it);
            InjectMeterExprList(&block.exprs, info);
            break;
        }
        case ExprType::Br:
        {
            gasCost += m_costTable[Instruction::Enum::Br].Cost;
            insertPoint = insertUseGasLogic(insertPoint, gasCost, it);
            break;
        }
        case ExprType::BrIf:
        {
            gasCost += m_costTable[Instruction::Enum::BrIf].Cost;
            insertPoint = insertUseGasLogic(insertPoint, gasCost, it);
            break;
        }
        case ExprType::BrTable:
        {
            gasCost += m_costTable[Instruction::Enum::BrTable].Cost;
            insertPoint = insertUseGasLogic(insertPoint, gasCost, it);
            break;
        }
        case ExprType::Call:
        {
            auto& var = cast<CallExpr>(&*it)->var;
            if (var.index() >= info.originSize && !info.foundGasFunction)
            {  // add outOfGas import, so update call func index
                var.set_index(var.index() + 1);
            }

            gasCost += m_costTable[Instruction::Enum::Call].Cost;
            insertPoint = insertUseGasLogic(insertPoint, gasCost, it);
            break;
        }
        case ExprType::CallIndirect:
        {
            gasCost += m_costTable[Instruction::Enum::CallIndirect].Cost;
            insertPoint = insertUseGasLogic(insertPoint, gasCost, it);
            break;
        }
        case ExprType::Compare:
        {  // float op throw exception
            auto& op = cast<CompareExpr>(&*it)->opcode;
            if (isFloatOpcode(op))
            {
                METER_LOG(WARNING) << LOG_BADGE("invalid Compare instruction")
                                   << LOG_KV("instruction", op.GetName());
                throw InvalidInstruction(op.GetName(), ((Expr*)(&*it))->loc.offset);
            }
            gasCost += m_costTable[op.GetCode()].Cost;
            break;
        }
        case ExprType::Const:
        {
            auto& constValue = cast<ConstExpr>(&*it)->const_;
            if (constValue.type() == Type::I32)
            {
                gasCost += m_costTable[Instruction::Enum::I32Const].Cost;
            }
            else if (constValue.type() == Type::I64)
            {
                gasCost += m_costTable[Instruction::Enum::I64Const].Cost;
            }
            else
            {
#ifndef WASM_FLOAT_ENABLE
                METER_LOG(WARNING) << LOG_BADGE("invalid Const instruction");
                throw InvalidInstruction(constValue.type() == Type::F32 ? "f32.const" : "f64.const",
                    ((Expr*)(&*it))->loc.offset);
#endif
            }

            break;
        }
        case ExprType::Convert:
        {  // float op throw exception
            auto& op = cast<ConvertExpr>(&*it)->opcode;
            if (isFloatOpcode(op))
            {
                METER_LOG(WARNING) << LOG_BADGE("invalid Convert instruction")
                                   << LOG_KV("instruction", op.GetName());
                throw InvalidInstruction(op.GetName(), ((Expr*)(&*it))->loc.offset);
            }
            gasCost += m_costTable[op.GetCode()].Cost;
            break;
        }
        case ExprType::Drop:
        {
            gasCost += m_costTable[Instruction::Enum::Drop].Cost;
            break;
        }
        case ExprType::GlobalGet:
        {
            auto& var = cast<GlobalGetExpr>(&*it)->var;
            // add globalGas import, so update global index
            var.set_index(var.index() + 1);
            gasCost += m_costTable[Instruction::Enum::GlobalGet].Cost;
            break;
        }
        case ExprType::GlobalSet:
        {
            auto& var = cast<GlobalSetExpr>(&*it)->var;
            // add globalGas import, so update global index
            var.set_index(var.index() + 1);
            gasCost += m_costTable[Instruction::Enum::GlobalSet].Cost;
            break;
        }
        case ExprType::If:
        {
            auto ifExpr = cast<IfExpr>(&*it);
            InjectMeterExprList(&ifExpr->true_.exprs, info);
            if (!ifExpr->false_.empty())
            {
                gasCost += m_costTable[Instruction::Enum::Else].Cost;
                InjectMeterExprList(&ifExpr->false_, info);
            }
            insertPoint = insertUseGasLogic(insertPoint, gasCost, it);
            break;
        }
        case ExprType::Load:
        {
            auto& op = cast<LoadExpr>(&*it)->opcode;
            gasCost += m_costTable[op.GetCode()].Cost;
            break;
        }
        case ExprType::LocalGet:
        {
            gasCost += m_costTable[Instruction::Enum::LocalGet].Cost;
            break;
        }
        case ExprType::LocalSet:
        {
            gasCost += m_costTable[Instruction::Enum::LocalSet].Cost;
            break;
        }
        case ExprType::LocalTee:
        {
            gasCost += m_costTable[Instruction::Enum::LocalTee].Cost;
            break;
        }
        case ExprType::Loop:
        {
            insertPoint = insertUseGasLogic(insertPoint, gasCost, it);
            auto& block = cast<LoopExpr>(&*it)->block;
            InjectMeterExprList(&block.exprs, info);
            break;
        }
        case ExprType::MemoryGrow:
        {
#if 0
// TODO: memoryGrow is not charged for now
            auto localTee = MakeUnique<LocalTeeExpr>(Var(info.tempVarForMemoryGasIndex));
            exprs->insert(it, std::move(localTee));
            auto getGas = MakeUnique<GlobalGetExpr>(Var(info.globalGasIndex));
            exprs->insert(it, std::move(getGas));
            auto localGet = MakeUnique<LocalGetExpr>(Var(info.tempVarForMemoryGasIndex));
            exprs->insert(it, std::move(localGet));
            auto constGas =
                MakeUnique<ConstExpr>(Const::I32(m_costTable[Instruction::Enum::MemoryGrow].Cost));
            exprs->insert(it, std::move(constGas));
            auto mul = MakeUnique<BinaryExpr>(Opcode::I32Mul);
            exprs->insert(it, std::move(mul));
            auto i64Convert = MakeUnique<ConvertExpr>(Opcode::I64ExtendI32S);
            exprs->insert(it, std::move(i64Convert));
            subAndCheck(it);
#endif
            break;
        }
        case ExprType::MemorySize:
        {
            gasCost += m_costTable[Instruction::Enum::MemorySize].Cost;
            break;
        }
        case ExprType::Nop:
        {
            gasCost += m_costTable[Instruction::Enum::Nop].Cost;
            break;
        }
        case ExprType::Return:
        {
            gasCost += m_costTable[Instruction::Enum::Return].Cost;
            insertPoint = insertUseGasLogic(insertPoint, gasCost, it);
            break;
        }
        case ExprType::Select:
        {
            gasCost += m_costTable[Instruction::Enum::Select].Cost;
            break;
        }
        case ExprType::Store:
        {
            auto& op = cast<StoreExpr>(&*it)->opcode;
            gasCost += m_costTable[op.GetCode()].Cost;
            break;
        }
        case ExprType::Unary:
        {
            auto& op = cast<UnaryExpr>(&*it)->opcode;
            if (isFloatOpcode(op))
            {
                METER_LOG(WARNING) << LOG_BADGE("invalid Unary instruction")
                                   << LOG_KV("instruction", op.GetName());
                throw InvalidInstruction(op.GetName(), ((Expr*)(&*it))->loc.offset);
            }
            gasCost += m_costTable[op.GetCode()].Cost;
            break;
        }
        case ExprType::Unreachable:
        {
            gasCost += m_costTable[Instruction::Enum::Unreachable].Cost;
            break;
        }
        case ExprType::AtomicLoad:
        case ExprType::AtomicRmw:
        case ExprType::AtomicRmwCmpxchg:
        case ExprType::AtomicStore:
        case ExprType::AtomicNotify:
        case ExprType::AtomicFence:
        case ExprType::AtomicWait:
        case ExprType::MemoryCopy:
        case ExprType::DataDrop:
        case ExprType::MemoryFill:
        case ExprType::MemoryInit:
        case ExprType::RefIsNull:
        case ExprType::RefFunc:
        case ExprType::RefNull:
        case ExprType::Rethrow:
        case ExprType::ReturnCall:
        case ExprType::ReturnCallIndirect:
        // case ExprType::SimdLaneOp:
        // case ExprType::SimdShuffleOp:
        case ExprType::LoadSplat:
        case ExprType::TableCopy:
        case ExprType::ElemDrop:
        case ExprType::TableInit:
        case ExprType::TableGet:
        case ExprType::TableGrow:
        case ExprType::TableSize:
        case ExprType::TableSet:
        case ExprType::TableFill:
        case ExprType::Ternary:  // v128.bitselect
        case ExprType::Throw:
        case ExprType::Try:
        default:
        {
            METER_LOG(WARNING) << LOG_BADGE("unsupported instruction in MVP");
            throw InvalidInstruction(
                m_costTable[Instruction::Enum::Unreachable].Name, ((Expr*)(&*it))->loc.offset);
            break;
        }
        }
    }
    if (gasCost != 0)
    {  // should not happen
        insertUseGasLogic(insertPoint, gasCost, insertPoint);
    }
}

GasInjector::Result GasInjector::InjectMeter(const std::vector<uint8_t>& byteCode)
{
    GasInjector::Result injectResult;
    // parse wasm use wabt
    Errors errors;
    Module module;
    bool s_read_debug_names = false;
    const bool kStopOnFirstError = true;
    const bool kFailOnCustomSectionError = true;
    ReadBinaryOptions options(
        Features(), nullptr, s_read_debug_names, kStopOnFirstError, kFailOnCustomSectionError);
    auto result = ReadBinaryIr("", byteCode.data(), byteCode.size(), options, &errors, &module);
    if (!Succeeded(result))
    {
        injectResult.status = Status::InvalidFormat;
        return injectResult;
    }
#if 0
        if (Succeeded(result)) {
        ValidateOptions options(s_features);
        result = ValidateModule(&module, &errors, options);
        result |= GenerateNames(&module);
      }

      if (Succeeded(result)) {
        /* TODO(binji): This shouldn't fail; if a name can't be applied
         * (because the index is invalid, say) it should just be skipped. */
        Result dummy_result = ApplyNames(&module);
        WABT_USE(dummy_result);
      }
#endif
    // check if import outOfGas function
    Index outOfGasIndex = 0;
    bool foundGasFunction = false;
    uint32_t originImportSize = module.imports.size();
    for (size_t i = 0; i < module.imports.size(); ++i)
    {
        const Import* import = module.imports[i];
        if (import->kind() == ExternalKind::Func && import->module_name == MODULE_NAME &&
            import->field_name == OUT_OF_GAS_NAME)
        {
            foundGasFunction = true;
            outOfGasIndex = i;
        }
    }
    if (!foundGasFunction)
    {  // import outOfGas
        TypeVector params{};
        TypeVector result;
        FuncSignature outOfGasSignature{params, result};
        Index sig_index = 0;
        bool foundUseGasSignature = false;
        for (size_t i = 0; i < module.types.size(); ++i)
        {
            if (module.types[i]->kind() == TypeEntryKind::Func)
            {
                const FuncType* func = cast<FuncType>(module.types[i]);
                if (func->sig == outOfGasSignature)
                {
                    foundUseGasSignature = true;
                    sig_index = i;
                }
            }
        }
        if (!foundUseGasSignature)
        {  // insert outOfGas type BinaryReaderIR::OnFuncType
            auto field = MakeUnique<TypeModuleField>();
            auto func_type = MakeUnique<FuncType>();
            func_type->sig.param_types = params;
            func_type->sig.result_types = result;
            field->type = std::move(func_type);
            module.AppendField(std::move(field));
            sig_index = module.types.size() - 1;
        }
        // add outOfGas import
        // BinaryReaderIR::OnImportFunc
        auto import = MakeUnique<FuncImport>(OUT_OF_GAS_NAME);
        import->module_name = MODULE_NAME;
        import->field_name = OUT_OF_GAS_NAME;
        // import->func.decl.has_func_type = false;
        import->func.decl.type_var = Var(sig_index);
        import->func.decl.sig = outOfGasSignature;
        // Module::AppendField,
        // module.AppendField(MakeUnique<ImportModuleField>(std::move(import)));
        module.func_bindings.emplace(import->func.name, Binding(Location(), module.funcs.size()));
        module.funcs.insert(module.funcs.begin(), &import->func);
        ++module.num_func_imports;
        module.imports.push_back(import.get());
        module.fields.push_back(MakeUnique<ImportModuleField>(std::move(import)));
        outOfGasIndex = originImportSize;
        for (Export* exportItem : module.exports)
        {
            if (exportItem->kind == ExternalKind::Func)
            {
                exportItem->var.set_index(exportItem->var.index() + 1);
            }
            // cout << "Export:" << exportItem->name << ", type:" << (int)exportItem->kind
            //      << ", index:" << exportItem->var.index() << endl;
        }
        for (ElemSegment* elem : module.elem_segments)
        {  // ElemSegment has func indexes, so update it
            for (auto& expr : elem->elem_exprs)
            {
                if (expr.var.index() >= outOfGasIndex)
                {
                    expr.var.set_index(expr.var.index() + 1);
                }
            }
        }
    }

    // add global var gas
    auto globalGas = MakeUnique<GlobalImport>(GLOBAL_GAS_NAME);
    globalGas->module_name = MODULE_NAME;
    globalGas->field_name = GLOBAL_GAS_NAME;
    globalGas->global.name = GLOBAL_GAS_NAME;
    globalGas->global.type = wabt::Type::I64;
    globalGas->global.mutable_ = true;
    auto zero = MakeUnique<ConstExpr>(Const::I64(0));
    globalGas->global.init_expr.push_back(std::move(zero));
    module.globals.insert(module.globals.begin(), &globalGas->global);
    ++module.num_global_imports;
    module.imports.push_back(globalGas.get());
    module.fields.push_back(MakeUnique<ImportModuleField>(std::move(globalGas)));
    // module.AppendField(MakeUnique<ImportModuleField>(std::move(globalGas)));

    // set memory limit
    auto memory = module.memories[0];
    if (memory->page_limits.initial < WASM_MEMORY_PAGES_INIT)
    {
        memory->page_limits.initial = WASM_MEMORY_PAGES_INIT;
    }
    memory->page_limits.max = WASM_MEMORY_PAGES_MAX;
    memory->page_limits.has_max = true;
    try
    {
        ImportsInfo info{foundGasFunction, outOfGasIndex, 0, 0, originImportSize};
        // FIXME: main and deploy of wasm should charge memory gas first
        for (Func* func : module.funcs)
        {  // scan opcode and add meter logic
            if (func->exprs.empty())
            {
                continue;
            }
            Index tempVarIndex = func->GetNumParamsAndLocals();
            func->local_types.AppendDecl(Type::Enum::I32, 1);
            info.tempVarForMemoryGasIndex = tempVarIndex;
            // cout << "Func:" << func->name << ", type index:" << func->decl.type_var.index()
            //      << ", expr size:" << func->exprs.size() << ",info.tempVarForMemoryGasIndex:" <<
            //      info.tempVarForMemoryGasIndex
            //      << "/" << func->local_types.size() << endl;
            InjectMeterExprList(&func->exprs, info);
        }
    }
    catch (const InvalidInstruction& e)
    {
        injectResult.status = Status::ForbiddenOpcode;
        METER_LOG(WARNING) << LOG_BADGE("InjectMeter failed, because of invalid instruction")
                           << LOG_KV("message", e.ErrorMessage());
        return injectResult;
    }

    WriteBinaryOptions writeOptions;
    writeOptions.relocatable = false;
    // writeOptions.canonicalize_lebs = false;
#if FISCO_DEBUG
    FileStream log("wasm.log");
    MemoryStream memoryStream(&log);
#else
    MemoryStream memoryStream;
#endif
    WriteBinaryModule((wabt::Stream*)&memoryStream, &module, writeOptions);

    // return result
    injectResult.status = Status::Success;
    auto resultBytes = make_shared<vector<uint8_t>>();
    resultBytes->swap(memoryStream.output_buffer().data);
    injectResult.byteCode = resultBytes;
    return injectResult;
}


}  // namespace wasm
}  // namespace bcos
