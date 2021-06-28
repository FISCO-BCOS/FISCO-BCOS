/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file SecureInitializer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#pragma once

#include "Common.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <map>

namespace bas = boost::asio::ssl;
namespace dev
{
namespace initializer
{
DEV_SIMPLE_EXCEPTION(SecureInitializerNotInitConfig);
DEV_SIMPLE_EXCEPTION(PrivateKeyError);
DEV_SIMPLE_EXCEPTION(PrivateKeyNotExists);
DEV_SIMPLE_EXCEPTION(CertificateError);
DEV_SIMPLE_EXCEPTION(CertificateNotExists);

class SecureInitializer : public std::enable_shared_from_this<SecureInitializer>
{
public:
    typedef std::shared_ptr<SecureInitializer> Ptr;

    enum class Usage
    {
        Default,
        ForP2P,
        ForRPC
    };

    void initConfig(const boost::property_tree::ptree& _pt);

    std::shared_ptr<bas::context> SSLContext(Usage _usage = Usage::Default);
    const KeyPair& keyPair() { return m_key; }

private:
    void completePath(std::string& _path);
    KeyPair m_key;
    std::map<Usage, std::shared_ptr<boost::asio::ssl::context>> m_sslContexts;

    void initConfigWithCrypto(const boost::property_tree::ptree& _pt);
    void initConfigWithSMCrypto(const boost::property_tree::ptree& pt);

    std::shared_ptr<bas::context> SSLContextWithCrypto(Usage _usage);
    std::shared_ptr<bas::context> SSLContextWithSMCrypto(Usage _usage);
};

class SslInitializer
{
public:
    std::shared_ptr<boost::asio::ssl::context> initSslContext();
    KeyPair loadKeyPair(bytes keyContent);
    bytes decryptKey(std::string key, bool diskEncryptionEnable, bool useHsm);
    void setCaCert(std::shared_ptr<boost::asio::ssl::context> sslContext, std::string caCert,
        std::string caPath);
#ifdef FISCO_SDF
    KeyPair loadKeyPairById(std::string cert, std::string keyId, std::string encKeyId);
    void useSwKey(
        std::shared_ptr<boost::asio::ssl::context> sslContext, std::string enKey, bytes keyContent);
    ENGINE* tryLoadEngine(const char* engine = "sdf");
    void useEngineKey(std::shared_ptr<boost::asio::ssl::context> sslContext, std::string keyId,
        std::string encKeyId);
#else
    void useKey(std::shared_ptr<boost::asio::ssl::context> sslContext, std::string enKey,
        std::string enCert, bytes keyContent);
#endif
#ifdef FISCO_SDF
private:
    void opensslDebugMessage(const std::string& _method);
    void useOpensslEngineKey(ENGINE* e, std::shared_ptr<boost::asio::ssl::context> sslContext,
        boost::asio::const_buffer keyBuffer, std::string keyName);
#endif
};

}  // namespace initializer
}  // namespace dev
