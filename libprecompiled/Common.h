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
const int CODE_SUCCESS = 0;

/// note: abi.abiOut will return a positive number related to the negative number.
/// It maybe coincide with the positive number that should have been returned.

/// Common error code among all precompiled contracts
const int CODE_UNKNOW_FUNCTION_CALL = 50100;

/// PermissionPrecompiled 51000 ~ 51099
const int CODE_TABLE_AND_ADDRESS_EXIST = 51000;
const int CODE_TABLE_AND_ADDRESS_NOT_EXIST = 51001;

/// ConsensusPrecompiled
const int CODE_INVALID_NODEID = 51100;
const int CODE_LAST_SEALER = 51101;

/// CNSPrecompiled
const int CODE_ADDRESS_AND_VERSION_EXIST = 51200;

/// SystemConfigPrecompiled
const int CODE_INVALID_CONFIGURATION_VALUES = 51300;

/// DagTransferPrecompiled 51400 ~ 51499
const int CODE_INVALID_USER_NAME = 51400;
const int CODE_INVALID_AMOUNT = 51401;
const int CODE_INVALID_USER_NOT_EXIST = 51402;
const int CODE_INVALID_USER_ALREADY_EXIST = 51403;
const int CODE_INVALID_INSUFFICIENT_BALANCE = 51404;
const int CODE_INVALID_BALANCE_OVERFLOW = 51405;
const int CODE_INVALID_OPENTALBLE_FAILED = 51406;

void getOut(bytes& out, int const& result);

}  // namespace precompiled
}  // namespace dev
