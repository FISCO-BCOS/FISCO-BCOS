/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file SecureInitializer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#include "SecureInitializer.h"
#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Common.h>
#include <openssl/engine.h>
#include <openssl/rsa.h>
#include <boost/algorithm/string/replace.hpp>
#include <iostream>

using namespace dev;
using namespace dev::initializer;

struct ConfigResult
{
    KeyPair keyPair;
    std::shared_ptr<boost::asio::ssl::context> sslContext;
};


ConfigResult initOriginConfig(const boost::property_tree::ptree& pt)
{
    std::string dataPath = pt.get<std::string>("secure.data_path", "./conf/");
    std::string originDataPath =
        pt.get<std::string>("secure.origin_data_path", dataPath + std::string("/originCert/"));
    std::string key = originDataPath + pt.get<std::string>("secure.originKey", "node.key");
    std::string cert = originDataPath + pt.get<std::string>("secure.originCert", "node.crt");
    std::string caCert = originDataPath + pt.get<std::string>("secure.originca_cert", "ca.crt");
    std::string caPath = originDataPath + pt.get<std::string>("secure.ca_path", "");
    bytes keyContent;
    if (!key.empty())
    {
        try
        {
            keyContent = contents(key);
        }
        catch (std::exception& e)
        {
            INITIALIZER_LOG(ERROR)
                << "[#SecureInitializer::initConfig] open privateKey failed: [file]: " << key;

            BOOST_THROW_EXCEPTION(PrivateKeyError());
        }
    }

    std::shared_ptr<EC_KEY> ecKey;
    if (!keyContent.empty())
    {
        try
        {
            INITIALIZER_LOG(DEBUG) << "[#SecureInitializer::initConfig] load existing privateKey.";
            std::shared_ptr<BIO> bioMem(BIO_new(BIO_s_mem()), [&](BIO* p) { BIO_free(p); });
            BIO_write(bioMem.get(), keyContent.data(), keyContent.size());

            std::shared_ptr<EVP_PKEY> evpPKey(
                PEM_read_bio_PrivateKey(bioMem.get(), NULL, NULL, NULL),
                [](EVP_PKEY* p) { EVP_PKEY_free(p); });

            if (!evpPKey)
            {
                BOOST_THROW_EXCEPTION(PrivateKeyError());
            }

            ecKey.reset(EVP_PKEY_get1_EC_KEY(evpPKey.get()), [](EC_KEY* p) { EC_KEY_free(p); });
        }
        catch (dev::Exception& e)
        {
            INITIALIZER_LOG(ERROR)
                << "[#SecureInitializer::initConfig] parse privateKey failed: [EINFO]: "
                << e.what();
            BOOST_THROW_EXCEPTION(e);
        }
    }
    else
    {
        INITIALIZER_LOG(ERROR)
            << "[#SecureInitializer::initConfig] channelserver privatekey not exists!";
        BOOST_THROW_EXCEPTION(PrivateKeyNotExists());
    }

    std::shared_ptr<const BIGNUM> ecPrivateKey(
        EC_KEY_get0_private_key(ecKey.get()), [](const BIGNUM* p) {});

    std::shared_ptr<char> privateKeyData(
        BN_bn2hex(ecPrivateKey.get()), [](char* p) { OPENSSL_free(p); });

    std::string keyHex(privateKeyData.get());
    if (keyHex.size() != 64u)
    {
        throw std::invalid_argument("Private Key file error! Missing bytes!");
    }

    KeyPair keyPair = KeyPair(Secret(keyHex));

    std::shared_ptr<boost::asio::ssl::context> sslContext =
        std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);

    std::shared_ptr<EC_KEY> ecdh(
        EC_KEY_new_by_curve_name(NID_secp256k1), [](EC_KEY* p) { EC_KEY_free(p); });
    SSL_CTX_set_tmp_ecdh(sslContext->native_handle(), ecdh.get());

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_none);
    INITIALIZER_LOG(DEBUG) << "[#SecureInitializer::initConfig] [nodeID]: " << keyPair.pub().hex();

    boost::asio::const_buffer keyBuffer(keyContent.data(), keyContent.size());
    sslContext->use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);

    if (!cert.empty() && !contents(cert).empty())
    {
        INITIALIZER_LOG(DEBUG) << "[#SecureInitializer::initConfig] use user certificate: [file]: "
                               << cert;
        sslContext->use_certificate_chain_file(cert);
        sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer);
    }
    else
    {
        INITIALIZER_LOG(ERROR) << "[#SecureInitializer::initConfig] certificate not exists!";
        BOOST_THROW_EXCEPTION(CertificateNotExists());
    }

    auto caCertContent = contents(caCert);
    if (!caCert.empty() && !caCertContent.empty())
    {
        INITIALIZER_LOG(DEBUG) << "[#SecureInitializer::initConfig] use ca certificate: [file]: "
                               << caCert;

        sslContext->add_certificate_authority(
            boost::asio::const_buffer(caCertContent.data(), caCertContent.size()));
        sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer);
    }
    else
    {
        INITIALIZER_LOG(ERROR) << "[#SecureInitializer::initConfig] CA Certificate not exists!";
        BOOST_THROW_EXCEPTION(CertificateNotExists());
    }

    if (!caPath.empty())
    {
        INITIALIZER_LOG(DEBUG) << "[#SecureInitializer::initConfig] use ca: [path]: " << caPath;

        sslContext->add_verify_path(caPath);
        sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer);
    }

    return ConfigResult{keyPair, sslContext};
}

