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
#include "libblockverifier/Precompiled.h"
#include <memory>
#include <string>

namespace dev
{
namespace blockverifier
{
class ExecutiveContext;
}

namespace storage
{
class Table;
}

namespace precompiled
{
#define PRECOMPILED_LOG(LEVEL) LOG(LEVEL) << "[PRECOMPILED]"

/// correct return: code great or equal 0

/// note: abi.abiOut will return a positive number related to the negative number.
/// It maybe coincide with the positive number that should have been returned.

/// AuthorityPrecompiled 51000 ~ 51099
const int CODE_TABLE_AND_ADDRESS_EXIST = 51000;
const int CODE_TABLE_AND_ADDRESS_NOT_EXIST = 51001;

/// ConsensusPrecompiled
const int CODE_INVALID_NODEID = 51100;
const int CODE_LAST_SEALER = 51101;

/// CNSPrecompiled
const int CODE_ADDRESS_AND_VERSION_EXIST = 51200;

/// SystemConfigPrecompiled
const int CODE_INVALID_CONFIGURATION_VALUES = 51300;

}  // namespace precompiled
}  // namespace dev
