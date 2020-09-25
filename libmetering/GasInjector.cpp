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
/** @file GasInjector.cpp
 *  @author xingqiangbai
 *  @date 20200921
 */

#include "GasInjector.h"
#include "libdevcore/Log.h"
#include "src/binary-reader-ir.h"
#include "src/binary-reader.h"
#include "src/binary-writer.h"
#include "src/cast.h"
#include "src/ir.h"
#include "src/stream.h"
#include <iostream>

using namespace std;
using namespace wabt;

#define METER_LOG(LEVEL) LOG(LEVEL) << "[METER]"

namespace wasm
{
const char* const MODULE_NAME = "bcos";
const char* const USE_GAS_NAME = "useGas";

// write wasm will not use loc, so wrong loc doesn't matter
void GasInjector::InjectMeterExprList(
    ExprList* exprs, Index funcIndex, Index tmpVarIndex, bool foundGasFunction)
{
    auto insertPoint = exprs->begin();
    int64_t gasCost = 0;
    auto insertUseGasCall = [funcIndex, exprs](ExprList::iterator insertPoint, int64_t gas,
                                ExprList::iterator current) -> ExprList::iterator {
        if (gas > 0)
        {
            auto constGas = MakeUnique<ConstExpr>(Const::I64(gas));
            exprs->insert(insertPoint, std::move(constGas));
            auto gas_expr = MakeUnique<CallExpr>(Var(funcIndex));
            exprs->insert(insertPoint, std::move(gas_expr));
        }
        return ++current;
    };
    auto isFloatOpcode = [](const Opcode& opcode) -> bool {
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
            insertPoint = insertUseGasCall(insertPoint, gasCost, it);
            gasCost = 0;
            InjectMeterExprList(&block.exprs, funcIndex, tmpVarIndex, foundGasFunction);
            break;
        }
        case ExprType::Br:
        {
            gasCost += m_costTable[Instruction::Enum::Br].Cost;
            insertPoint = insertUseGasCall(insertPoint, gasCost, it);
            gasCost = 0;
            break;
        }
        case ExprType::BrIf:
        {
            gasCost += m_costTable[Instruction::Enum::BrIf].Cost;
            insertPoint = insertUseGasCall(insertPoint, gasCost, it);
            gasCost = 0;
            break;
        }
        case ExprType::BrTable:
        {
            gasCost += m_costTable[Instruction::Enum::BrTable].Cost;
            insertPoint = insertUseGasCall(insertPoint, gasCost, it);
            gasCost = 0;
            break;
        }
        case ExprType::Call:
        {
            auto& var = cast<CallExpr>(&*it)->var;
            if (var.index() >= funcIndex && !foundGasFunction)
            {  // add useGas import, so update call func index
                var.set_index(var.index() + 1);
            }
            gasCost += m_costTable[Instruction::Enum::Call].Cost;
            insertPoint = insertUseGasCall(insertPoint, gasCost, it);
            gasCost = 0;
            break;
        }
        case ExprType::CallIndirect:
        {
            gasCost += m_costTable[Instruction::Enum::CallIndirect].Cost;
            insertPoint = insertUseGasCall(insertPoint, gasCost, it);
            gasCost = 0;
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
                METER_LOG(WARNING) << LOG_BADGE("invalid Const instruction");
                throw InvalidInstruction(constValue.type() == Type::F32 ? "f32.const" : "f64.const",
                    ((Expr*)(&*it))->loc.offset);
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
            gasCost += m_costTable[Instruction::Enum::GlobalGet].Cost;
            break;
        }
        case ExprType::GlobalSet:
        {
            gasCost += m_costTable[Instruction::Enum::GlobalSet].Cost;
            break;
        }
        case ExprType::If:
        {
            auto ifExpr = cast<IfExpr>(&*it);
            InjectMeterExprList(&ifExpr->true_.exprs, funcIndex, tmpVarIndex, foundGasFunction);
            if (!ifExpr->false_.empty())
            {
                gasCost += m_costTable[Instruction::Enum::Else].Cost;
                InjectMeterExprList(&ifExpr->false_, funcIndex, tmpVarIndex, foundGasFunction);
            }
            insertPoint = insertUseGasCall(insertPoint, gasCost, it);
            gasCost = 0;
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
            auto& block = cast<LoopExpr>(&*it)->block;
            insertPoint = insertUseGasCall(insertPoint, gasCost, it);
            gasCost = 0;
            InjectMeterExprList(&block.exprs, funcIndex, tmpVarIndex, foundGasFunction);
            break;
        }
        case ExprType::MemoryGrow:
        {
            auto localTee = MakeUnique<LocalTeeExpr>(Var(tmpVarIndex));
            exprs->insert(it, std::move(localTee));
            auto localGet = MakeUnique<LocalGetExpr>(Var(tmpVarIndex));
            exprs->insert(it, std::move(localGet));
            auto constGas =
                MakeUnique<ConstExpr>(Const::I32(m_costTable[Instruction::Enum::MemoryGrow].Cost));
            exprs->insert(it, std::move(constGas));
            auto mul = MakeUnique<BinaryExpr>(Opcode::I32Mul);
            exprs->insert(it, std::move(mul));
            auto i64Convert = MakeUnique<ConvertExpr>(Opcode::I64ExtendI32S);
            exprs->insert(it, std::move(i64Convert));
            auto gas_expr = MakeUnique<CallExpr>(Var(funcIndex));
            exprs->insert(it, std::move(gas_expr));
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
            insertPoint = insertUseGasCall(insertPoint, gasCost, it);
            gasCost = 0;
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
        case ExprType::BrOnExn:
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
    {
        auto constGas = MakeUnique<ConstExpr>(Const::I64(gasCost));
        exprs->insert(insertPoint, std::move(constGas));
        auto gas_expr = MakeUnique<CallExpr>(Var(funcIndex));
        exprs->insert(insertPoint, std::move(gas_expr));
        gasCost = 0;
    }
}

GasInjector::Result GasInjector::InjectMeter(std::vector<uint8_t> byteCode)
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
    // check if import useGas function
    Index useGasIndex = 0;
    bool foundGasFunction = false;
    for (size_t i = 0; i < module.imports.size(); ++i)
    {
        const Import* import = module.imports[i];
        if (import->kind() == ExternalKind::Func && import->module_name == "bcos" &&
            import->field_name == "useGas")
        {
            foundGasFunction = true;
            useGasIndex = i;
        }
    }
    if (!foundGasFunction)
    {  // import useGas
        TypeVector params{Type::I64};
        TypeVector result;
        FuncSignature useGasSignature{params, result};
        Index sig_index = 0;
        bool foundUseGasSignature = false;
        for (size_t i = 0; i < module.types.size(); ++i)
        {
            if (module.types[i]->kind() == TypeEntryKind::Func)
            {
                const FuncType* func = cast<FuncType>(module.types[i]);
                if (func->sig == useGasSignature)
                {
                    foundUseGasSignature = true;
                    sig_index = i;
                }
            }
        }
        if (!foundUseGasSignature)
        {  // insert useGas type BinaryReaderIR::OnFuncType
            auto field = MakeUnique<TypeModuleField>();
            auto func_type = MakeUnique<FuncType>();
            func_type->sig.param_types = params;
            func_type->sig.result_types = result;
            field->type = std::move(func_type);
            module.AppendField(std::move(field));
            sig_index = module.types.size() - 1;
        }
        {
            // BinaryReaderIR::OnImportFunc
            auto import = MakeUnique<FuncImport>(USE_GAS_NAME);
            import->module_name = MODULE_NAME;
            import->field_name = USE_GAS_NAME;
            // import->func.decl.has_func_type = false;
            import->func.decl.type_var = Var(sig_index);
            import->func.decl.sig = useGasSignature;
            // Module::AppendField,
            // module.AppendField(MakeUnique<ImportModuleField>(std::move(import)));
            module.func_bindings.emplace(
                import->func.name, Binding(Location(), module.funcs.size()));
            module.funcs.insert(module.funcs.begin(), &import->func);
            ++module.num_func_imports;
            module.imports.push_back(import.get());
            module.fields.push_back(MakeUnique<ImportModuleField>(std::move(import)));
        }

        useGasIndex = module.imports.size() - 1;
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
                if (expr.var.index() >= useGasIndex)
                {
                    expr.var.set_index(expr.var.index() + 1);
                }
            }
        }
    }
    try
    {
        // FIXME: main and deploy of wasm should charge memeory gas first
        for (Func* func : module.funcs)
        {  // scan opcode and add meter logic
            if (func->exprs.empty())
            {
                continue;
            }
            Index tmpVarIndex = func->GetNumParamsAndLocals();
            func->local_types.AppendDecl(Type::Enum::I32, 1);
            // cout << "Func:" << func->name << ", type index:" << func->decl.type_var.index()
            //      << ", expr size:" << func->exprs.size() << ",tmpVarIndex:" << tmpVarIndex
            //      << "/" << func->local_types.size() << endl;
            InjectMeterExprList(&func->exprs, useGasIndex, tmpVarIndex, foundGasFunction);
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