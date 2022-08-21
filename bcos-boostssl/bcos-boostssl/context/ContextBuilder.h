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
 * @file ContextBuilder.h
 * @author: octopus
 * @date 2021-06-14
 */
#pragma once
#include <bcos-boostssl/context/ContextConfig.h>
#include <boost/asio/ssl.hpp>
#include <boost/filesystem.hpp>

namespace bcos
{
namespace boostssl
{
namespace context
{
class ContextBuilder
{
public:
    using Ptr = std::shared_ptr<ContextBuilder>;
    using ConstPtr = std::shared_ptr<const ContextBuilder>;

public:
    std::shared_ptr<std::string> readFileContent(boost::filesystem::path const& _file);

    std::string moduleName() { return m_moduleName; }
    void setModuleName(std::string _moduleName) { m_moduleName = _moduleName; }

public:
    std::shared_ptr<boost::asio::ssl::context> buildSslContext(
        bool _server, const std::string& _configPath);
    std::shared_ptr<boost::asio::ssl::context> buildSslContext(
        bool _server, const ContextConfig& _contextConfig);

private:
    std::shared_ptr<boost::asio::ssl::context> buildSslContext(
        const ContextConfig::CertConfig& _certConfig);
    std::shared_ptr<boost::asio::ssl::context> buildSslContext(
        bool _server, const ContextConfig::SMCertConfig& _smCertConfig);
    std::shared_ptr<boost::asio::ssl::context> buildSslContextByCertContent(
        const ContextConfig::CertConfig& _certConfig);
    std::shared_ptr<boost::asio::ssl::context> buildSslContextByCertContent(
        bool _server, const ContextConfig::SMCertConfig& _smCertConfig);

private:
    std::string m_moduleName = "DEFAULT";
};

}  // namespace context
}  // namespace boostssl
}  // namespace bcos
