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

#include <map>
#include <string>
#define INVALIDNUMBER -1
#define RPC_LOG(LEVEL) LOG(LEVEL) << "[RPC]"

namespace dev
{
namespace rpc
{
///< RPCExceptionCode
enum RPCExceptionType : int
{
    Success = 0,
    IncompleteInitialization = -40010,
    InvalidRequest = -40009,
    InvalidSystemConfig = -40008,
    NoView = -40007,
    CallFrom = -40006,
    TransactionIndex = -40005,
    BlockNumberT = -40004,
    BlockHash = -40003,
    JsonParse = -40002,
    GroupID = -40001
};

extern std::map<int, std::string> RPCMsg;
}  // namespace rpc
}  // namespace dev
