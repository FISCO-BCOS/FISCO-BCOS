/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file EVMSchedule.h
 * @author wheatli
 * @date 2018.8.28
 * @brief:
 */

#pragma once

#include <libdevcore/Common.h>
#include <boost/optional.hpp>
#include <array>

namespace dev
{
namespace eth
{
struct EVMSchedule
{
    EVMSchedule() : tierStepGas(std::array<unsigned, 8>{{0, 2, 3, 5, 8, 10, 20, 0}}) {}
    EVMSchedule(bool _efcd, bool _hdc, unsigned const& _txCreateGas)
      : tierStepGas(std::array<unsigned, 8>{{0, 2, 3, 5, 8, 10, 20, 0}}),
        exceptionalFailedCodeDeposit(_efcd),
        haveDelegateCall(_hdc),
        txCreateGas(_txCreateGas)
    {}
    std::array<unsigned, 8> tierStepGas;
    bool exceptionalFailedCodeDeposit = true;
    bool haveDelegateCall = true;
    bool eip150Mode = false;
    bool eip158Mode = false;
    bool haveBitwiseShifting = false;
    bool haveRevert = false;
    bool haveReturnData = false;
    bool haveStaticCall = false;
    bool haveCreate2 = false;
    bool haveExtcodehash = false;
    bool enableIstanbul = false;
    /// gas cost for specified calculation
    /// exp gas cost
    unsigned expGas = 10;
    unsigned expByteGas = 10;
    /// sha3 gas cost
    unsigned sha3Gas = 30;
    unsigned sha3WordGas = 6;
    /// load/store gas cost
    unsigned sloadGas = 50;
    unsigned sstoreSetGas = 20000;
    unsigned sstoreResetGas = 5000;
    unsigned sstoreRefundGas = 15000;
    /// jump gas cost
    unsigned jumpdestGas = 1;
    /// log gas cost
    unsigned logGas = 375;
    unsigned logDataGas = 8;
    unsigned logTopicGas = 375;
    /// creat contract gas cost
    unsigned createGas = 32000;
    /// call function of contract gas cost
    unsigned callGas = 40;
    unsigned callStipend = 2300;
    unsigned callValueTransferGas = 9000;
    unsigned callNewAccountGas = 25000;

    unsigned suicideRefundGas = 24000;
    unsigned memoryGas = 3;
    unsigned quadCoeffDiv = 512;
    unsigned createDataGas = 200;
    /// transaction related gas
    unsigned txGas = 21000;
    unsigned txCreateGas = 53000;
    unsigned txDataZeroGas = 4;
    unsigned txDataNonZeroGas = 68;
    unsigned copyGas = 3;
    /// extra code related gas
    unsigned extcodesizeGas = 20;
    unsigned extcodecopyGas = 20;
    unsigned extcodehashGas = 400;
    unsigned balanceGas = 20;
    unsigned suicideGas = 0;
    unsigned blockhashGas = 20;
    unsigned maxCodeSize = unsigned(-1);

    boost::optional<u256> blockRewardOverwrite;

    bool staticCallDepthLimit() const { return !eip150Mode; }
    bool suicideChargesNewAccountGas() const { return eip150Mode; }
    bool emptinessIsNonexistence() const { return eip158Mode; }
    bool zeroValueTransferChargesNewAccountGas() const { return !eip158Mode; }
};

/// exceptionalFailedCodeDeposit: false
/// haveDelegateCall: false
/// tierStepGas: {0, 2, 3, 5, 8, 10, 20, 0}
/// txCreateGas: 21000
static const EVMSchedule FrontierSchedule = EVMSchedule(false, false, 21000);
/// value of params are equal to HomesteadSchedule
static const EVMSchedule HomesteadSchedule = EVMSchedule(true, true, 53000);
/// EIP150(refer to: https://github.com/ethereum/EIPs/blob/master/EIPS/eip-150.md)
static const EVMSchedule EIP150Schedule = [] {
    EVMSchedule schedule = HomesteadSchedule;
    schedule.eip150Mode = true;
    schedule.extcodesizeGas = 700;
    schedule.extcodecopyGas = 700;
    schedule.balanceGas = 400;
    schedule.sloadGas = 200;
    schedule.callGas = 700;
    schedule.suicideGas = 5000;
    return schedule;
}();
/// EIP158
static const EVMSchedule EIP158Schedule = [] {
    EVMSchedule schedule = EIP150Schedule;
    schedule.expByteGas = 50;
    schedule.eip158Mode = true;
    schedule.maxCodeSize = 0x6000;
    return schedule;
}();

static const EVMSchedule ByzantiumSchedule = [] {
    EVMSchedule schedule = EIP158Schedule;
    schedule.haveRevert = true;
    schedule.haveReturnData = true;
    schedule.haveStaticCall = true;
    // schedule.blockRewardOverwrite = {3 * ether};
    return schedule;
}();


static const EVMSchedule ConstantinopleSchedule = [] {
    EVMSchedule schedule = ByzantiumSchedule;
    schedule.blockhashGas = 800;
    schedule.haveCreate2 = true;
    schedule.haveBitwiseShifting = true;
    schedule.haveExtcodehash = true;
    return schedule;
}();

static const EVMSchedule FiscoBcosSchedule = [] {
    EVMSchedule schedule = ConstantinopleSchedule;
    return schedule;
}();

static const EVMSchedule FiscoBcosScheduleV2 = [] {
    EVMSchedule schedule = ConstantinopleSchedule;
    schedule.maxCodeSize = 0x40000;
    return schedule;
}();

static const EVMSchedule FiscoBcosScheduleV3 = [] {
    EVMSchedule schedule = FiscoBcosScheduleV2;
    schedule.enableIstanbul = true;
    return schedule;
}();

static const EVMSchedule EWASMSchedule = [] {
    EVMSchedule schedule = FiscoBcosScheduleV3;
    schedule.maxCodeSize = std::numeric_limits<unsigned>::max();
    // Ensure that zero bytes are not subsidised and are charged the same as non-zero bytes.
    schedule.txDataZeroGas = schedule.txDataNonZeroGas;
    return schedule;
}();

static const EVMSchedule DefaultSchedule = FiscoBcosScheduleV3;

}  // namespace eth
}  // namespace dev
