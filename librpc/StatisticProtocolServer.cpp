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

// Overload RpcProtocolServerV2 to implement RPC interface network statistics function
void StatisticPotocolServer::HandleRequest(const std::string& _request, std::string& _retValue)
{
    // except for adding statistical logic,
    // the following implementation is the same as RpcProtocolServerV2::HandleRequest
    Json::Reader reader;
    Json::Value req;
    Json::Value resp;
    Json::FastWriter w;

    if (reader.parse(_request, req, false))
    {
        this->HandleJsonRequest(req, resp);
    }
    else
    {
        this->WrapError(Json::nullValue, Errors::ERROR_RPC_JSON_PARSE_ERROR,
            Errors::GetErrorMessage(Errors::ERROR_RPC_JSON_PARSE_ERROR), resp);
    }
    auto groupId = getGroupIDAndUpdateResponse(resp);
    if (resp != Json::nullValue)
        _retValue = w.write(resp);
    if (m_networkStatHandler && groupId != -1)
    {
        m_networkStatHandler->updateIncomingTrafficForRPC(groupId, _request.size());
        m_networkStatHandler->updateOutcomingTrafficForRPC(groupId, _retValue.size());
    }
}

dev::GROUP_ID StatisticPotocolServer::getGroupID(Json::Value const& _input)
{
    try
    {
        return boost::lexical_cast<dev::GROUP_ID>(_input[0u].asString());
    }
    catch (std::exception const& _e)
    {
        LOG(WARNING) << LOG_DESC("StatisticPotocolServer: getGroupID failed!")
                     << LOG_KV("errorInfo", boost::diagnostic_information(_e));
        return -1;
    }
}

void StatisticPotocolServer::WrapResult(
    const Json::Value& _request, Json::Value& _response, Json::Value& _retValue)
{
    RpcProtocolServerV2::WrapResult(_request, _response, _retValue);
    try
    {
        if (!m_networkStatHandler)
        {
            return;
        }
        auto procedureName = _request[KEY_REQUEST_METHODNAME].asString();
        if (!m_networkStatHandler->shouldStatistic(procedureName))
        {
            return;
        }
        // get groupId and wrapper it into _response
        auto groupId = getGroupID(_request[KEY_REQUEST_PARAMETERS]);
        if (groupId != -1)
        {
            _response[KEY_GROUPID_FIELD] = groupId;
        }
    }
    catch (const std::exception& _e)
    {
        LOG(WARNING) << LOG_DESC("StatisticPotocolServer: WrapResult failed!")
                     << LOG_KV("errorInfo", boost::diagnostic_information(_e));
    }
}

// get groupId from the KEY_GROUPID_FIELD field of response,
// and remove the field after the groupId obtained
dev::GROUP_ID StatisticPotocolServer::getGroupIDAndUpdateResponse(Json::Value& _resp)
{
    try
    {
        if (!m_networkStatHandler)
        {
            return -1;
        }
        if (!_resp.isMember(KEY_GROUPID_FIELD))
        {
            return -1;
        }
        auto groupIdJsonValue = _resp[KEY_GROUPID_FIELD];
        if (groupIdJsonValue == Json::nullValue)
        {
            _resp.removeMember(KEY_GROUPID_FIELD);
            return -1;
        }
        GROUP_ID groupId = groupIdJsonValue.asInt();
        // remove KEY_GROUPID_FIELD from response
        _resp.removeMember(KEY_GROUPID_FIELD);
        return groupId;
    }
    catch (std::exception const& _e)
    {
        LOG(ERROR) << LOG_DESC("getGroupIDAndUpdateResponse exceptioned")
                   << LOG_KV("errorInfo", boost::diagnostic_information(_e));
        return -1;
    }
}