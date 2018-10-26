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
 * @brief: unit test for Session
 *
 * @file JsonHelper.cpp
 * @author: caryliao
 * @date 2018-10-26
 */

#include "JsonHelper.h"
#include <jsonrpccpp/common/exception.h>
#include <libdevcore/easylog.h>
#include <libethcore/CommonJS.h>
#include <libethcore/SealEngine.h>
#include <libethcore/Transaction.h>

using namespace std;
using namespace dev::eth;

namespace dev
{
namespace rpc
{
Json::Value toJson(
    Transaction const& _t, std::pair<h256, unsigned> _location, BlockNumber _blockNumber)
{
    Json::Value res;
    if (_t)
    {
        res["hash"] = toJS(_t.sha3());
        res["input"] = toJS(_t.data());
        res["to"] = _t.isCreation() ? Json::Value() : toJS(_t.receiveAddress());
        res["from"] = toJS(_t.safeSender());
        res["gas"] = toJS(_t.gas());
        res["gasPrice"] = toJS(_t.gasPrice());
        res["nonce"] = toJS(_t.nonce());
        res["value"] = toJS(_t.value());
        res["blockHash"] = toJS(_location.first);
        res["transactionIndex"] = toJS(_location.second);
        res["blockNumber"] = toJS(_blockNumber);
    }
    return res;
}

TransactionSkeleton toTransactionSkeleton(Json::Value const& _json)
{
    TransactionSkeleton ret;
    if (!_json.isObject() || _json.empty())
        return ret;

    if (!_json["params"]["from"].empty())
        ret.from = jsToAddress(_json["params"]["from"].asString());
    if ((!_json["params"]["to"].empty()) && (!_json["params"]["to"].asString().empty()) &&
        _json["params"]["to"].asString() != "0x")
        ret.to = jsToAddress(_json["params"]["to"].asString());
    else
        ret.creation = true;

    if (!_json["params"]["value"].empty())
        ret.value = jsToU256(_json["params"]["value"].asString());

    if (!_json["params"]["gas"].empty())
        ret.gas = jsToU256(_json["params"]["gas"].asString());

    if (!_json["params"]["gasPrice"].empty())
        ret.gasPrice = jsToU256(_json["params"]["gasPrice"].asString());

    if (!_json["params"]["data"].empty() && _json["params"]["data"].isString())
        ret.data = jsToBytes(_json["params"]["data"].asString(), OnFailed::Throw);

    if (!_json["params"]["code"].empty())
        ret.data = jsToBytes(_json["params"]["code"].asString(), OnFailed::Throw);

    if (!_json["params"]["randomid"].empty())
        ret.nonce = jsToU256(_json["params"]["randomid"].asString());

    // add blocklimit params
    if (!_json["params"]["blockLimit"].empty())
        ret.blockLimit = jsToU256(_json["params"]["blockLimit"].asString());

    return ret;
}

}  // namespace rpc

}  // namespace dev
