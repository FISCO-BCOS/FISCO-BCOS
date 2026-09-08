/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "MinerEndpoint.h"

#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-utilities/DataConvertUtility.h>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
std::uint64_t parseDaCapQuantity(const Json::Value& value, std::string_view field)
{
    if (!value.isString())
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, std::string(field) + " must be a hex quantity"));
    }
    auto const parsed = bcos::safeFromQuantity(value.asString());
    if (!parsed.has_value())
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, std::string(field) + " is not a valid quantity"));
    }
    return *parsed;
}
}  // namespace

task::Task<void> MinerEndpoint::setMaxDASize(const Json::Value& request, Json::Value& response)
{
    auto daCaps = m_nodeService->daCaps();
    if (!daCaps)
    {
        // Ethereum-mode nodes do not expose OP batcher DA throttling.
        BOOST_THROW_EXCEPTION(JsonRpcException(MethodNotFound, "Method not found"));
    }
    if (request.size() < 2)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InvalidParams, "miner_setMaxDASize expects [maxTxSize, maxBlockSize]"));
    }

    auto const maxTxSize = parseDaCapQuantity(request[0U], "maxTxSize");
    auto const maxBlockSize = parseDaCapQuantity(request[1U], "maxBlockSize");
    daCaps->maxTxSize.store(maxTxSize, std::memory_order_relaxed);
    daCaps->maxBlockSize.store(maxBlockSize, std::memory_order_relaxed);

    Json::Value result = true;
    buildJsonContent(result, response);
    co_return;
}
