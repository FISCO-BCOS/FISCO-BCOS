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
void PrecompiledGas::appendOperation(InterfaceOpcode const& _opType, unsigned const& _opSize)
{
    m_operationList->push_back(std::make_pair(_opType, _opSize));
}

void PrecompiledGas::updateMemUsed(uint64_t const& _newMemSize)
{
    if (_newMemSize > m_memUsed)
    {
        m_memUsed = _newMemSize;
    }
}

u256 PrecompiledGas::calTotalGas()
{
    return (calComputationGas() + calMemGas());
}


// Traverse m_operationList to calculate total gas cost
u256 PrecompiledGas::calComputationGas()
{
    u256 totalGas = 0;
    for (auto const& it : *m_operationList)
    {
        if (!m_metric->OpCode2GasCost.count(it.first))
        {
            PrecompiledGas_LOG(WARNING) << LOG_DESC("Invalid opType:") << it.first;
            continue;
        }
        totalGas += ((m_metric->OpCode2GasCost)[it.first]) * it.second;
    }
    return totalGas;
}

// Calculating gas consumed by memory
u256 PrecompiledGas::calMemGas()
{
    if (m_memUsed == 0)
    {
        return 0;
    }
    auto memSize = (m_memUsed + GasMetrics::MemUnitSize - 1) / GasMetrics::MemUnitSize;
    return (GasMetrics::MemGas * memSize) + (memSize * memSize) / 512;
}

const int64_t GasMetrics::Zero = 0;
const int64_t GasMetrics::Base = 2;
const int64_t GasMetrics::VeryLow = 3;
const int64_t GasMetrics::Low = 5;
const int64_t GasMetrics::Mid = 8;
const int64_t GasMetrics::High = 10;
// Every 256 bytes is a memory gas calculation unit
const unsigned GasMetrics::MemGas = 3;
const unsigned GasMetrics::MemUnitSize = 32;


void PrecompiledGasFactory::createMetric(VMFlagType const& _vmFlagType)
{
    PrecompiledGas_LOG(INFO) << LOG_DESC("createGasMetric") << LOG_KV("vmFlagType", _vmFlagType);
    // the FreeStorageGasMetrics enabled
    if (enableFreeStorage(_vmFlagType))
    {
        PrecompiledGas_LOG(INFO) << LOG_DESC("createGasMetric: FreeStorageGasMetrics");
        m_gasMetric = std::make_shared<FreeStorageGasMetrics>();
    }
    else
    {
        PrecompiledGas_LOG(INFO) << LOG_DESC("createGasMetric");
        m_gasMetric = std::make_shared<GasMetrics>();
    }
    m_gasMetric->init();
}