ConfigResult initGmConfig(const boost::property_tree::ptree& pt)
{
    std::string dataPath = pt.get<std::string>("secure.data_path", "./conf/");
    std::string key = dataPath + pt.get<std::string>("secure.key", "gmnode.key");
    std::string enKey = dataPath + pt.get<std::string>("secure.enkey", "gmennode.key");
    std::string enCert = dataPath + pt.get<std::string>("secure.enCert", "gmennode.crt");
    std::string cert = dataPath + pt.get<std::string>("secure.cert", "gmnode.crt");
    std::string caCert = dataPath + pt.get<std::string>("secure.ca_cert", "gmca.crt");
    std::string caPath = dataPath + pt.get<std::string>("secure.ca_path", "");
    bytes keyContent;
    if (!key.empty())
    {
        try
        {
            keyContent = contents(key);
        }
        catch (std::exception& e)
        {
            INITIALIZER_LOG(ERROR)
                << "[#SecureInitializer::initConfig] open privateKey failed: [file]: " << key;

            BOOST_THROW_EXCEPTION(PrivateKeyError());
        }
    }

    std::shared_ptr<EC_KEY> ecKey;
    if (!keyContent.empty())
    {
        try
        {
            INITIALIZER_LOG(DEBUG) << "[#SecureInitializer::initConfig] load existing privateKey.";
            std::shared_ptr<BIO> bioMem(BIO_new(BIO_s_mem()), [&](BIO* p) { BIO_free(p); });
            BIO_write(bioMem.get(), keyContent.data(), keyContent.size());

            std::shared_ptr<EVP_PKEY> evpPKey(
                PEM_read_bio_PrivateKey(bioMem.get(), NULL, NULL, NULL),
                [](EVP_PKEY* p) { EVP_PKEY_free(p); });

            if (!evpPKey)
            {
                BOOST_THROW_EXCEPTION(PrivateKeyError());
            }

            ecKey.reset(EVP_PKEY_get1_EC_KEY(evpPKey.get()), [](EC_KEY* p) { EC_KEY_free(p); });
        }
        catch (dev::Exception& e)
        {
            INITIALIZER_LOG(ERROR)
                << "[#SecureInitializer::initConfig] parse privateKey failed: [EINFO]: "
                << e.what();
            BOOST_THROW_EXCEPTION(e);
        }
    }
    else
    {
        INITIALIZER_LOG(ERROR) << "[#SecureInitializer::initConfig] privatekey not exists!";
        BOOST_THROW_EXCEPTION(PrivateKeyNotExists());
    }

    std::shared_ptr<const BIGNUM> ecPrivateKey(
        EC_KEY_get0_private_key(ecKey.get()), [](const BIGNUM* p) {});

    std::shared_ptr<char> privateKeyData(
        BN_bn2hex(ecPrivateKey.get()), [](char* p) { OPENSSL_free(p); });

    std::string keyHex(privateKeyData.get());
    if (keyHex.size() != 64u)
    {
        throw std::invalid_argument("Private Key file error! Missing bytes!");
    }

    KeyPair keyPair = KeyPair(Secret(keyHex));

    std::shared_ptr<boost::asio::ssl::context> sslContext =
        std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);

    std::shared_ptr<EC_KEY> ecdh(
        EC_KEY_new_by_curve_name(NID_secp256k1), [](EC_KEY* p) { EC_KEY_free(p); });
    SSL_CTX_set_tmp_ecdh(sslContext->native_handle(), ecdh.get());

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_none);
    INITIALIZER_LOG(DEBUG) << "[#SecureInitializer::initConfig] [nodeID]: " << keyPair.pub().hex();

    boost::asio::const_buffer keyBuffer(keyContent.data(), keyContent.size());
    sslContext->use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);

    sslContext->use_certificate_file(enCert, boost::asio::ssl::context::file_format::pem);
    if (SSL_CTX_use_enc_PrivateKey_file(
            sslContext->native_handle(), enKey.c_str(), SSL_FILETYPE_PEM) > 0)
    {
        INITIALIZER_LOG(DEBUG)
            << "[#SecureInitializer::initConfig] use GM enc ca certificate: [file]: " << enKey;
    }
    else
    {
        INITIALIZER_LOG(ERROR)
            << "[#SecureInitializer::initConfig] GM enc ca certificate not exists!";
        BOOST_THROW_EXCEPTION(CertificateNotExists());
    }

    if (!cert.empty() && !contents(cert).empty())
    {
        INITIALIZER_LOG(DEBUG) << "[#SecureInitializer::initConfig] use user certificate: [file]: "
                               << cert;
        sslContext->use_certificate_chain_file(cert);
        sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer);
    }
    else
    {
        INITIALIZER_LOG(ERROR) << "[#SecureInitializer::initConfig] certificate not exists!";
        BOOST_THROW_EXCEPTION(CertificateNotExists());
    }

    auto caCertContent = contents(caCert);
    if (!caCert.empty() && !caCertContent.empty())
    {
        INITIALIZER_LOG(DEBUG) << "[#SecureInitializer::initConfig] use ca certificate: [file]: "
                               << caCert;

        sslContext->add_certificate_authority(
            boost::asio::const_buffer(caCertContent.data(), caCertContent.size()));
        sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer);
    }
    else
    {
        INITIALIZER_LOG(ERROR) << "[#SecureInitializer::initConfig] CA Certificate not exists!";
        BOOST_THROW_EXCEPTION(CertificateNotExists());
    }

    if (!caPath.empty())
    {
        INITIALIZER_LOG(DEBUG) << "[#SecureInitializer::initConfig] use ca: [path]: " << caPath;

        sslContext->add_verify_path(caPath);
        sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer);
    }

    return ConfigResult{keyPair, sslContext};
}

