/**
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
 * @file PrecompiledGas.cpp
 * @author: kyonRay
 * @date 2021-05-25
 */

#include "PrecompiledGas.h"
#include "bcos-executor/src/precompiled/common/Common.h"
using namespace bcos;
using namespace bcos::precompiled;

const int64_t GasMetrics::Zero = 0;
const int64_t GasMetrics::Base = 2;
const int64_t GasMetrics::VeryLow = 3;
const int64_t GasMetrics::Low = 5;
const int64_t GasMetrics::Mid = 8;
const int64_t GasMetrics::High = 10;
// Every 256 bytes is a memory gas calculation unit
const unsigned GasMetrics::MemGas = 3;
const unsigned GasMetrics::MemUnitSize = 32;

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

int64_t PrecompiledGas::calTotalGas()
{
    return (calComputationGas() + calMemGas());
}


// Traverse m_operationList to calculate total gas cost
int64_t PrecompiledGas::calComputationGas()
{
    int64_t totalGas = 0;
    for (auto const& it : *m_operationList)
    {
        if (!m_metric->OpCode2GasCost.count(it.first))
        {
            PRECOMPILED_LOG(INFO) << LOG_DESC("Invalid opType:") << it.first;
            continue;
        }
        totalGas += ((m_metric->OpCode2GasCost)[it.first]) * it.second;
    }
    return totalGas;
}

// Calculating gas consumed by memory
int64_t PrecompiledGas::calMemGas()
{
    if (m_memUsed == 0)
    {
        return 0;
    }
    auto memSize = (m_memUsed + GasMetrics::MemUnitSize - 1) / GasMetrics::MemUnitSize;
    return (GasMetrics::MemGas * memSize) + (memSize * memSize) / 512;
}
