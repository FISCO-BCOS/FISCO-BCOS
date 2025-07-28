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
 * @file Common.h
 * @author: octopus
 * @date 2021-07-08
 */
#pragma once

#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/vector_body.hpp>


#define HTTP_LISTEN(LEVEL) BCOS_LOG(LEVEL) << "[HTTP][LISTEN]"
#define HTTP_SESSION(LEVEL) BCOS_LOG(LEVEL) << "[HTTP][SESSION]"
#define HTTP_SERVER(LEVEL) BCOS_LOG(LEVEL) << "[HTTP][SERVER]"
#define HTTP_STREAM(LEVEL) BCOS_LOG(LEVEL) << "[HTTP][STREAM]"


namespace bcos::boostssl::http
{
class HttpStream;
using HttpRequest = boost::beast::http::request<boost::beast::http::string_body>;
using HttpResponse = boost::beast::http::response<boost::beast::http::vector_body<bcos::byte>>;
using HttpRequestPtr = std::shared_ptr<HttpRequest>;
using HttpResponsePtr = std::shared_ptr<HttpResponse>;
using HttpReqHandler =
    std::function<void(const std::string_view req, std::function<void(bcos::bytes)>)>;
using WsUpgradeHandler =
    std::function<void(std::shared_ptr<HttpStream>, HttpRequest&&, std::shared_ptr<std::string>)>;

// cors config
struct CorsConfig
{
    bool enableCORS{true};
    bool allowCredentials{true};

    // bool allowCredentials{true};
    std::string allowedOrigins{"*"};
    std::string allowedMethods{"GET, POST, OPTIONS"};
    std::string allowedHeaders{"Content-Type, Authorization, X-Requested-With"};
    int maxAge{86400};

    std::string toString() const
    {
        return std::string("CorsConfig{") + "enableCORS=" + (enableCORS ? "true" : "false") +
               ", allowedOrigins=" + allowedOrigins + ", allowedMethods=" + allowedMethods +
               ", allowedHeaders=" + allowedHeaders + ", maxAge=" + std::to_string(maxAge) +
               ", allowCredentials=" + (allowCredentials ? "true" : "false") + "}}";
    }
};
}  // namespace bcos::boostssl::http
