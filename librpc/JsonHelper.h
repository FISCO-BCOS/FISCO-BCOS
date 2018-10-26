/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file JsonHelper.h
 * @authors:
 *   Gav Wood <i@gavwood.com>
 * @date 2015
 */
#pragma once

#include <json/json.h>
#include <libethcore/Common.h>
#include <libp2p/Common.h>

namespace dev
{
namespace eth
{
Json::Value toJson(
    dev::eth::Transaction const& _t, std::pair<h256, unsigned> _location, BlockNumber _blockNumber);
TransactionSkeleton toTransactionSkeleton(Json::Value const& _json);

}  // namespace eth

}  // namespace dev
