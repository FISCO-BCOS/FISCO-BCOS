/**
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
 * @brief test for HttpSession
 * @file HttpSessionTest.cpp
 * @author: octopus
 * @date 2025-07-29
 */

#include <bcos-boostssl/httpserver/HttpSession.h>
#include <bcos-rpc/RpcFactory.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <sstream>
#include <optional>

#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::tool;
using namespace bcos::rpc;
using namespace bcos::boostssl;
using namespace bcos::boostssl::http;


struct HttpSessionTestFixture
{
    HttpSessionTestFixture()
    {
        // 初始化测试数据
        defaultCorsConfig = CorsConfig{};
        disabledCorsConfig = CorsConfig{.enableCORS = false,
            .allowCredentials = true,
            .allowedOrigins = "*",
            .allowedMethods = "GET, POST, OPTIONS",
            .allowedHeaders = "Content-Type, Authorization, X-Requested-With",
            .maxAge = 86400};

        customCorsConfig = CorsConfig{.enableCORS = true,
            .allowCredentials = false,
            .allowedOrigins = "https://example.com",
            .allowedMethods = "GET, POST, PUT, DELETE",
            .allowedHeaders = "Content-Type, Authorization, X-Custom-Header",
            .maxAge = 3600};

        BOOST_TEST_MESSAGE("Setup fixture");
    }

    ~HttpSessionTestFixture() { BOOST_TEST_MESSAGE("Teardown fixture"); }

    // 创建测试用的 HttpSession 对象
    HttpSession session{10240000, CorsConfig{}};

    CorsConfig defaultCorsConfig;
    CorsConfig disabledCorsConfig;
    CorsConfig customCorsConfig;
    std::optional<std::string> capturedMethod;
    std::optional<std::string> capturedTarget;

    // 辅助函数：将字符串转换为 bytes
    bcos::bytes stringToBytes(const std::string& str)
    {
        bcos::bytes result;
        result.reserve(str.size());
        for (char c : str)
        {
            result.push_back(static_cast<bcos::byte>(c));
        }
        return result;
    }

    // 辅助函数：将 bytes 转换为字符串
    std::string bytesToString(const bcos::bytes& bytes)
    {
        std::string result;
        result.reserve(bytes.size());
        for (bcos::byte b : bytes)
        {
            result.push_back(static_cast<char>(b));
        }
        return result;
    }

    // 辅助函数：检查响应的基本属性
    void checkBasicResponseProperties(const HttpResponsePtr& response,
        boost::beast::http::status expectedStatus, bool expectedKeepAlive, unsigned expectedVersion)
    {
        BOOST_REQUIRE(response != nullptr);
        BOOST_CHECK_EQUAL(response->result(), expectedStatus);
        BOOST_CHECK_EQUAL(response->keep_alive(), expectedKeepAlive);
        BOOST_CHECK_EQUAL(response->version(), expectedVersion);

        // 验证服务器头
        auto serverHeader = response->find(boost::beast::http::field::server);
        BOOST_REQUIRE(serverHeader != response->end());
        BOOST_CHECK_EQUAL(serverHeader->value(), BOOST_BEAST_VERSION_STRING);

        // 验证内容类型
        auto contentTypeHeader = response->find(boost::beast::http::field::content_type);
        BOOST_REQUIRE(contentTypeHeader != response->end());
        BOOST_CHECK_EQUAL(contentTypeHeader->value(), "application/json");
    }
};

BOOST_FIXTURE_TEST_SUITE(HttpSessionTest, HttpSessionTestFixture)


