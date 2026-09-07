#include "RpcServiceServer.h"
#include "../Common/TarsUtils.h"
#include <bcos-tars-protocol/Common.h>
#include <bcos-tars-protocol/ErrorConverter.h>
#include <bcos-tars-protocol/protocol/TransactionSubmitResultImpl.h>
#include <bcos-task/Wait.h>
#include <servant/Servant.h>
#include <memory>

using namespace bcostars;
bcostars::Error RpcServiceServer::asyncNotifyBlockNumber(const std::string& _groupID,
    const std::string& _nodeName, tars::Int64 blockNumber, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    m_rpcInitializer->rpc()->asyncNotifyBlockNumber(
        _groupID, _nodeName, blockNumber, [current, blockNumber](bcos::Error::Ptr _error) {
            RPCSERVICE_LOG(DEBUG) << LOG_BADGE("asyncNotifyBlockNumber")
                                  << LOG_KV("blockNumber", blockNumber)
                                  << LOG_KV("code", _error ? _error->errorCode() : 0)
                                  << LOG_KV("msg", _error ? _error->errorMessage() : "");
            async_response_asyncNotifyBlockNumber(current, toTarsError(_error));
        });

    return bcostars::Error();
}

bcostars::Error RpcServiceServer::asyncNotifyGroupInfo(
    const bcostars::GroupInfo& groupInfo, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto bcosGroupInfo = toBcosGroupInfo(
        m_rpcInitializer->chainNodeInfoFactory(), m_rpcInitializer->groupInfoFactory(), groupInfo);
    m_rpcInitializer->rpc()->asyncNotifyGroupInfo(
        bcosGroupInfo, [current](bcos::Error::Ptr&& _error) {
            async_response_asyncNotifyGroupInfo(current, toTarsError(_error));
        });
    return bcostars::Error();
}

bcostars::Error RpcServiceServer::asyncNotifyAMOPMessage(tars::Int32 _type,
    const std::string& _topic, const std::vector<tars::Char>& _requestData, std::vector<tars::Char>&,
    tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto rpc = m_rpcInitializer->rpc();
    // copy the payload and pass all state as coroutine parameters so it stays alive for the whole
    // (possibly deferred) notify; try/catch guarantees the RPC is always answered even if the
    // notify throws (current->setResponse(false) already disabled the automatic reply)
    auto requestData = std::make_shared<std::vector<tars::Char>>(_requestData);
    bcos::task::wait([](auto _rpc, auto _type, auto _topic, auto _requestData,
                         auto _current) -> bcos::task::Task<void> {
        try
        {
            auto [error, responseData] = co_await _rpc->notifyAMOPMessage(_type, _topic,
                bcos::bytesConstRef(
                    (const bcos::byte*)_requestData->data(), _requestData->size()));
            std::vector<tars::Char> response;
            if (responseData)
            {
                response.assign(responseData->begin(), responseData->end());
            }
            async_response_asyncNotifyAMOPMessage(_current, toTarsError(error), response);
        }
        catch (std::exception const& e)
        {
            RPCSERVICE_LOG(WARNING) << LOG_DESC("asyncNotifyAMOPMessage exception")
                                    << LOG_KV("topic", _topic)
                                    << LOG_KV("what", boost::diagnostic_information(e));
            async_response_asyncNotifyAMOPMessage(_current,
                toTarsError(BCOS_ERROR_PTR(-1, boost::diagnostic_information(e))), {});
        }
    }(rpc, _type, _topic, requestData, current));
    return bcostars::Error();
}

bcostars::Error RpcServiceServer::asyncNotifySubscribeTopic(
    std::string&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_rpcInitializer->rpc()->asyncNotifySubscribeTopic(
        [current](bcos::Error::Ptr&& _error, std::string _topicInfo) {
            async_response_asyncNotifySubscribeTopic(current, toTarsError(_error), _topicInfo);
        });
    return bcostars::Error();
}