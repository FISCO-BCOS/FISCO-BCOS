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
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @file JsonHelper.h
 * @author: caryliao
 * @date 2018-10-26
 */
#pragma once

#include "libutilities/Common.h"
#include "libutilities/FixedBytes.h"
#include <json/json.h>

namespace bcos
{
namespace rpc
{
struct TransactionSkeleton
{
    bool creation = false;
    bcos::Address from;
    bcos::Address to;
    bcos::u256 value;
    bcos::bytes data;
    bcos::u256 nonce = Invalid256;
    bcos::u256 gas = Invalid256;
    bcos::u256 gasPrice = Invalid256;
    bcos::u256 blockLimit = Invalid256;
};
TransactionSkeleton toTransactionSkeleton(Json::Value const& _json);

}  // namespace rpc

}  // namespace bcos
