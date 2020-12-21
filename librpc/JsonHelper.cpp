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
 * @file JsonHelper.cpp
 * @author: caryliao
 * @date 2018-10-26
 */

#include "JsonHelper.h"
#include "libprotocol/Common.h"
#include "libutilities/JsonDataConvertUtility.h"
#include <jsonrpccpp/common/exception.h>

using namespace std;
using namespace bcos::protocol;

namespace bcos
{
namespace rpc
{
TransactionSkeleton toTransactionSkeleton(Json::Value const& _json)
{
    TransactionSkeleton ret;
    if (!_json.isObject() || _json.empty())
        return ret;

    if (!_json["from"].empty())
        ret.from = jsonStringToAddress(_json["from"].asString());
    if ((!_json["to"].empty()) && (!_json["to"].asString().empty()) &&
        _json["to"].asString() != "0x")
        ret.to = jsonStringToAddress(_json["to"].asString());
    else
        ret.creation = true;

    if (!_json["value"].empty())
        ret.value = jonStringToU256(_json["value"].asString());

    if (!_json["gas"].empty())
        ret.gas = jonStringToU256(_json["gas"].asString());

    if (!_json["gasPrice"].empty())
        ret.gasPrice = jonStringToU256(_json["gasPrice"].asString());

    if (!_json["data"].empty() && _json["data"].isString())
        ret.data = jonStringToBytes(_json["data"].asString(), OnFailed::Throw);

    if (!_json["code"].empty())
        ret.data = jonStringToBytes(_json["code"].asString(), OnFailed::Throw);

    if (!_json["randomid"].empty())
        ret.nonce = jonStringToU256(_json["randomid"].asString());

    // add blocklimit params
    if (!_json["blockLimit"].empty())
        ret.blockLimit = jonStringToU256(_json["blockLimit"].asString());

    return ret;
}

}  // namespace rpc

}  // namespace bcos
