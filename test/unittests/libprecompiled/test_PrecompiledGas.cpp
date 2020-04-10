/**
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
 *
 * @brief: unit test for PrecompiledGas
 *
 * @file test_PrecompiledGas.cpp
 * @author: yujiechen
 * @date 20200410
 */
#include <libprecompiled/PrecompiledGas.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev::precompiled;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(precompiledGasTest, TestOutputHelperFixture)

void checkGasCost(GasMetrics::Ptr _metric, InterfaceOpcode const& _key, int64_t const& _value)
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
    // disable FreeStorage
    auto precompiledGasFactory = std::make_shared<PrecompiledGasFactory>(0);
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

    // enable FreeStorage
    dev::VMFlagType evmFlags = 0;
    evmFlags |= EVMFlags::FreeStorageGas;
    precompiledGasFactory = std::make_shared<PrecompiledGasFactory>(evmFlags);
    BOOST_CHECK(precompiledGasFactory->gasMetric() != nullptr);
    metric = precompiledGasFactory->gasMetric();
    checkBasicGasCost(metric);
    checkGasCost(metric, InterfaceOpcode::CreateTable, 500);
    checkGasCost(metric, InterfaceOpcode::OpenTable, 200);
    checkGasCost(metric, InterfaceOpcode::Select, 200);
    checkGasCost(metric, InterfaceOpcode::Insert, 200);
    checkGasCost(metric, InterfaceOpcode::Update, 200);
    checkGasCost(metric, InterfaceOpcode::Remove, 200);
}

BOOST_AUTO_TEST_CASE(testPrecompiledGas)
{
    // disable freeStorage
    auto precompiledGasFactory = std::make_shared<PrecompiledGasFactory>(0);
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

    // enable freeStorage
    dev::VMFlagType evmFlags = 0;
    evmFlags |= EVMFlags::FreeStorageGas;
    precompiledGasFactory = std::make_shared<PrecompiledGasFactory>(evmFlags);
    gasPricer = precompiledGasFactory->createPrecompiledGas();

    gasPricer->appendOperation(InterfaceOpcode::CreateTable);
    BOOST_CHECK(gasPricer->calTotalGas() == 500);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    BOOST_CHECK(gasPricer->calTotalGas() == 700);
    gasPricer->appendOperation(InterfaceOpcode::Insert);
    BOOST_CHECK(gasPricer->calTotalGas() == 900);
    gasPricer->appendOperation(InterfaceOpcode::Select);
    BOOST_CHECK(gasPricer->calTotalGas() == 1100);
    gasPricer->appendOperation(InterfaceOpcode::Update);
    BOOST_CHECK(gasPricer->calTotalGas() == 1300);

    // test memory
    gasPricer->setMemUsed(256);
    BOOST_CHECK(gasPricer->calTotalGas() == 1324);
    gasPricer->updateMemUsed(25000);
    BOOST_CHECK(gasPricer->calTotalGas() == 4840);
    gasPricer->updateMemUsed(2500);
    BOOST_CHECK(gasPricer->calTotalGas() == 4840);

    gasPricer->appendOperation(InterfaceOpcode::LE);
    BOOST_CHECK(gasPricer->calTotalGas() == 4843);
    gasPricer->appendOperation(InterfaceOpcode::GroupSigVerify);
    BOOST_CHECK(gasPricer->calTotalGas() == 24843);
    gasPricer->appendOperation(InterfaceOpcode::PaillierAdd);
    BOOST_CHECK(gasPricer->calTotalGas() == 44843);
    gasPricer->appendOperation(InterfaceOpcode::RingSigVerify);
    BOOST_CHECK(gasPricer->calTotalGas() == 64843);

    // with bad instructions
    auto metric = precompiledGasFactory->gasMetric();
    metric->OpCode2GasCost.clear();
    gasPricer->appendOperation(InterfaceOpcode::LT);
    gasPricer->appendOperation(InterfaceOpcode::GetString);
    gasPricer->appendOperation(InterfaceOpcode::CreateTable);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    // only calculate the memory gas for bad instructions
    BOOST_CHECK(gasPricer->calTotalGas() == 3540);
    metric->init();
    gasPricer->appendOperation(InterfaceOpcode::CreateTable);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    BOOST_CHECK(gasPricer->calTotalGas() == 66249);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev