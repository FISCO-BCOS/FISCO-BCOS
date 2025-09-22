#include "JsonRpcInterface.h"
#include <json/forwards.h>
#include <boost/beast/core/ostream.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/stream.hpp>

using namespace bcos::rpc;

void JsonRpcInterface::initMethod()
{
    using ParamsType = const Json::Value&;
    using CallbackType = std::function<void(Error::Ptr, Json::Value&)>;
    m_methodToFunc["call"] = [this](ParamsType params, CallbackType callback) {
        callI(params, std::move(callback));
    };
    m_methodToFunc["sendTransaction"] = [this](ParamsType params, CallbackType callback) {
        sendTransactionI(params, std::move(callback));
    };
    m_methodToFunc["getTransaction"] = [this](ParamsType params, CallbackType callback) {
        getTransactionI(params, std::move(callback));
    };
    m_methodToFunc["getTransactionReceipt"] = [this](ParamsType params, CallbackType callback) {
        getTransactionReceiptI(params, std::move(callback));
    };
    m_methodToFunc["getBlockByHash"] = [this](ParamsType params, CallbackType callback) {
        getBlockByHashI(params, std::move(callback));
    };
    m_methodToFunc["getBlockByNumber"] = [this](ParamsType params, CallbackType callback) {
        getBlockByNumberI(params, std::move(callback));
    };
    m_methodToFunc["getBlockHashByNumber"] = [this](ParamsType params, CallbackType callback) {
        getBlockHashByNumberI(params, std::move(callback));
    };
    m_methodToFunc["getBlockNumber"] = [this](ParamsType params, CallbackType callback) {
        getBlockNumberI(params, std::move(callback));
    };
    m_methodToFunc["getCode"] = [this](ParamsType params, CallbackType callback) {
        getCodeI(params, std::move(callback));
    };
    m_methodToFunc["getABI"] = [this](ParamsType params, CallbackType callback) {
        getABII(params, std::move(callback));
    };
    m_methodToFunc["getSealerList"] = [this](ParamsType params, CallbackType callback) {
        getSealerListI(params, std::move(callback));
    };
    m_methodToFunc["getObserverList"] = [this](ParamsType params, CallbackType callback) {
        getObserverListI(params, std::move(callback));
    };
    m_methodToFunc["getNodeListByType"] = [this](ParamsType params, CallbackType callback) {
        getNodeListByTypeI(params, std::move(callback));
    };
    m_methodToFunc["getPbftView"] = [this](ParamsType params, CallbackType callback) {
        getPbftViewI(params, std::move(callback));
    };
    m_methodToFunc["getPendingTxSize"] = [this](ParamsType params, CallbackType callback) {
        getPendingTxSizeI(params, std::move(callback));
    };
    m_methodToFunc["getSyncStatus"] = [this](ParamsType params, CallbackType callback) {
        getSyncStatusI(params, std::move(callback));
    };
    m_methodToFunc["getConsensusStatus"] = [this](ParamsType params, CallbackType callback) {
        getConsensusStatusI(params, std::move(callback));
    };
    m_methodToFunc["getSystemConfigByKey"] = [this](ParamsType params, CallbackType callback) {
        getSystemConfigByKeyI(params, std::move(callback));
    };
    m_methodToFunc["getTotalTransactionCount"] = [this](ParamsType params, CallbackType callback) {
        getTotalTransactionCountI(params, std::move(callback));
    };
    m_methodToFunc["getPeers"] = [this](ParamsType params, CallbackType callback) {
        getPeersI(params, std::move(callback));
    };
    m_methodToFunc["getGroupPeers"] = [this](ParamsType params, CallbackType callback) {
        getGroupPeersI(params, std::move(callback));
    };
    m_methodToFunc["getGroupList"] = [this](ParamsType params, CallbackType callback) {
        getGroupListI(params, std::move(callback));
    };
    m_methodToFunc["getGroupInfo"] = [this](ParamsType params, CallbackType callback) {
        getGroupInfoI(params, std::move(callback));
    };
    m_methodToFunc["getGroupInfoList"] = [this](ParamsType params, CallbackType callback) {
        getGroupInfoListI(params, std::move(callback));
    };
    m_methodToFunc["getGroupNodeInfo"] = [this](ParamsType params, CallbackType callback) {
        getGroupNodeInfoI(params, std::move(callback));
    };

    // filter interface
    m_methodToFunc["newBlockFilter"] = [this](ParamsType params, CallbackType callback) {
        newBlockFilterI(params, std::move(callback));
    };
    m_methodToFunc["newPendingTransactionFilter"] = [this](
                                                        ParamsType params, CallbackType callback) {
        newPendingTransactionFilterI(params, std::move(callback));
    };
    m_methodToFunc["newFilter"] = [this](ParamsType params, CallbackType callback) {
        newFilterI(params, std::move(callback));
    };
    m_methodToFunc["uninstallFilter"] = [this](ParamsType params, CallbackType callback) {
        uninstallFilterI(params, std::move(callback));
    };
    m_methodToFunc["getFilterChanges"] = [this](ParamsType params, CallbackType callback) {
        getFilterChangesI(params, std::move(callback));
    };
    m_methodToFunc["getFilterLogs"] = [this](ParamsType params, CallbackType callback) {
        getFilterLogsI(params, std::move(callback));
    };
    m_methodToFunc["getLogs"] = [this](ParamsType params, CallbackType callback) {
        getLogsI(params, std::move(callback));
    };

    for (const auto& method : m_methodToFunc)
    {
        RPC_IMPL_LOG(INFO) << LOG_BADGE("initMethod") << LOG_KV("method", method.first);
    }
    RPC_IMPL_LOG(INFO) << LOG_BADGE("initMethod") << LOG_KV("size", m_methodToFunc.size());
}

