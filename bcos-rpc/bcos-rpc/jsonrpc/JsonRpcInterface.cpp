#include "JsonRpcInterface.h"
#include <json/forwards.h>
#include <boost/beast/core/ostream.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/stream.hpp>
#include <iterator>
#include <ostream>
#include <sstream>

using namespace bcos::rpc;

void JsonRpcInterface::initMethod()
{
    m_methodToFunc["call"] =
        std::bind(&JsonRpcInterface::callI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["sendTransaction"] = std::bind(
        &JsonRpcInterface::sendTransactionI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getTransaction"] = std::bind(
        &JsonRpcInterface::getTransactionI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getTransactionReceipt"] = std::bind(&JsonRpcInterface::getTransactionReceiptI,
        this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getBlockByHash"] = std::bind(
        &JsonRpcInterface::getBlockByHashI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getBlockByNumber"] = std::bind(
        &JsonRpcInterface::getBlockByNumberI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getBlockHashByNumber"] = std::bind(&JsonRpcInterface::getBlockHashByNumberI,
        this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getBlockNumber"] = std::bind(
        &JsonRpcInterface::getBlockNumberI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getCode"] =
        std::bind(&JsonRpcInterface::getCodeI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getABI"] =
        std::bind(&JsonRpcInterface::getABII, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getSealerList"] = std::bind(
        &JsonRpcInterface::getSealerListI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getObserverList"] = std::bind(
        &JsonRpcInterface::getObserverListI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getPbftView"] = std::bind(
        &JsonRpcInterface::getPbftViewI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getPendingTxSize"] = std::bind(
        &JsonRpcInterface::getPendingTxSizeI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getSyncStatus"] = std::bind(
        &JsonRpcInterface::getSyncStatusI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getConsensusStatus"] = std::bind(
        &JsonRpcInterface::getConsensusStatusI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getSystemConfigByKey"] = std::bind(&JsonRpcInterface::getSystemConfigByKeyI,
        this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getTotalTransactionCount"] =
        std::bind(&JsonRpcInterface::getTotalTransactionCountI, this, std::placeholders::_1,
            std::placeholders::_2);
    m_methodToFunc["getPeers"] =
        std::bind(&JsonRpcInterface::getPeersI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getGroupPeers"] = std::bind(
        &JsonRpcInterface::getGroupPeersI, this, std::placeholders::_1, std::placeholders::_2);

    m_methodToFunc["getGroupList"] = std::bind(
        &JsonRpcInterface::getGroupListI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getGroupInfo"] = std::bind(
        &JsonRpcInterface::getGroupInfoI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getGroupInfoList"] = std::bind(
        &JsonRpcInterface::getGroupInfoListI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getGroupNodeInfo"] = std::bind(
        &JsonRpcInterface::getGroupNodeInfoI, this, std::placeholders::_1, std::placeholders::_2);

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

        it->second(request.params,
            [_requestBody, response, _sender](Error::Ptr _error, Json::Value& _result) mutable {
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
                RPC_IMPL_LOG(TRACE)
                    << LOG_BADGE("onRPCRequest") << LOG_KV("request", _requestBody)
                    << LOG_KV("response",
                           std::string_view((const char*)strResp.data(), strResp.size()));
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

    auto strResp = toStringResponse(response);

    RPC_IMPL_LOG(DEBUG) << LOG_BADGE("onRPCRequest") << LOG_KV("request", _requestBody)
                        << LOG_KV("response",
                               std::string_view((const char*)strResp.data(), strResp.size()));
    _sender(strResp);
}

void JsonRpcInterface::parseRpcRequestJson(std::string_view _requestBody, JsonRequest& _jsonRequest)
{
    Json::Value root;
    Json::Reader jsonReader;
    std::string errorMessage;

    try
    {
        std::string jsonrpc = "";
        std::string method = "";
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
                            << LOG_KV("error", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(
            JsonRpcException(JsonRpcError::ParseError, "Invalid JSON was received by the server."));
    }

    RPC_IMPL_LOG(ERROR) << LOG_BADGE("parseRpcRequestJson") << LOG_KV("request", _requestBody)
                        << LOG_KV("errorMessage", errorMessage);

    BOOST_THROW_EXCEPTION(JsonRpcException(
        JsonRpcError::InvalidRequest, "The JSON sent is not a valid Request object."));
}


bcos::bytes JsonRpcInterface::toStringResponse(JsonResponse _jsonResponse)
{
    auto jResp = toJsonResponse(std::move(_jsonResponse));
    std::unique_ptr<Json::StreamWriter> writer(Json::StreamWriterBuilder().newStreamWriter());
    class JsonSink
    {
    public:
        typedef char char_type;
        typedef boost::iostreams::sink_tag category;

        JsonSink(bcos::bytes& buffer) : m_buffer(buffer) {}

        std::streamsize write(const char* s, std::streamsize n)
        {
            m_buffer.insert(m_buffer.end(), (bcos::byte*)s, (bcos::byte*)s + n);
            return n;
        }

        bcos::bytes& m_buffer;
        /* Other members */
    };

    bcos::bytes out;
    boost::iostreams::stream<JsonSink> outputStream(out);

    writer->write(jResp, &outputStream);
    writer.reset();
    return out;
}

Json::Value JsonRpcInterface::toJsonResponse(JsonResponse _jsonResponse)
{
    Json::Value jResp;
    jResp["jsonrpc"] = std::move(_jsonResponse.jsonrpc);
    jResp["id"] = std::move(_jsonResponse.id);

    if (_jsonResponse.error.code == 0)
    {  // success
        jResp["result"] = std::move(_jsonResponse.result);
    }
    else
    {  // error
        Json::Value jError;
        jError["code"] = std::move(_jsonResponse.error.code);
        jError["message"] = std::move(_jsonResponse.error.message);
        jResp["error"] = std::move(jError);
    }

    return jResp;
}