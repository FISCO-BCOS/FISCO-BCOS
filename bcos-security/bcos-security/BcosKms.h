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
 */
/**
 * @brief : BcosKms for disk encrytion
 * @author: jimmyshi
 * @date: 2018-12-03
 */
#pragma once
#include "Common.h"
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <cstdlib>
#include <memory>
#include <string>

namespace Json
{
class Value;
}

namespace bcos
{
namespace security
{
#define KC_LOG(_OBV)             \
    BCOS_LOG(_OBV) << "[g:null]" \
                   << "[p:null][KeyManager]"

class BcosKmsHttpClientInterface
{
public:
    using Ptr = std::shared_ptr<BcosKmsHttpClientInterface>;
    virtual ~BcosKmsHttpClientInterface(){};
    virtual void connect() = 0;
    virtual void close() = 0;
    virtual Json::Value callMethod(const std::string& _method, Json::Value _params) = 0;
};

class BcosKmsHttpClient : public BcosKmsHttpClientInterface
{
public:
    using Ptr = std::shared_ptr<BcosKmsHttpClient>;

    BcosKmsHttpClient(const std::string& _ip, int _port);
    ~BcosKmsHttpClient();
    void connect() override;
    void close() override;
    Json::Value callMethod(const std::string& _method, Json::Value _params) override;

private:
    std::string m_ip;
    int m_port;
    boost::asio::io_context m_ioc;
    boost::asio::ip::tcp::socket m_socket;
    mutable SharedMutex x_clinetSocket;
};

class BcosKms
{
public:
    using Ptr = std::shared_ptr<BcosKms>;

    BcosKms(){};
    virtual ~BcosKms(){};
    virtual const bytes getDataKey(const std::string& _cipherDataKey, const bool isSMCrypto);
    void setIpPort(const std::string& _ip, int _port);
    const std::string url() { return m_ip + ":" + std::to_string(m_port); }
    void setKcClient(BcosKmsHttpClientInterface::Ptr _kcclient) { m_kcclient = _kcclient; };
    bytes uniformDataKey(const bytes& _readableDataKey, const bool isSMCrypto);

    void clearCache()
    {
        m_lastQueryCipherDataKey.clear();
        m_lastRcvDataKey.clear();
    }

private:
    std::string m_ip;
    int m_port;
    std::string m_url;

    BcosKmsHttpClientInterface::Ptr m_kcclient = nullptr;

    // Query cache
    std::string m_lastQueryCipherDataKey;
    bytes m_lastRcvDataKey;
};

}  // namespace security

}  // namespace bcos