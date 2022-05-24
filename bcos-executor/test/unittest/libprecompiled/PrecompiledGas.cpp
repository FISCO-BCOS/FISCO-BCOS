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
 * @date 2021-06-03
 */

#include "bcos-executor/src/precompiled/common/PrecompiledGas.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(precompiledGasTest, TestPromptFixture)
void checkGasCost(GasMetrics::Ptr _metric, InterfaceOpcode const& _key, int64_t _value)
{
    BOOST_CHECK(_metric->OpCode2GasCost[_key] == _value);
}

void checkBasicGasCost(GasMetrics::Ptr _metric)
{
    checkGasCost(_metric, InterfaceOpcode::EQ, 3);
    checkGasCost(_metric, InterfaceOpcode::GE, 3);
    checkGasCost(_metric, InterfaceOpcode::GT, 3);
    checkGasCost(_metric, InterfaceOpcode::LE, 3);
    checkGasCost(_metric, InterfaceOpcode::LT, 3);
    checkGasCost(_metric, InterfaceOpcode::NE, 3);
    checkGasCost(_metric, InterfaceOpcode::Limit, 3);
    checkGasCost(_metric, InterfaceOpcode::GetInt, 3);
    checkGasCost(_metric, InterfaceOpcode::GetAddr, 3);
    checkGasCost(_metric, InterfaceOpcode::Set, 3);
    checkGasCost(_metric, InterfaceOpcode::GetByte32, 96);
    checkGasCost(_metric, InterfaceOpcode::GetByte64, 192);
    checkGasCost(_metric, InterfaceOpcode::GetString, 3);
    checkGasCost(_metric, InterfaceOpcode::PaillierAdd, 20000);
    checkGasCost(_metric, InterfaceOpcode::GroupSigVerify, 20000);
    checkGasCost(_metric, InterfaceOpcode::RingSigVerify, 20000);
}

BOOST_AUTO_TEST_CASE(testPrecompiledGasFactory)
{
    auto precompiledGasFactory = std::make_shared<PrecompiledGasFactory>();
    BOOST_CHECK(precompiledGasFactory->gasMetric() != nullptr);
    auto metric = precompiledGasFactory->gasMetric();
    // check gas cost
    checkBasicGasCost(metric);
    checkGasCost(metric, InterfaceOpcode::CreateTable, 16000);
    checkGasCost(metric, InterfaceOpcode::OpenTable, 200);
    checkGasCost(metric, InterfaceOpcode::Select, 200);
    checkGasCost(metric, InterfaceOpcode::Insert, 10000);
    checkGasCost(metric, InterfaceOpcode::Update, 10000);
    checkGasCost(metric, InterfaceOpcode::Remove, 2500);
}

BOOST_AUTO_TEST_CASE(testPrecompiledGas)
{
    // disable freeStorage
    auto precompiledGasFactory = std::make_shared<PrecompiledGasFactory>();
    auto gasPricer = precompiledGasFactory->createPrecompiledGas();
    gasPricer->appendOperation(InterfaceOpcode::CreateTable);
    BOOST_CHECK(gasPricer->calTotalGas() == 16000);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    BOOST_CHECK(gasPricer->calTotalGas() == 16200);
    gasPricer->appendOperation(InterfaceOpcode::Insert);
    BOOST_CHECK(gasPricer->calTotalGas() == 26200);
    gasPricer->appendOperation(InterfaceOpcode::Select);
    BOOST_CHECK(gasPricer->calTotalGas() == 26400);
    gasPricer->appendOperation(InterfaceOpcode::Update);
    BOOST_CHECK(gasPricer->calTotalGas() == 36400);
    gasPricer->setMemUsed(256);
    BOOST_CHECK(gasPricer->calTotalGas() == 36424);
    gasPricer->updateMemUsed(15000);
    BOOST_CHECK(gasPricer->calTotalGas() == 38236);
    gasPricer->appendOperation(InterfaceOpcode::LE);
    BOOST_CHECK(gasPricer->calTotalGas() == 38239);

    // with bad instructions
    auto metric = precompiledGasFactory->gasMetric();
    metric->OpCode2GasCost.clear();
    gasPricer->appendOperation(InterfaceOpcode::LT);
    gasPricer->appendOperation(InterfaceOpcode::GetString);
    // only calculate the memory gas for bad instructions
    BOOST_CHECK(gasPricer->calTotalGas() == 1836);
    metric->init();
    gasPricer->appendOperation(InterfaceOpcode::CreateTable);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    BOOST_CHECK(gasPricer->calTotalGas() == 54445);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos