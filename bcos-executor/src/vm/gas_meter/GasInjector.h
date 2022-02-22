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
/** @file GasInjector.h
 *  @author xingqiangbai
 *  @date 20200921
 */
#pragma once
#include "Metric.h"
#include <exception>
#include <memory>
#include <vector>

namespace wabt
{
class Expr;
template <typename T>
class intrusive_list;
typedef intrusive_list<Expr> ExprList;
}  // namespace wabt
namespace bcos
{
namespace wasm
{
const uint64_t WASM_MEMORY_PAGES_INIT = 16;
const uint64_t WASM_MEMORY_PAGES_MAX = 1024;
class GasInjector
{
public:
    class InvalidInstruction : std::exception
    {
    public:
        InvalidInstruction(const std::string& _opName, uint32_t _loc)
          : m_opName(_opName), m_location(_loc){};
        InvalidInstruction(const char* _opName, uint32_t _loc)
          : m_opName(_opName), m_location(_loc){};
        std::string ErrorMessage() const noexcept
        {
            return "Unsupported opcode " + m_opName + ", location:" + std::to_string(m_location);
        }

    private:
        std::string m_opName;
        uint32_t m_location;
    };
    enum Status
    {
        Success = 0,
        InvalidFormat = 1,
        ForbiddenOpcode = 2,
    };
    struct Result
    {
        Status status;
        std::shared_ptr<std::vector<uint8_t>> byteCode;
    };
    GasInjector(const InstructionTable costTable) : m_costTable(costTable) {}

    Result InjectMeter(const std::vector<uint8_t>& byteCode);

private:
    struct ImportsInfo
    {
        bool foundGasFunction = false;
        uint32_t gasFuncIndex;
        uint32_t globalGasIndex;
        uint32_t tempVarForMemoryGasIndex;
        uint32_t originSize;
    };
    void InjectMeterExprList(wabt::ExprList* exprs, const ImportsInfo& info);
    const InstructionTable m_costTable;
};
}  // namespace wasm
}  // namespace bcos
