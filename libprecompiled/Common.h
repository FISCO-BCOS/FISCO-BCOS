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
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file Common.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once
#include <json_spirit/JsonSpiritHeaders.h>
#include <string>

namespace dev
{
namespace precompiled
{
#define PRECOMPILED_LOG(LEVEL) LOG(LEVEL) << "[PRECOMPILED]"

/// correct return: code great or equal 0
/// system error: code from -29 to -1
/// logic error: each Precompiled occupy range of 10 numbers for code

/// note: abi.abiOut will return a positive number related to the negative number.
/// It maybe coincide with the positive number that should have been returned.

const int CODE_NO_AUTHORIZED = -1;

/// AuthorityPrecompiled -30 ~ -39
const int CODE_TABLE_AND_ADDRESS_EXIST = -30;
const int CODE_TABLE_AND_ADDRESS_NOT_EXIST = -31;

/// ConsensusPrecompiled
const int CODE_INVALID_NODEID = -40;
const int CODE_LAST_MINER = -41;

/// CNSPrecompiled -50 ~ -59
const int CODE_ADDRESS_AND_VERSION_EXIST = -50;

/// SystemConfigPrecompiled -60 ~ -69
const int CODE_INVALID_CONFIGURATION_VALUES = -60;

}  // namespace precompiled
}  // namespace dev
