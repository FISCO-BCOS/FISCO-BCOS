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

namespace wasm
{
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

    Result InjectMeter(std::vector<uint8_t> byteCode);

private:
    void InjectMeterExprList(
        wabt::ExprList* exprs, uint32_t funcIndex, uint32_t tmpVarIndex, bool foundGasFunction);
    const InstructionTable m_costTable;
};

}  // namespace wasm
