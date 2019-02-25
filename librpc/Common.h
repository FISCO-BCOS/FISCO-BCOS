/**
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
 * along with FISCO-BCOS.  If not, see <http:www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @file Rpc.cpp
 * @author: caryliao
 * @date 2018-11-6
 */

#pragma once

#include <string>

#define INVALIDNUMBER -1
#define RPC_LOG(LEVEL) LOG(LEVEL) << "[RPC]"

namespace dev
{
namespace rpc
{
///< RPCExceptionCode
enum RPCExceptionType
{
    Success = 0,
    GroupID,
    JsonParse,
    BlockHash,
    BlockNumberT,
    TransactionIndex,
    CallFrom,
    NoView,
    InvalidSystemConfig,
    InvalidRequest
};

const std::string RPCMsg[] = {"Success", "GroupID does not exist", "Response json parse error",
    "BlockHash does not exist", "BlockNumber does not exist", "TransactionIndex is out of range",
    "Call needs a 'from' field", "Only pbft consensus supports the view property",
    "Invalid System Config",
    "Don't send request to this node, the node doesn't belong to the group of this request"};

}  // namespace rpc
}  // namespace dev
