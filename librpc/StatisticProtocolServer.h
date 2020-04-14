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
 * @file StatisticPotocolServer.h
 * @author: yujiechen
 * @date 2020-04-01
 */
#pragma once
#include "Common.h"
#include "jsonrpccpp/server/rpcprotocolserverv2.h"
#include <libethcore/Protocol.h>
#include <libflowlimit/RPCQPSLimiter.h>
#include <libstat/ChannelNetworkStatHandler.h>

namespace dev
{
class StatisticPotocolServer : public jsonrpc::RpcProtocolServerV2
{
public:
    StatisticPotocolServer(jsonrpc::IProcedureInvokationHandler& _handler);
    void HandleRequest(const std::string& _request, std::string& _retValue) override;

    void setNetworkStatHandler(dev::stat::ChannelNetworkStatHandler::Ptr _networkStatHandler)
    {
        m_networkStatHandler = _networkStatHandler;
    }

    void setQPSLimiter(dev::limit::RPCQPSLimiter::Ptr _qpsLimiter) { m_qpsLimiter = _qpsLimiter; }

private:
    void limitRPCQPS();
    void limitGroupQPS(dev::GROUP_ID const& _groupId);

    dev::GROUP_ID getGroupID(Json::Value const& _input);

    dev::stat::ChannelNetworkStatHandler::Ptr m_networkStatHandler;
    dev::limit::RPCQPSLimiter::Ptr m_qpsLimiter;
};
}  // namespace dev