void JsonRpcInterface::onRPCRequest(std::string_view _requestBody, Sender _sender)
{
    JsonRequest request;
    JsonResponse response;
    try
    {
        parseRpcRequestJson(_requestBody, request);

        response.jsonrpc = request.jsonrpc;
        response.id = request.id;

        const auto& method = request.method;
        auto it = m_methodToFunc.find(method);
        if (it == m_methodToFunc.end())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(
                JsonRpcError::MethodNotFound, "The method does not exist/is not available."));
        }
        if (c_fileLogLevel == TRACE) [[unlikely]]
        {
            RPC_IMPL_LOG(TRACE) << LOG_BADGE("onRPCRequest") << LOG_KV("request", _requestBody);
        }
        it->second(
            request.params, [response, _sender](Error::Ptr _error, Json::Value& _result) mutable {
                if (_error && (_error->errorCode() != bcos::protocol::CommonError::SUCCESS))
                {
                    // error
                    response.error.code = _error->errorCode();
                    response.error.message = _error->errorMessage();
                }
                else
                {
                    response.result.swap(_result);
                }
                auto strResp = toStringResponse(std::move(response));
                if (c_fileLogLevel == TRACE) [[unlikely]]
                {
                    RPC_IMPL_LOG(TRACE)
                        << LOG_BADGE("onRPCRequest")
                        << LOG_KV("response",
                               std::string_view((const char*)strResp.data(), strResp.size()));
                }
                _sender(std::move(strResp));
            });

        // success response
        return;
    }
    catch (const JsonRpcException& e)
    {
        response.error.code = e.code();
        response.error.message = std::string(e.what());
    }
    catch (const std::exception& e)
    {
        // server internal error or unexpected error
        response.error.code = JsonRpcError::InvalidRequest;
        response.error.message = std::string(e.what());
    }
    catch (...)
    {
        RPC_IMPL_LOG(DEBUG) << LOG_BADGE("onRPCRequest")
                            << LOG_DESC("response with unknown exception");
        response.error.code = JsonRpcError::InternalError;
        response.error.message = boost::current_exception_diagnostic_information();
    }

    auto strResp = toStringResponse(response);

    RPC_IMPL_LOG(DEBUG) << LOG_BADGE("onRPCRequest") << LOG_DESC("response with exception")
                        << LOG_KV("request", _requestBody)
                        << LOG_KV("response",
                               std::string_view((const char*)strResp.data(), strResp.size()));
    _sender(std::move(strResp));
}

void bcos::rpc::parseRpcRequestJson(std::string_view _requestBody, JsonRequest& _jsonRequest)
{
    Json::Value root;
    Json::Reader jsonReader;
    std::string errorMessage;

    try
    {
        std::string jsonrpc;
        std::string method;
        int64_t id = 0;
        do
        {
            if (!jsonReader.parse(_requestBody.begin(), _requestBody.end(), root))
            {
                errorMessage = "invalid request json object";
                break;
            }

            if (!root.isMember("jsonrpc"))
            {
                errorMessage = "request has no jsonrpc field";
                break;
            }
            jsonrpc = root["jsonrpc"].asString();

            if (!root.isMember("method"))
            {
                errorMessage = "request has no method field";
                break;
            }
            method = root["method"].asString();

            if (root.isMember("id"))
            {
                id = root["id"].asInt64();
            }

            if (!root.isMember("params"))
            {
                errorMessage = "request has no params field";
                break;
            }

            if (!root["params"].isArray())
            {
                errorMessage = "request params is not array object";
                break;
            }

            auto jParams = root["params"];

            _jsonRequest.jsonrpc = jsonrpc;
            _jsonRequest.method = method;
            _jsonRequest.id = id;
            _jsonRequest.params = jParams;

            // RPC_IMPL_LOG(DEBUG) << LOG_BADGE("parseRpcRequestJson") << LOG_KV("method", method)
            //                     << LOG_KV("requestMessage", _requestBody);

            // success return
            return;
        } while (0);
    }
    catch (const std::exception& e)
    {
        RPC_IMPL_LOG(ERROR) << LOG_BADGE("parseRpcRequestJson") << LOG_KV("request", _requestBody)
                            << LOG_KV("message", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(
            JsonRpcException(JsonRpcError::ParseError, "Invalid JSON was received by the server."));
    }

    RPC_IMPL_LOG(ERROR) << LOG_BADGE("parseRpcRequestJson") << LOG_KV("request", _requestBody)
                        << LOG_KV("message", errorMessage);

    BOOST_THROW_EXCEPTION(JsonRpcException(
        JsonRpcError::InvalidRequest, "The JSON sent is not a valid Request object."));
}


bcos::bytes bcos::rpc::toStringResponse(JsonResponse _jsonResponse)
{
    auto jResp = toJsonResponse(std::move(_jsonResponse));
    auto builder = Json::StreamWriterBuilder();
    builder["commentStyle"] = "None";
    builder["indentation"] = "";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    class JsonSink
    {
    public:
        using char_type = char;
        using category = boost::iostreams::sink_tag;

        JsonSink(bcos::bytes& buffer) : m_buffer(buffer) {}

        std::streamsize write(const char* s, std::streamsize n)
        {
            m_buffer.insert(m_buffer.end(), (bcos::byte*)s, (bcos::byte*)s + n);
            return n;
        }

        bcos::bytes& m_buffer;
    };

    bcos::bytes out;
    boost::iostreams::stream<JsonSink> outputStream(out);

    writer->write(jResp, &outputStream);
    writer.reset();
    return out;
}

Json::Value bcos::rpc::toJsonResponse(JsonResponse _jsonResponse)
{
    Json::Value jResp;
    jResp["jsonrpc"] = _jsonResponse.jsonrpc;
    jResp["id"] = _jsonResponse.id;

    if (_jsonResponse.error.code == 0)
    {  // success
        jResp["result"] = std::move(_jsonResponse.result);
    }
    else
    {  // error
        Json::Value jError;
        jError["code"] = _jsonResponse.error.code;
        jError["message"] = _jsonResponse.error.message;
        jResp["error"] = std::move(jError);
    }

    return jResp;
}