BOOST_AUTO_TEST_CASE(test_loadWeb3ConfigTest)
{
    {
        std::string strConfig = R"(
            [web3_rpc]
                enable=false
                listen_ip=127.0.0.1
                listen_port=8545
                thread_count=16
                ; 300s
                filter_timeout=300
                filter_max_process_block=10
                batch_request_size_limit=8
                ;request body size limit for web3 rpc, default is 10MB
                request_body_size_limit=10240000
                ; cors config for web3 rpc
                enable_cors=true
                cors_allow_credentials=true
                cors_allowed_origins=*
                cors_allowed_methods=GET, POST, OPTIONS
                cors_allowed_headers=Content-Type, Authorization, X-Requested-With
                cors_max_age=86400
            )";

        std::istringstream iss(strConfig);
        boost::property_tree::ptree pt;
        boost::property_tree::ini_parser::read_ini(iss, pt);

        auto nodeConfig = std::make_shared<NodeConfig>();
        nodeConfig->loadConfig(pt);

        BOOST_CHECK_EQUAL(nodeConfig->web3CorsAllowCredentials(), true);
        BOOST_CHECK_EQUAL(nodeConfig->web3CorsAllowedOrigins(), "*");
        BOOST_CHECK_EQUAL(nodeConfig->web3CorsAllowedMethods(), "GET, POST, OPTIONS");
        BOOST_CHECK_EQUAL(
            nodeConfig->web3CorsAllowedHeaders(), "Content-Type, Authorization, X-Requested-With");
        BOOST_CHECK_EQUAL(nodeConfig->web3CorsMaxAge(), 86400);
        BOOST_CHECK_EQUAL(nodeConfig->web3EnableCors(), true);
        BOOST_CHECK_EQUAL(nodeConfig->web3HttpBodySizeLimit(), 10240000);

        auto rpcFactory = std::make_shared<RpcFactory>("1", nullptr, nullptr, nullptr);
        auto wsConfig = rpcFactory->initWeb3RpcServiceConfig(nodeConfig);
        BOOST_CHECK_EQUAL(wsConfig->corsConfig().allowCredentials, true);
        BOOST_CHECK_EQUAL(wsConfig->corsConfig().allowedOrigins, "*");
        BOOST_CHECK_EQUAL(wsConfig->corsConfig().allowedMethods, "GET, POST, OPTIONS");
        BOOST_CHECK_EQUAL(
            wsConfig->corsConfig().allowedHeaders, "Content-Type, Authorization, X-Requested-With");
        BOOST_CHECK_EQUAL(wsConfig->corsConfig().maxAge, 86400);
    }

    {
        std::string strConfig = R"(
            [web3_rpc]
                enable=false
                listen_ip=127.0.0.1
                listen_port=8545
                thread_count=16
                ; 300s
                filter_timeout=300
                filter_max_process_block=10
                batch_request_size_limit=8
                ;request body size limit for web3 rpc, default is 10MB
                request_body_size_limit=10240001
                ; cors config for web3 rpc
                enable_cors=false
                cors_allow_credentials=false
                cors_allowed_origins=potos.hk
                cors_allowed_methods=OPTIONS
                cors_allowed_headers= Authorization, X-Requested-With
                cors_max_age=-1
            )";

        std::istringstream iss(strConfig);
        boost::property_tree::ptree pt;
        boost::property_tree::ini_parser::read_ini(iss, pt);

        auto nodeConfig = std::make_shared<NodeConfig>();
        nodeConfig->loadConfig(pt);

        BOOST_CHECK_EQUAL(nodeConfig->web3CorsAllowCredentials(), false);
        BOOST_CHECK_EQUAL(nodeConfig->web3CorsAllowedOrigins(), "potos.hk");
        BOOST_CHECK_EQUAL(nodeConfig->web3CorsAllowedMethods(), "OPTIONS");
        BOOST_CHECK_EQUAL(nodeConfig->web3CorsAllowedHeaders(), "Authorization, X-Requested-With");
        BOOST_CHECK_EQUAL(nodeConfig->web3CorsMaxAge(), -1);
        BOOST_CHECK_EQUAL(nodeConfig->web3HttpBodySizeLimit(), 10240001);

        auto rpcFactory = std::make_shared<RpcFactory>("1", nullptr, nullptr, nullptr);
        auto wsConfig = rpcFactory->initWeb3RpcServiceConfig(nodeConfig);
        BOOST_CHECK_EQUAL(wsConfig->corsConfig().allowCredentials, false);
        BOOST_CHECK_EQUAL(wsConfig->corsConfig().allowedOrigins, "potos.hk");
        BOOST_CHECK_EQUAL(wsConfig->corsConfig().allowedMethods, "OPTIONS");
        BOOST_CHECK_EQUAL(wsConfig->corsConfig().allowedHeaders, "Authorization, X-Requested-With");
        BOOST_CHECK_EQUAL(wsConfig->corsConfig().maxAge, -1);
    }
}

