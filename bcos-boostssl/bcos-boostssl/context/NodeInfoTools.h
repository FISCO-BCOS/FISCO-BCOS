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
 * @file NodeInfoTools.h
 * @author: lucasli
 * @date 2022-03-07
 */
#pragma once
#include <openssl/x509.h>
#include <boost/asio/ssl.hpp>
#include <functional>
#include <memory>

namespace bcos
{
namespace boostssl
{
namespace context
{
static std::string m_moduleName = "DEFAULT";

class NodeInfoTools
{
public:
    // register the function fetch pub hex from the cert
    static std::function<bool(const std::string& priKey, std::string& pubHex)>
    initCert2PubHexHandler();
    // register the function fetch public key from the ssl context
    static std::function<bool(X509* cert, std::string& pubHex)> initSSLContextPubHexHandler();

    static std::function<bool(bool, boost::asio::ssl::verify_context&)> newVerifyCallback(
        std::shared_ptr<std::string> nodeIDOut);

    static std::string moduleName() { return m_moduleName; }
    static void setModuleName(std::string _moduleName) { m_moduleName = _moduleName; }
};

}  // namespace context
}  // namespace boostssl
}  // namespace bcos
