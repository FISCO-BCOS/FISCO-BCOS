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
 * @file ModuleWhiteList.h
 * @author: octopus
 * @date 2022-06-30
 */

#pragma once

#include <sys/types.h>
#include <array>

namespace bcos
{
namespace gateway
{
namespace ratelimit
{

/**
 * This module is used to record modules that do not limit bandwidth
 */

/*
enum ModuleID
{
    PBFT = 1000,
    Raft = 1001,
    BlockSync = 2000,
    TxsSync = 2001,
    AMOP = 3000,
};
*/

#define BIT_NUMBER_PER_UINT32 (32)

class ModuleWhiteList
{
public:
    using Ptr = std::shared_ptr<ModuleWhiteList>;
    using ConstPtr = std::shared_ptr<const ModuleWhiteList>;
    using UniquePtr = std::unique_ptr<const ModuleWhiteList>;

public:
    bool isModuleExist(uint16_t _moduleID)
    {
        uint index = _moduleID / BIT_NUMBER_PER_UINT32;
        uint temp = _moduleID % BIT_NUMBER_PER_UINT32;

        return m_moduleIDsBitMap.at(index) & (1 << temp);
    }

    bool addModuleID(uint16_t _moduleID)
    {
        uint index = _moduleID / BIT_NUMBER_PER_UINT32;
        uint temp = _moduleID % BIT_NUMBER_PER_UINT32;

        if (m_moduleIDsBitMap.at(index) & (1 << temp))
        {  // already exist
            return false;
        }

        m_moduleIDsBitMap.at(index) |= (1 << temp);
        return true;
    }


    bool removeModuleID(uint16_t _moduleID)
    {
        uint index = _moduleID / BIT_NUMBER_PER_UINT32;
        uint temp = _moduleID % BIT_NUMBER_PER_UINT32;

        if (m_moduleIDsBitMap.at(index) & (1 << temp))
        {
            m_moduleIDsBitMap.at(index) &= ~(1 << temp);
            return true;
        }

        // not exist
        return false;
    }

private:
    std::array<uint32_t, UINT16_MAX / BIT_NUMBER_PER_UINT32 + 1> m_moduleIDsBitMap;
};

}  // namespace ratelimit
}  // namespace gateway
}  // namespace bcos
