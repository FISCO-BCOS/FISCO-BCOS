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
/** @file EVMHostInterface.h
 * @author wheatli
 * @date 2018.8.28
 * @record copy from aleth, this is a vm interface
 */

#pragma once

#include "Common.h"
#include "evmc/evmc.h"
#include "evmc/instructions.h"
#include <libconfig/GlobalConfigure.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/Common.h>
#include <libethcore/EVMFlags.h>
#include <libethcore/EVMSchedule.h>
#include <libethcore/LogEntry.h>
#include <libutilities/Common.h>
#include <libutilities/CommonData.h>

#include <boost/optional.hpp>
#include <functional>
#include <set>

namespace bcos
{
namespace executive
{
const evmc_host_interface* getHostInterface();


/**
 * @brief : trans ethereum addess to evm address
 * @param _addr : the ehereum address
 * @return evmc_address : the transformed evm address
 */
inline evmc_address toEvmC(Address const& _addr)
{
    return reinterpret_cast<evmc_address const&>(_addr);
}

/**
 * @brief : trans ethereum hash to evm hash
 * @param _h : hash value
 * @return evmc_bytes32 : transformed hash
 */
inline evmc_bytes32 toEvmC(h256 const& _h)
{
    return reinterpret_cast<evmc_bytes32 const&>(_h);
}
/**
 * @brief : trans uint256 number of evm-represented to u256
 * @param _n : the uint256 number that can parsed by evm
 * @return u256 : transformed u256 number
 */
inline u256 fromEvmC(evmc_bytes32 const& _n)
{
    return fromBigEndian<u256>(_n.bytes);
}

/**
 * @brief : trans evm address to ethereum address
 * @param _addr : the address that can parsed by evm
 * @return Address : the transformed ethereum address
 */
inline Address fromEvmC(evmc_address const& _addr)
{
    return reinterpret_cast<Address const&>(_addr);
}
}  // namespace executive
}  // namespace bcos
