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
 *
 * @brief c++ wrapper of vm
 * @file VMInstance.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#pragma once
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>

namespace bcos::transaction_executor
{
class HostContext;

struct VMSchedule
{
    VMSchedule() : tierStepGas(std::array<unsigned, 8>{{0, 2, 3, 5, 8, 10, 20, 0}}) {}
    VMSchedule(bool _efcd, bool _hdc, unsigned const& _txCreateGas)
      : tierStepGas(std::array<unsigned, 8>{{0, 2, 3, 5, 8, 10, 20, 0}}),
        exceptionalFailedCodeDeposit(_efcd),
        haveDelegateCall(_hdc),
        txCreateGas(_txCreateGas)
    {}

    std::array<unsigned, 8> tierStepGas;
    bool exceptionalFailedCodeDeposit = true;
    bool haveDelegateCall = false;
    constexpr static bool eip150Mode = true;
    constexpr static bool eip158Mode = true;
    constexpr static bool haveBitwiseShifting = false;
    constexpr static bool haveRevert = true;
    constexpr static bool haveReturnData = true;
    constexpr static bool haveStaticCall = true;
    constexpr static bool haveCreate2 = true;
    constexpr static bool haveExtcodehash = false;
    constexpr static bool enableIstanbul = false;
    constexpr static bool enableLondon = false;
    /// gas cost for specified calculation
    /// exp gas cost
    constexpr static unsigned expGas = 10;
    constexpr static unsigned expByteGas = 10;
    /// sha3 gas cost
    constexpr static unsigned sha3Gas = 30;
    constexpr static unsigned sha3WordGas = 6;
    /// load/store gas cost
    constexpr static unsigned sloadGas = 50;
    constexpr static unsigned sstoreSetGas = 20000;
    constexpr static unsigned sstoreResetGas = 5000;
    constexpr static unsigned sstoreRefundGas = 15000;
    /// jump gas cost
    constexpr static unsigned jumpdestGas = 1;
    /// log gas cost
    constexpr static unsigned logGas = 375;
    constexpr static unsigned logDataGas = 8;
    constexpr static unsigned logTopicGas = 375;
    /// creat contract gas cost
    constexpr static unsigned createGas = 32000;
    /// call function of contract gas cost
    constexpr static unsigned callGas = 40;
    constexpr static unsigned callStipend = 2300;
    constexpr static unsigned callValueTransferGas = 9000;
    constexpr static unsigned callNewAccountGas = 25000;

    constexpr static unsigned suicideRefundGas = 24000;
    constexpr static unsigned memoryGas = 3;
    constexpr static unsigned quadCoeffDiv = 512;
    constexpr static unsigned createDataGas = 20;
    /// transaction related gas
    constexpr static unsigned txGas = 21000;
    unsigned txCreateGas = 53000;
    constexpr static unsigned txDataZeroGas = 4;
    constexpr static unsigned txDataNonZeroGas = 68;
    constexpr static unsigned copyGas = 3;
    /// extra code related gas
    constexpr static unsigned extcodesizeGas = 20;
    constexpr static unsigned extcodecopyGas = 20;
    constexpr static unsigned extcodehashGas = 400;
    constexpr static unsigned balanceGas = 20;
    constexpr static unsigned suicideGas = 0;
    constexpr static unsigned blockhashGas = 20;
    unsigned maxCodeSize = unsigned(-1);

    // boost::optional<u256> blockRewardOverwrite;

    static bool staticCallDepthLimit() { return !eip150Mode; }
    static bool suicideChargesNewAccountGas() { return eip150Mode; }
    static bool emptinessIsNonexistence() { return eip158Mode; }
    static bool zeroValueTransferChargesNewAccountGas() { return !eip158Mode; }
};

class Result : public evmc_result
{
public:
    explicit Result(evmc_result const& _result) : evmc_result(_result) {}

    ~Result()
    {
        if (release != nullptr)
        {
            release(this);
        }
    }

    Result(Result&& _other) noexcept : evmc_result(_other) { _other.release = nullptr; }

    Result& operator=(Result&&) = delete;
    Result(Result const&) = delete;
    Result& operator=(Result const&) = delete;

    evmc_status_code status() const { return status_code; }
    int64_t gasLeft() const { return gas_left; }
    bytesConstRef output() const { return {output_data, output_size}; }
};


static std::vector<std::pair<std::string, std::string>> s_evmcOptions;
/// Returns the EVM-C options parsed from command line.
std::vector<std::pair<std::string, std::string>>& evmcOptions() noexcept
{
    return s_evmcOptions;
}

/// Translate the VMSchedule to VMInstance-C revision.
evmc_revision toRevision(VMSchedule const& _schedule)
{
    if (_schedule.enableLondon)
        return EVMC_LONDON;
    if (_schedule.enableIstanbul)
        return EVMC_ISTANBUL;
    if (_schedule.haveCreate2)
        return EVMC_CONSTANTINOPLE;
    if (_schedule.haveRevert)
        return EVMC_BYZANTIUM;
    if (_schedule.eip158Mode)
        return EVMC_SPURIOUS_DRAGON;
    if (_schedule.eip150Mode)
        return EVMC_TANGERINE_WHISTLE;
    if (_schedule.haveDelegateCall)
        return EVMC_HOMESTEAD;
    return EVMC_FRONTIER;
}

/// The RAII wrapper for an VMInstance-C instance.
class VMInstance
{
public:
    explicit VMInstance(evmc_vm* _instance) noexcept : m_instance(_instance)
    {
        assert(m_instance != nullptr);
        // the abi_version of intepreter is EVMC_ABI_VERSION when callback VMFactory::create()
        assert(m_instance->abi_version == EVMC_ABI_VERSION);

        // Set the options.
        if (m_instance->set_option != nullptr)
        {
            for (auto& pair : evmcOptions())
            {
                m_instance->set_option(m_instance, pair.first.c_str(), pair.second.c_str());
            }
        }
    }

    ~VMInstance() { m_instance->destroy(m_instance); }

    VMInstance(VMInstance const&) = delete;
    VMInstance& operator=(VMInstance) = delete;

    Result exec(HostContext& _hostContext, evmc_revision _rev, evmc_message* _msg,
        const uint8_t* _code, size_t _code_size)
    {
        Result result = Result(m_instance->execute(
            m_instance, hostContext.interface, &hostContext, _rev, _msg, _code, _code_size));
        return result;
    }

    void enableDebugOutput() {}

private:
    /// The VM instance created with VMInstance-C <prefix>_create() function.
    evmc_vm* m_instance = nullptr;
};

}  // namespace bcos::transaction_executor
