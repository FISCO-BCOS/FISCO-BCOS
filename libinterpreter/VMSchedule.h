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
/** @file VMSchedule.h
 * @author yujiechen
 * @date 2020.3.26
 * @ refactor libinterpreter
 */

#pragma once
#include <memory>

namespace bcos
{
namespace protocol
{
struct VMSchedule
{
    using Ptr = std::shared_ptr<VMSchedule>;
    VMSchedule() = default;
    virtual ~VMSchedule() {}
    static constexpr int64_t stackLimit = 1024;

    int64_t stepGas0 = 0;
    int64_t stepGas1 = 2;
    int64_t stepGas2 = 3;
    int64_t stepGas3 = 5;
    int64_t stepGas4 = 8;
    int64_t stepGas5 = 10;
    int64_t stepGas6 = 20;
    int64_t sha3Gas = 30;
    int64_t sha3WordGas = 6;
    int64_t sloadGas = 200;
    int64_t sstoreSetGas = 20000;
    int64_t sstoreResetGas = 5000;
    int64_t jumpdestGas = 1;
    int64_t logGas = 375;
    int64_t logDataGas = 8;
    int64_t logTopicGas = 375;
    // create takes up a lot of CPU
    int64_t createGas = 32000;
    int64_t memoryGas = 3;
    int64_t quadCoeffDiv = 512;
    int64_t copyGas = 3;
    int64_t valueTransferGas = 9000;
    int64_t callStipend = 2300;
    int64_t callNewAccount = 25000;
};

// Remove storage gas when the storage is not expensive
struct FreeStorageVMSchedule : public VMSchedule
{
public:
    using Ptr = std::shared_ptr<FreeStorageVMSchedule>;
    FreeStorageVMSchedule()
    {
        createGas = 16000;
        sstoreSetGas = 1200;
        sstoreResetGas = 1200;
        sloadGas = 1200;
        // remove value transfer
        valueTransferGas = stepGas0;
        callStipend = stepGas5;
        callNewAccount = stepGas5;
    }
    virtual ~FreeStorageVMSchedule() {}
};
}  // namespace protocol
}  // namespace bcos