void SecureInitializer::initConfig(const boost::property_tree::ptree& pt)
{
    try
    {
        ConfigResult gmConfig = initGmConfig(pt);
        m_key = gmConfig.keyPair;
        m_sslContexts[Usage::Default] = gmConfig.sslContext;
        m_sslContexts[Usage::ForP2p] = gmConfig.sslContext;

        ConfigResult originConfig = initOriginConfig(pt);
        m_sslContexts[Usage::ForRpc] = originConfig.sslContext;
    }
    catch (Exception& e)
    {
        INITIALIZER_LOG(ERROR)
            << "[#SecureInitializer::initConfig] load verify file failed: [EINFO]: " << e.what();
        BOOST_THROW_EXCEPTION(e);
    }
}

std::shared_ptr<bas::context> SecureInitializer::SSLContext(Usage _usage)
{
    auto defaultP = m_sslContexts.find(Usage::Default);
    if (defaultP == m_sslContexts.end())
    {
        INITIALIZER_LOG(ERROR)
            << "[#SecureInitializer::SSLContext] SecureInitializer has not been initialied";
        BOOST_THROW_EXCEPTION(SecureInitializerNotInitConfig());
    }

    auto p = m_sslContexts.find(_usage);
    if (p != m_sslContexts.end())
        return p->second;

    // if not found, return default
    return defaultP->second;
}
