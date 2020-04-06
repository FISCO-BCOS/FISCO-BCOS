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
/** @file EVMFlags.h
 * @author yujiechen
 * @date 2020.04.02
 *
 * EVMFlags identify.
 */

#pragma once
#include <stdint.h>
#include <iosfwd>
namespace dev
{
using VMFlagType = uint64_t;
struct EVMFlags
{
    static const VMFlagType FreeStorageGas;
    static const unsigned freeStorageFlagPos;
};

inline bool enableFlags(VMFlagType const& _vmFlags, unsigned const& _flagPos)
{
    return (_vmFlags & (1 << _flagPos)) > 0;
}

inline bool enableFreeStorage(VMFlagType const& _vmFlags)
{
    return enableFlags(_vmFlags, EVMFlags::freeStorageFlagPos);
}
}  // namespace dev