BOOST_AUTO_TEST_CASE(test_buildHttpRespTest)
{
    {
        // Arrange
        auto status = boost::beast::http::status::ok;
        bool keepAlive = true;
        unsigned version = 11;
        std::string testContent = R"({"message": "Hello World"})";
        bcos::bytes content = stringToBytes(testContent);

        // Act
        auto response = session.buildHttpResp(
            status, keepAlive, version, std::move(content), defaultCorsConfig);

        // Assert
        checkBasicResponseProperties(response, status, keepAlive, version);

        // 验证响应体
        std::string responseBody = bytesToString(response->body());
        BOOST_CHECK_EQUAL(responseBody, testContent);
    }

    {
        struct StatusTestCase
        {
            boost::beast::http::status status;
            std::string description;
        };

        std::vector<StatusTestCase> testCases = {{boost::beast::http::status::ok, "200 OK"},
            {boost::beast::http::status::bad_request, "400 Bad Request"},
            {boost::beast::http::status::unauthorized, "401 Unauthorized"},
            {boost::beast::http::status::not_found, "404 Not Found"},
            {boost::beast::http::status::internal_server_error, "500 Internal Server Error"}};

        bcos::bytes emptyContent;

        for (const auto& testCase : testCases)
        {
            BOOST_TEST_CONTEXT("Testing status: " << testCase.description)
            {
                auto response = session.buildHttpResp(
                    testCase.status, true, 11, bcos::bytes(emptyContent), defaultCorsConfig);

                BOOST_CHECK_EQUAL(response->result(), testCase.status);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(test_corsEnabledTest)
{
    bcos::bytes content = stringToBytes("test");

    auto response = session.buildHttpResp(
        boost::beast::http::status::ok, true, 11, std::move(content), customCorsConfig);

    // 验证所有CORS头部都被设置
    auto originHeader = response->find(boost::beast::http::field::access_control_allow_origin);
    BOOST_REQUIRE(originHeader != response->end());
    BOOST_CHECK_EQUAL(originHeader->value(), "https://example.com");

    auto methodsHeader = response->find(boost::beast::http::field::access_control_allow_methods);
    BOOST_REQUIRE(methodsHeader != response->end());
    BOOST_CHECK_EQUAL(methodsHeader->value(), "GET, POST, PUT, DELETE");

    auto headersHeader = response->find(boost::beast::http::field::access_control_allow_headers);
    BOOST_REQUIRE(headersHeader != response->end());
    BOOST_CHECK_EQUAL(headersHeader->value(), "Content-Type, Authorization, X-Custom-Header");

    auto maxAgeHeader = response->find(boost::beast::http::field::access_control_max_age);
    BOOST_REQUIRE(maxAgeHeader != response->end());
    BOOST_CHECK_EQUAL(maxAgeHeader->value(), "3600");

    auto credentialsHeader =
        response->find(boost::beast::http::field::access_control_allow_credentials);
    BOOST_REQUIRE(credentialsHeader != response->end());
    BOOST_CHECK_EQUAL(credentialsHeader->value(), "false");
}

// 测试用例6：CORS禁用时的头部测试
BOOST_AUTO_TEST_CASE(test_corsDisabledTest)
{
    bcos::bytes content = stringToBytes("test");

    auto response = session.buildHttpResp(
        boost::beast::http::status::ok, true, 11, std::move(content), disabledCorsConfig);

    // 验证CORS头部都没有被设置
    BOOST_CHECK(
        response->find(boost::beast::http::field::access_control_allow_origin) == response->end());
    BOOST_CHECK(
        response->find(boost::beast::http::field::access_control_allow_methods) == response->end());
    BOOST_CHECK(
        response->find(boost::beast::http::field::access_control_allow_headers) == response->end());
    BOOST_CHECK(
        response->find(boost::beast::http::field::access_control_max_age) == response->end());
    BOOST_CHECK(response->find(boost::beast::http::field::access_control_allow_credentials) ==
                response->end());
}

BOOST_AUTO_TEST_CASE(test_httpOptionsMethodTest) {}

BOOST_AUTO_TEST_CASE(test_httpReqHandlerReceivesHeaders)
{
    HttpRequest request;
    request.version(11);
    request.method(boost::beast::http::verb::post);
    request.target("/authrpc");
    request.keep_alive(true);
    request.set(boost::beast::http::field::content_type, "application/json");
    request.set(boost::beast::http::field::authorization, "Bearer test-token");
    request.body() = R"({"jsonrpc":"2.0","method":"eth_chainId","params":[]})";
    request.prepare_payload();

    Queue queue;
    HttpResponsePtr response;
    queue.setSender([&response](HttpResponsePtr _response) { response = std::move(_response); });
    session.setQueue(std::move(queue));

    session.setRequestHandler([this](const bcos::boostssl::http::HttpRequest& req,
                                std::function<void(bcos::bytes)> sender) {
        BOOST_CHECK_EQUAL(req.body(), R"({"jsonrpc":"2.0","method":"eth_chainId","params":[]})");
        capturedMethod = std::string(req.method_string());
        capturedTarget = std::string(req.target());
        sender(stringToBytes(R"({"result":"ok"})"));
    });

    session.handleRequest(request);

    BOOST_REQUIRE(capturedMethod.has_value());
    BOOST_CHECK_EQUAL(*capturedMethod, "POST");
    BOOST_CHECK_EQUAL(*capturedTarget, "/authrpc");
    BOOST_REQUIRE(response != nullptr);
    BOOST_CHECK_EQUAL(response->result(), boost::beast::http::status::ok);
}

BOOST_AUTO_TEST_CASE(test_httpReqHandlerMapsJwtUnauthorizedTo401)
{
    HttpRequest request;
    request.version(11);
    request.method(boost::beast::http::verb::post);
    request.target("/authrpc");
    request.keep_alive(true);
    request.body() = R"({"jsonrpc":"2.0","method":"engine_forkchoiceUpdatedV3","params":[],"id":1})";
    request.prepare_payload();

    Queue queue;
    HttpResponsePtr response;
    queue.setSender([&response](HttpResponsePtr _response) { response = std::move(_response); });
    session.setQueue(std::move(queue));

    session.setRequestHandler([](const bcos::boostssl::http::HttpRequest&, std::function<void(bcos::bytes)> sender) {
        sender(bcos::bytes{'{', '"', 'j', 's', 'o', 'n', 'r', 'p', 'c', '"', ':', '"', '2', '.',
            '0', '"', ',', '"', 'i', 'd', '"', ':', '1', ',', '"', 'e', 'r', 'r', 'o', 'r', '"',
            ':', '{', '"', 'c', 'o', 'd', 'e', '"', ':', '-', '3', '2', '0', '1', '0', ',',
            '"', 'm', 'e', 's', 's', 'a', 'g', 'e', '"', ':', '"', 'u', 'n', 'a', 'u', 't', 'h',
            'o', 'r', 'i', 'z', 'e', 'd', '"', '}', '}'});
    });

    session.handleRequest(request);

    BOOST_REQUIRE(response != nullptr);
    BOOST_CHECK_EQUAL(response->result(), boost::beast::http::status::unauthorized);
}

BOOST_AUTO_TEST_CASE(test_httpReqHandlerMapsJwtForbiddenTo403)
{
    HttpRequest request;
    request.version(11);
    request.method(boost::beast::http::verb::post);
    request.target("/authrpc");
    request.keep_alive(true);
    request.body() = R"({"jsonrpc":"2.0","method":"engine_forkchoiceUpdatedV3","params":[],"id":1})";
    request.prepare_payload();

    Queue queue;
    HttpResponsePtr response;
    queue.setSender([&response](HttpResponsePtr _response) { response = std::move(_response); });
    session.setQueue(std::move(queue));

    session.setRequestHandler([](const bcos::boostssl::http::HttpRequest&, std::function<void(bcos::bytes)> sender) {
        sender(bcos::bytes{'{', '"', 'j', 's', 'o', 'n', 'r', 'p', 'c', '"', ':', '"', '2', '.',
            '0', '"', ',', '"', 'i', 'd', '"', ':', '1', ',', '"', 'e', 'r', 'r', 'o', 'r', '"',
            ':', '{', '"', 'c', 'o', 'd', 'e', '"', ':', '-', '3', '2', '0', '1', '1', ',',
            '"', 'm', 'e', 's', 's', 'a', 'g', 'e', '"', ':', '"', 'f', 'o', 'r', 'b', 'i', 'd',
            'd', 'e', 'n', '"', '}', '}'});
    });

    session.handleRequest(request);

    BOOST_REQUIRE(response != nullptr);
    BOOST_CHECK_EQUAL(response->result(), boost::beast::http::status::forbidden);
}

BOOST_AUTO_TEST_CASE(test_httpReqHandlerKeeps200ForNonJwtJsonRpcError)
{
    HttpRequest request;
    request.version(11);
    request.method(boost::beast::http::verb::post);
    request.target("/jsonrpc");
    request.keep_alive(true);
    request.body() = R"({"jsonrpc":"2.0","method":"eth_call","params":[],"id":1})";
    request.prepare_payload();

    Queue queue;
    HttpResponsePtr response;
    queue.setSender([&response](HttpResponsePtr _response) { response = std::move(_response); });
    session.setQueue(std::move(queue));

    session.setRequestHandler([](const bcos::boostssl::http::HttpRequest&, std::function<void(bcos::bytes)> sender) {
        auto payload = std::string(R"({"jsonrpc":"2.0","id":1,"error":{"code":)") +
                       std::to_string(bcos::rpc::MethodNotFound) + R"(,"message":"not found"}})";
        sender(bcos::bytes(payload.begin(), payload.end()));
    });

    session.handleRequest(request);

    BOOST_REQUIRE(response != nullptr);
    BOOST_CHECK_EQUAL(response->result(), boost::beast::http::status::ok);
}

BOOST_AUTO_TEST_SUITE_END()
