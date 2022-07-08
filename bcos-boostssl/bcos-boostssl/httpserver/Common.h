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
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>


#define HTTP_LISTEN(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE(m_moduleName) << "[HTTP][LISTEN]"
#define HTTP_SESSION(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE(m_moduleName) << "[HTTP][SESSION]"
#define HTTP_SERVER(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE(m_moduleName) << "[HTTP][SERVER]"
#define HTTP_STREAM(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE(m_moduleName) << "[HTTP][STREAM]"


namespace bcos
{
namespace boostssl
{
namespace http
{
class HttpStream;
using HttpRequest = boost::beast::http::request<boost::beast::http::string_body>;
using HttpResponse = boost::beast::http::response<boost::beast::http::string_body>;
using HttpRequestPtr = std::shared_ptr<HttpRequest>;
using HttpResponsePtr = std::shared_ptr<HttpResponse>;
using HttpReqHandler =
    std::function<void(const std::string_view req, std::function<void(std::string_view resp)>)>;
using WsUpgradeHandler =
    std::function<void(std::shared_ptr<HttpStream>, HttpRequest&&, std::shared_ptr<std::string>)>;

static const int PARSER_BODY_LIMITATION = 100 * 1024 * 1024;
}  // namespace http
}  // namespace boostssl
}  // namespace bcos
