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
 * (c) 2016-2020 fisco-dev contributors.
 *
 * @file StatisticProtocolServer.h
 * @author: yujiechen
 * @date 2020-04-01
 */
#pragma once
#include "Common.h"
#include "jsonrpccpp/server/rpcprotocolserverv2.h"
#include <libflowlimit/RPCQPSLimiter.h>
#include <libprotocol/Protocol.h>
#include <libstat/ChannelNetworkStatHandler.h>

namespace bcos
{
class StatisticProtocolServer : public jsonrpc::RpcProtocolServerV2
{
public:
    StatisticProtocolServer(jsonrpc::IProcedureInvokationHandler& _handler);
    void HandleChannelRequest(const std::string& _request, std::string& _retValue,
        std::function<bool(bcos::GROUP_ID _groupId)> const& permissionChecker);

    void setNetworkStatHandler(bcos::stat::ChannelNetworkStatHandler::Ptr _networkStatHandler)
    {
        m_networkStatHandler = _networkStatHandler;
    }

    void setQPSLimiter(bcos::flowlimit::RPCQPSLimiter::Ptr _qpsLimiter)
    {
        m_qpsLimiter = _qpsLimiter;
    }

protected:
    bool limitRPCQPS(Json::Value const& _request, std::string& _retValue);
    bool limitGroupQPS(
        bcos::GROUP_ID const& _groupId, Json::Value const& _request, std::string& _retValue);
    void wrapResponseForNodeBusy(Json::Value const& _request, std::string& _retValue);
    void wrapErrorResponse(Json::Value const& _request, std::string& _retValue, int _errorCode,
        std::string const& _errorMsg);

    bcos::GROUP_ID getGroupID(Json::Value const& _request);
    bool isValidRequest(Json::Value const& _request);

protected:
    // record group related RPC methods
    std::set<std::string> const m_groupRPCMethodSet = {"getSystemConfigByKey", "getBlockNumber",
        "getPbftView", "getSealerList", "getEpochSealersList", "getObserverList",
        "getConsensusStatus", "getSyncStatus", "getGroupPeers", "getBlockByHash",
        "getBlockByNumber", "getBlockHashByNumber", "getTransactionByHash",
        "getTransactionByBlockHashAndIndex", "getTransactionByBlockNumberAndIndex",
        "getTransactionReceipt", "getPendingTransactions", "getPendingTxSize", "call",
        "sendRawTransaction", "getCode", "getTotalTransactionCount",
        "getTransactionByHashWithProof", "getTransactionReceiptByHashWithProof",
        "sendRawTransactionAndGetProof", "getBlockHeaderByNumber", "getBlockHeaderByHash",
        "startGroup", "stopGroup", "removeGroup", "recoverGroup", "queryGroupStatus",
        "getBatchReceiptsByBlockNumberAndRange", "getBatchReceiptsByBlockHashAndRange"};

    // RPC interface without restrictions
    std::set<std::string> const m_noRestrictRpcMethodSet = {"getClientVersion"};

    bcos::stat::ChannelNetworkStatHandler::Ptr m_networkStatHandler;
    bcos::flowlimit::RPCQPSLimiter::Ptr m_qpsLimiter;
};
}  // namespace bcos
