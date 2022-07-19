/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file JsonRpcRequest.h
 * @author: octopus
 * @date 2021-08-10
 */

#pragma once
#include <json/json.h>
#include <atomic>
#include <list>
#include <memory>
#include <string>

namespace bcos
{
namespace cppsdk
{
namespace jsonrpc
{
class JsonRpcException : public std::exception
{
public:
    JsonRpcException(int32_t _code, std::string const& _msg) : m_code(_code), m_msg(_msg) {}
    virtual const char* what() const noexcept override { return m_msg.c_str(); }

public:
    int32_t code() const noexcept { return m_code; }
    std::string msg() const noexcept { return m_msg; }

private:
    int32_t m_code;
    std::string m_msg;
};

enum JsonRpcError : int32_t
{
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603
    // -32000 to -32099: Server error	Reserved for implementation-defined server-errors.
};

class JsonRpcRequest
{
public:
    using Ptr = std::shared_ptr<JsonRpcRequest>;

    JsonRpcRequest() {}
    ~JsonRpcRequest() {}

public:
    void setJsonrpc(const std::string _jsonrpc) { m_jsonrpc = _jsonrpc; }
    std::string jsonrpc() { return m_jsonrpc; }
    void setMethod(const std::string _method) { m_method = _method; }
    std::string method() { return m_method; }
    void setId(int64_t _id) { m_id = _id; }
    int64_t id() { return m_id; }
    Json::Value params() { return m_params; }
    void setParams(const Json::Value& _params) { m_params = _params; }

public:
    std::string toJson();
    void fromJson(const std::string& _request);

private:
    std::string m_jsonrpc = "2.0";
    std::string m_method;
    int64_t m_id{1};
    Json::Value m_params;
};

class JsonRpcRequestFactory
{
public:
    using Ptr = std::shared_ptr<JsonRpcRequestFactory>;
    JsonRpcRequestFactory() {}

public:
    JsonRpcRequest::Ptr buildRequest()
    {
        auto request = std::make_shared<JsonRpcRequest>();
        request->setId(nextId());
        return request;
    }

    JsonRpcRequest::Ptr buildRequest(const std::string& _method, const Json::Value& _params)
    {
        auto request = buildRequest();
        request->setMethod(_method);
        request->setParams(_params);
        return request;
    }

    int64_t nextId()
    {
        int64_t _id = ++id;
        return _id;
    }

private:
    std::atomic<int64_t> id{0};
};

}  // namespace jsonrpc
}  // namespace cppsdk
}  // namespace bcos