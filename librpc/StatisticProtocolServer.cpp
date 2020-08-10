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
 * @file StatisticProtocolServer.cpp
 * @author: yujiechen
 * @date 2020-04-01
 */
#include "StatisticProtocolServer.h"

using namespace dev;
using namespace jsonrpc;
using namespace dev::rpc;

StatisticProtocolServer::StatisticProtocolServer(jsonrpc::IProcedureInvokationHandler& _handler)
  : RpcProtocolServerV2(_handler)
{}

bool StatisticProtocolServer::limitRPCQPS(Json::Value const& _request, std::string& _retValue)
{
    // get procedure name failed
    if (!isValidRequest(_request))
    {
        return true;
    }
    auto procedureName = _request[KEY_REQUEST_METHODNAME].asString();
    // no restrict rpc method
    if (m_noRestrictRpcMethodSet.count(procedureName))
    {
        m_qpsLimiter->acquireWithoutWait();
        return true;
    }
    auto canHandle = m_qpsLimiter->acquire();
    if (!canHandle)
    {
        wrapResponseForNodeBusy(_request, _retValue);
    }
    return canHandle;
}

bool StatisticProtocolServer::limitGroupQPS(
    dev::GROUP_ID const& _groupId, Json::Value const& _request, std::string& _retValue)
{
    if (_groupId == -1)
    {
        return true;
    }
    auto canHandle = m_qpsLimiter->acquireFromGroup(_groupId);
    if (!canHandle)
    {
        wrapResponseForNodeBusy(_request, _retValue);
    }

    return canHandle;
}

// QPS limit exceeded, respond OverQPSLimit
void StatisticProtocolServer::wrapResponseForNodeBusy(
    Json::Value const& _request, std::string& _retValue)
{
    std::string errorMsg =
        _request[KEY_REQUEST_METHODNAME].asString() + " " + RPCMsg[RPCExceptionType::OverQPSLimit];
    wrapErrorResponse(_request, _retValue, RPCExceptionType::OverQPSLimit, errorMsg);
}

void StatisticProtocolServer::wrapErrorResponse(Json::Value const& _request, std::string& _retValue,
    int _errorCode, std::string const& _errorMsg)
{
    Json::Value resp;
    Json::FastWriter writer;
    this->WrapError(_request, _errorCode, _errorMsg, resp);
    if (resp != Json::nullValue)
        _retValue = writer.write(resp);
}

// Overload RpcProtocolServerV2 to implement RPC interface network statistics function
void StatisticProtocolServer::HandleChannelRequest(const std::string& _request,
    std::string& _retValue, std::function<bool(dev::GROUP_ID _groupId)> const& permissionChecker)
{  // except for adding statistical logic,
    // the following implementation is the same as RpcProtocolServerV2::HandleRequest
    Json::Reader reader;
    Json::Value req;
    Json::Value resp;
    Json::FastWriter w;

    dev::GROUP_ID groupId = -1;
    if (reader.parse(_request, req, false))
    {
        // get groupId
        groupId = getGroupID(req);
        // check sdk allow list
        if (permissionChecker && !permissionChecker(groupId))
        {
            std::string errorMsg = req[KEY_REQUEST_METHODNAME].asString() + " " +
                                   RPCMsg[RPCExceptionType::PermissionDenied];
            wrapErrorResponse(_request, _retValue, RPCExceptionType::PermissionDenied, errorMsg);
            return;
        }
        if (m_qpsLimiter)
        {
            // limit the whole RPC QPS
            if (!limitRPCQPS(req, _retValue))
            {
                return;
            }
            // limit group QPS
            if (!limitGroupQPS(groupId, req, _retValue))
            {
                return;
            }
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
        m_networkStatHandler->updateOutgoingTrafficForRPC(groupId, _retValue.size());
    }
}

// check the request
bool StatisticProtocolServer::isValidRequest(Json::Value const& _request)
{
    if (!_request.isObject() || !_request.isMember(KEY_REQUEST_METHODNAME) ||
        !_request.isMember(KEY_REQUEST_PARAMETERS))
    {
        return false;
    }
    return true;
}

dev::GROUP_ID StatisticProtocolServer::getGroupID(Json::Value const& _request)
{
    try
    {
        if (!isValidRequest(_request))
        {
            return -1;
        }

        auto procedureName = _request[KEY_REQUEST_METHODNAME].asString();
        if (!m_groupRPCMethodSet.count(procedureName))
        {
            return -1;
        }
        return boost::lexical_cast<dev::GROUP_ID>(_request[KEY_REQUEST_PARAMETERS][0u].asString());
    }
    catch (std::exception const& _e)
    {
        LOG(WARNING) << LOG_DESC("StatisticProtocolServer: getGroupID failed!")
                     << LOG_KV("errorInfo", boost::diagnostic_information(_e));
        return -1;
    }
}