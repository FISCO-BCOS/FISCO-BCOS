/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file TxLookup.cpp
 */
#include "TxLookup.h"

namespace bcos::ops
{
Json::Value TxLookup::toJson() const
{
    Json::Value value;
    value["kind"] = kind == Kind::Found      ? "found" :
                    kind == Kind::NotOnChain ? "not_on_chain" :
                                               "unavailable";
    if (!source.empty())
    {
        value["source"] = source;
    }
    if (kind == Kind::Found)
    {
        value["blockNumber"] = static_cast<Json::Int64>(blockNumber);
        value["status"] = status;
    }
    if (!detail.empty())
    {
        value["detail"] = detail;
    }
    return value;
}

std::string TxLookup::line() const
{
    switch (kind)
    {
    case Kind::Found:
        return source + ": included in block " + std::to_string(blockNumber) + ", status " +
               std::to_string(status);
    case Kind::NotOnChain:
        return source + ": not on chain (" + detail + ")";
    case Kind::Unavailable:
        break;
    }
    return "rpc: unavailable (" + detail + ")";
}

TxLookup lookupTxOnChain(RpcCall const& _call, std::string const& _source,
    std::string const& _group, std::string const& _hash)
{
    TxLookup lookup;
    lookup.source = _source;
    Json::Value params(Json::arrayValue);
    params.append(_group);
    params.append("");
    params.append(_hash);
    params.append(false);
    try
    {
        Json::Value const receipt = _call("getTransactionReceipt", params);
        if (!receipt.isObject() || !receipt.isMember("blockNumber") || !receipt.isMember("status"))
        {
            lookup.kind = TxLookup::Kind::NotOnChain;
            lookup.detail = "no receipt for this hash";
            return lookup;
        }
        lookup.kind = TxLookup::Kind::Found;
        lookup.blockNumber = receipt["blockNumber"].asInt64();
        lookup.status = receipt["status"].isString() ? std::stoi(receipt["status"].asString()) :
                                                       receipt["status"].asInt();
    }
    catch (std::exception const& e)
    {
        // the node answered with a JSON-RPC error: the hash is not there, the node is fine
        lookup.kind = TxLookup::Kind::NotOnChain;
        lookup.detail = e.what();
    }
    return lookup;
}

TxLookup TxLookup::unavailable(std::string _detail)
{
    TxLookup lookup;
    lookup.kind = TxLookup::Kind::Unavailable;
    lookup.detail = std::move(_detail);
    return lookup;
}
}  // namespace bcos::ops
