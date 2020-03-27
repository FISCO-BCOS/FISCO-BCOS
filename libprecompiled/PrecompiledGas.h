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
 * @file: PrecompiledGas.h
 * @author: yujiechen
 * @date: 2020-03-23
 */
#pragma once
#include <libdevcore/Common.h>

#define PrecompiledGas_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("LEDGER")

namespace dev
{
namespace precompiled
{
// opcode of corresponding method
enum InterfaceOpcode : int64_t
{
    EQ = 0x00,
    GE = 0x01,
    GT = 0x02,
    LE = 0x03,
    LT = 0x04,
    NE = 0x05,
    Limit = 0x06,
    GetInt = 0x07,
    GetAddr = 0x08,
    Set = 0x09,
    GetByte32 = 0X0a,
    GetByte64 = 0x0b,
    GetString = 0x0c,
    CreateTable = 0x0d,
    OpenTable = 0x0e,
    Select = 0x0f,
    Insert = 0x10,
    Update = 0x11,
    Remove = 0x12,
    PaillierAdd = 0x13,
    GroupSigVerify = 0x14,
    RingSigVerify = 0x15
};

struct GasMetrics
{
    // Measure the level of instruction gas
    static const int64_t Zero = 0;
    static const int64_t Base = 2;
    static const int64_t VeryLow = 3;
    static const int64_t Low = 5;
    static const int64_t Mid = 8;
    static const int64_t High = 10;
    // Every 256 bytes is a memory gas calculation unit
    static const unsigned MemGas = 3;
    static const unsigned MemUnitSize = 32;

    // opcode to gasCost mapping
    std::map<InterfaceOpcode, int64_t> OpCode2GasCost;

    using Ptr = std::shared_ptr<GasMetrics>;
    GasMetrics()
    {
        OpCode2GasCost = {{InterfaceOpcode::EQ, VeryLow}, {InterfaceOpcode::GE, VeryLow},
            {InterfaceOpcode::GT, VeryLow}, {InterfaceOpcode::LE, VeryLow},
            {InterfaceOpcode::LT, VeryLow}, {InterfaceOpcode::NE, VeryLow},
            {InterfaceOpcode::Limit, VeryLow}, {InterfaceOpcode::GetInt, VeryLow},
            {InterfaceOpcode::GetAddr, VeryLow}, {InterfaceOpcode::Set, VeryLow},
            {InterfaceOpcode::GetByte32, 32 * VeryLow}, {InterfaceOpcode::GetByte64, 64 * VeryLow},
            {InterfaceOpcode::GetString, 64 * VeryLow}, {InterfaceOpcode::CreateTable, High},
            {InterfaceOpcode::OpenTable, Low}, {InterfaceOpcode::Select, Low},
            {InterfaceOpcode::Insert, High}, {InterfaceOpcode::Update, High},
            {InterfaceOpcode::Remove, Mid}, {InterfaceOpcode::PaillierAdd, High},
            {InterfaceOpcode::GroupSigVerify, High}, {InterfaceOpcode::RingSigVerify, High}};
    }
    virtual ~GasMetrics() {}
};

// FreeStorageGasMetrics
struct FreeStorageGasMetrics : public GasMetrics
{
    FreeStorageGasMetrics() : GasMetrics() {}
    virtual ~FreeStorageGasMetrics() {}
};

class PrecompiledGas
{
public:
    using Ptr = std::shared_ptr<PrecompiledGas>;
    using OpListType = std::vector<std::pair<InterfaceOpcode, unsigned>>;

    PrecompiledGas() : m_operationList(std::make_shared<OpListType>()) {}
    virtual ~PrecompiledGas() {}

    virtual void appendOperation(InterfaceOpcode const& _opType, unsigned const& _opSize = 1);
    virtual u256 calTotalGas();
    void setMemUsed(uint64_t const& _memUsed) { m_memUsed = _memUsed; }
    uint64_t const& memUsed() const { return m_memUsed; }

    void updateMemUsed(uint64_t const& _newMemSize);
    void setGasMetric(GasMetrics::Ptr _metric) { m_metric = _metric; }

protected:
    // Traverse m_operationList to calculate total gas cost
    virtual u256 calComputationGas();
    // Calculating gas consumed by memory
    virtual u256 calMemGas();

private:
    std::shared_ptr<OpListType> m_operationList;
    GasMetrics::Ptr m_metric;
    uint64_t m_memUsed = 0;
};

class PrecompiledGasFactory
{
public:
    using Ptr = std::shared_ptr<PrecompiledGasFactory>;
    PrecompiledGasFactory(VMFlagType const& _vmFlags) { createMetric(_vmFlags); }
    virtual ~PrecompiledGasFactory() {}

    PrecompiledGas::Ptr createPrecompiledGas()
    {
        auto gasPricer = std::make_shared<PrecompiledGas>();
        gasPricer->setGasMetric(m_gasMetric);
        return gasPricer;
    }

private:
    void createMetric(VMFlagType const& _vmFlagType)
    {
        PrecompiledGas_LOG(DEBUG) << LOG_DESC("createGasMetric")
                                  << LOG_KV("vmFlagType", _vmFlagType);

        auto rightShiftLen = sizeof(VMFlagType) * 8 - 1;
        uint64_t metricFlag = (_vmFlagType >> rightShiftLen);
        // the FreeStorageGasMetrics enabled
        if (metricFlag)
        {
            PrecompiledGas_LOG(DEBUG) << LOG_DESC("createGasMetric: FreeStorageGasMetrics");
            m_gasMetric = std::make_shared<FreeStorageGasMetrics>();
            return;
        }
        m_gasMetric = std::make_shared<GasMetrics>();
    }

private:
    GasMetrics::Ptr m_gasMetric;
};
}  // namespace precompiled
}  // namespace dev