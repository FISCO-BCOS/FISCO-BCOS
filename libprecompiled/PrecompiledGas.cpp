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
 * (c) 2016-2020 fisco-dev contributors.
 */
/**
 * @brief : Implement precompiled contract gas calculation
 * @file: PrecompiledGas.cpp
 * @author: yujiechen
 * @date: 2020-03-23
 */
#include "PrecompiledGas.h"
#include <libdevcore/Log.h>
using namespace dev;
using namespace dev::precompiled;

std::map<InterfaceOpcode, int64_t> GasMetrics::OpCode2GasCost = {{InterfaceOpcode::EQ, VeryLow},
    {InterfaceOpcode::GE, VeryLow}, {InterfaceOpcode::GT, VeryLow}, {InterfaceOpcode::LE, VeryLow},
    {InterfaceOpcode::NE, VeryLow}, {InterfaceOpcode::Limit, VeryLow},
    {InterfaceOpcode::GetInt, VeryLow}, {InterfaceOpcode::GetAddr, VeryLow},
    {InterfaceOpcode::Set, VeryLow}, {InterfaceOpcode::GetByte32, 32 * VeryLow},
    {InterfaceOpcode::GetByte64, 64 * VeryLow}, {InterfaceOpcode::CreateTable, High},
    {InterfaceOpcode::OpenTable, Low}, {InterfaceOpcode::Insert, High},
    {InterfaceOpcode::Update, High}, {InterfaceOpcode::Delete, Mid}};

void PrecompiledGas::appendOperation(InterfaceOpcode const& _opType, unsigned const& _opSize)
{
    m_operationList->push_back(std::make_pair(_opType, _opSize));
}

u256 PrecompiledGas::calTotalGas(uint64_t const& _maxMemSize)
{
    return (calComputationGas() + calMemGas(_maxMemSize));
}


// Traverse m_operationList to calculate total gas cost
u256 PrecompiledGas::calComputationGas()
{
    u256 totalGas = 0;
    for (auto const& it : *m_operationList)
    {
        if (!GasMetrics::OpCode2GasCost.count(it.first))
        {
            PrecompiledGas_LOG(WARNING) << LOG_DESC("Invalid opType:") << it.first;
            continue;
        }
        totalGas += (GasMetrics::OpCode2GasCost[it.first]) * it.second;
    }
    return totalGas;
}

// Calculating gas consumed by memory
u256 PrecompiledGas::calMemGas(uint64_t const& _maxMemSize)
{
    auto memSize = (_maxMemSize + GasMetrics::MemUnitSize - 1) / GasMetrics::MemUnitSize;
    return (GasMetrics::MemGas * memSize) + (memSize * memSize) / 512;
}
