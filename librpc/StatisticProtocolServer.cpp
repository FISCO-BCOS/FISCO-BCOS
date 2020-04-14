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
 * @file StatisticPotocolServer.cpp
 * @author: yujiechen
 * @date 2020-04-01
 */
#include "StatisticProtocolServer.h"

using namespace dev;
using namespace jsonrpc;

StatisticPotocolServer::StatisticPotocolServer(jsonrpc::IProcedureInvokationHandler& _handler)
  : RpcProtocolServerV2(_handler)
{}

void StatisticPotocolServer::limitRPCQPS()
{
    if (!m_qpsLimiter)
    {
        return;
    }
    m_qpsLimiter->acquire();
}

void StatisticPotocolServer::limitGroupQPS(dev::GROUP_ID const& _groupId)
{
    if (!m_qpsLimiter)
    {
        return;
    }
    m_qpsLimiter->acquireFromGroup(_groupId);
}

// Overload RpcProtocolServerV2 to implement RPC interface network statistics function
void StatisticPotocolServer::HandleRequest(const std::string& _request, std::string& _retValue)
{
    // limit the whole RPC QPS
    limitRPCQPS();
    // except for adding statistical logic,
    // the following implementation is the same as RpcProtocolServerV2::HandleRequest
    Json::Reader reader;
    Json::Value req;
    Json::Value resp;
    Json::FastWriter w;
    dev::GROUP_ID groupId = -1;
    if (reader.parse(_request, req, false))
    {
        if (m_networkStatHandler)
        {
            groupId = getGroupID(req);
            // limit group QPS
            limitGroupQPS(groupId);
        }
        this->HandleJsonRequest(req, resp);
    }
    else
    {
        this->WrapError(Json::nullValue, Errors::ERROR_RPC_JSON_PARSE_ERROR,
            Errors::GetErrorMessage(Errors::ERROR_RPC_JSON_PARSE_ERROR), resp);
    }
    if (resp != Json::nullValue)
        _retValue = w.write(resp);
    if (m_networkStatHandler && groupId != -1)
    {
        m_networkStatHandler->updateIncomingTrafficForRPC(groupId, _request.size());
        m_networkStatHandler->updateOutcomingTrafficForRPC(groupId, _retValue.size());
    }
}

dev::GROUP_ID StatisticPotocolServer::getGroupID(Json::Value const& _request)
{
    try
    {
        if (!_request.isObject() || !_request.isMember(KEY_REQUEST_METHODNAME) ||
            !_request.isMember(KEY_REQUEST_PARAMETERS))
        {
            return -1;
        }

        auto procedureName = _request[KEY_REQUEST_METHODNAME].asString();
        if (!m_networkStatHandler->shouldStatistic(procedureName))
        {
            return -1;
        }
        return boost::lexical_cast<dev::GROUP_ID>(_request[KEY_REQUEST_PARAMETERS][0u].asString());
    }
    catch (std::exception const& _e)
    {
        LOG(WARNING) << LOG_DESC("StatisticPotocolServer: getGroupID failed!")
                     << LOG_KV("errorInfo", boost::diagnostic_information(_e));
        return -1;
    }
}