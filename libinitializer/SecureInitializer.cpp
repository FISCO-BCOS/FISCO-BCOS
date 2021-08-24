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

#include "SecureInitializer.h"
#include "libdevcrypto/CryptoInterface.h"
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcrypto/Common.h>
#include <libsecurity/EncryptedFile.h>
#include <openssl/engine.h>
#include <openssl/rsa.h>
#include <boost/algorithm/string/replace.hpp>
#include <iostream>

using namespace std;
using namespace dev;
using namespace dev::initializer;

void SecureInitializer::initConfigWithCrypto(const boost::property_tree::ptree& pt)
{
    std::string sectionName = "secure";
    if (pt.get_child_optional("network_security"))
    {
        sectionName = "network_security";
    }
    std::string dataPath = pt.get<std::string>(sectionName + ".data_path", "./conf/");
    std::string key = dataPath + "/" + pt.get<std::string>(sectionName + ".key", "node.key");
    std::string cert = dataPath + "/" + pt.get<std::string>(sectionName + ".cert", "node.crt");
    std::string caCert = dataPath + "/" + pt.get<std::string>(sectionName + ".ca_cert", "ca.crt");
    std::string caPath = dataPath + "/" + pt.get<std::string>(sectionName + ".ca_path", "");
    bytes keyContent;
    if (!key.empty())
    {
        try
        {
            if (g_BCOSConfig.diskEncryption.enable)
                keyContent = EncryptedFile::decryptContents(key);
            else
                keyContent = contents(key);
        }
        catch (std::exception& e)
        {
            INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializer")
                                   << LOG_DESC("open privateKey failed") << LOG_KV("file", key);
            ERROR_OUTPUT << LOG_BADGE("SecureInitializer") << LOG_DESC("open privateKey failed")
                         << LOG_KV("file", key) << std::endl;
            exit(1);
        }
    }

    std::shared_ptr<EC_KEY> ecKey;
    if (!keyContent.empty())
    {
        try
        {
            INITIALIZER_LOG(INFO) << LOG_BADGE("SecureInitializer")
                                  << LOG_DESC("loading privateKey");
            std::shared_ptr<BIO> bioMem(BIO_new(BIO_s_mem()), [&](BIO* p) { BIO_free(p); });
            BIO_write(bioMem.get(), keyContent.data(), keyContent.size());

            std::shared_ptr<EVP_PKEY> evpPKey(
                PEM_read_bio_PrivateKey(bioMem.get(), NULL, NULL, NULL),
                [](EVP_PKEY* p) { EVP_PKEY_free(p); });

            if (!evpPKey)
            {
                ERROR_OUTPUT << LOG_BADGE("SecureInitializer") << LOG_DESC("load privateKey failed")
                             << std::endl;
                exit(1);
            }

            ecKey.reset(EVP_PKEY_get1_EC_KEY(evpPKey.get()), [](EC_KEY* p) { EC_KEY_free(p); });
        }
        catch (dev::Exception& e)
        {
            INITIALIZER_LOG(ERROR)
                << LOG_BADGE("SecureInitializer") << LOG_DESC("parse privateKey failed")
                << LOG_KV("EINFO", boost::diagnostic_information(e));
            ERROR_OUTPUT << LOG_BADGE("SecureInitializer") << LOG_DESC("parse privateKey failed")
                         << LOG_KV("EINFO", boost::diagnostic_information(e)) << std::endl;
            exit(1);
        }
    }
    else
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializer")
                               << LOG_DESC("privateKey doesn't exist!");
        ERROR_OUTPUT << LOG_BADGE("SecureInitializer") << LOG_DESC("privateKey doesn't exist!")
                     << std::endl;
        exit(1);
    }

    std::shared_ptr<const BIGNUM> ecPrivateKey(
        EC_KEY_get0_private_key(ecKey.get()), [](const BIGNUM*) {});

    std::shared_ptr<char> privateKeyData(
        BN_bn2hex(ecPrivateKey.get()), [](char* p) { OPENSSL_free(p); });

    std::string keyHex(privateKeyData.get());
    if (keyHex.size() != 64u)
    {
        throw std::invalid_argument("Incompleted privateKey!");
    }
    m_key = KeyPair(Secret(keyHex));

    try
    {
        std::shared_ptr<boost::asio::ssl::context> sslContext =
            std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);

        std::shared_ptr<EC_KEY> ecdh(
            EC_KEY_new_by_curve_name(NID_secp256k1), [](EC_KEY* p) { EC_KEY_free(p); });
        SSL_CTX_set_tmp_ecdh(sslContext->native_handle(), ecdh.get());

        INITIALIZER_LOG(INFO) << LOG_BADGE("SecureInitializer") << LOG_DESC("get pub of node")
                              << LOG_KV("nodeID", m_key.pub().hex());

        boost::asio::const_buffer keyBuffer(keyContent.data(), keyContent.size());
        sslContext->use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);

        if (!cert.empty() && !contents(cert).empty())
        {
            INITIALIZER_LOG(INFO) << LOG_BADGE("SecureInitializer")
                                  << LOG_DESC("use user certificate") << LOG_KV("file", cert);
            sslContext->use_certificate_chain_file(cert);
            if (!SSL_CTX_get0_certificate(sslContext->native_handle()))
            {
                INITIALIZER_LOG(ERROR)
                    << LOG_BADGE("SecureInitializer")
                    << LOG_DESC("certificate load failed, please check") << LOG_KV("file", cert);
                ERROR_OUTPUT << LOG_BADGE("SecureInitializer")
                             << LOG_DESC("certificate load failed, please check")
                             << LOG_KV("file", cert) << std::endl;
                exit(1);
            }
        }
        else
        {
            INITIALIZER_LOG(ERROR)
                << LOG_BADGE("SecureInitializer") << LOG_DESC("certificate doesn't exist!");
            ERROR_OUTPUT << LOG_BADGE("SecureInitializer") << LOG_DESC("certificate doesn't exist!")
                         << std::endl;
            exit(1);
        }

        auto caCertContent = contents(caCert);
        if (!caCert.empty() && !caCertContent.empty())
        {
            INITIALIZER_LOG(INFO) << LOG_BADGE("SecureInitializer")
                                  << LOG_DESC("use ca certificate") << LOG_KV("file", caCert);
            sslContext->add_certificate_authority(
                boost::asio::const_buffer(caCertContent.data(), caCertContent.size()));
        }
        else
        {
            INITIALIZER_LOG(ERROR)
                << LOG_BADGE("SecureInitializer") << LOG_DESC("CA Certificate doesn't exist!");
            ERROR_OUTPUT << LOG_BADGE("SecureInitializer")
                         << LOG_DESC("CA Certificate doesn't exist!") << std::endl;
            exit(1);
        }

        if (!caPath.empty())
        {
            INITIALIZER_LOG(INFO) << LOG_BADGE("SecureInitializer") << LOG_DESC("use ca")
                                  << LOG_KV("file", caPath);
            sslContext->add_verify_path(caPath);
        }
        sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer |
                                    boost::asio::ssl::verify_fail_if_no_peer_cert);

        m_sslContexts[Usage::Default] = sslContext;
    }
    catch (Exception& e)
    {
        // TODO: catch in Initializer::init, delete this catch
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializer")
                               << LOG_DESC("load verify file failed")
                               << LOG_KV("EINFO", boost::diagnostic_information(e));
        ERROR_OUTPUT << LOG_BADGE("SecureInitializer") << LOG_DESC("load verify file failed")
                     << LOG_KV("EINFO", boost::diagnostic_information(e)) << std::endl;
        exit(1);
    }
}

std::shared_ptr<bas::context> SecureInitializer::SSLContextWithCrypto(Usage _usage)
{
    auto defaultP = m_sslContexts.find(Usage::Default);
    if (defaultP == m_sslContexts.end())
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializer")
                               << LOG_DESC("SecureInitializer has not been initialied");
        BOOST_THROW_EXCEPTION(SecureInitializerNotInitConfig());
    }

    auto p = m_sslContexts.find(_usage);
    if (p != m_sslContexts.end())
        return p->second;

    // if not found, return default
    return defaultP->second;
}

struct ConfigResult
{
    KeyPair keyPair;
    std::shared_ptr<boost::asio::ssl::context> sslContext;
};

ConfigResult initOriginConfig(const string& _dataPath)
{
    std::string originDataPath = _dataPath + "/origin_cert/";
    std::string key = originDataPath + "node.key";
    std::string cert = originDataPath + "node.crt";
    std::string caCert = originDataPath + "ca.crt";
    std::string caPath = originDataPath;
    bytes keyContent;
    if (!key.empty())
    {
        try
        {
            if (g_BCOSConfig.diskEncryption.enable)
                keyContent = EncryptedFile::decryptContents(key);
            else
                keyContent = contents(key);
        }
        catch (std::exception& e)
        {
            INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializer")
                                   << LOG_DESC("open privateKey failed") << LOG_KV("file", key);

            BOOST_THROW_EXCEPTION(PrivateKeyError());
        }
    }

    std::shared_ptr<EC_KEY> ecKey;
    if (!keyContent.empty())
    {
        try
        {
            INITIALIZER_LOG(DEBUG)
                << LOG_BADGE("SecureInitializer") << LOG_DESC("loading privateKey");
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
                << LOG_BADGE("SecureInitializer") << LOG_DESC("load privateKey failed")
                << LOG_KV("EINFO", boost::diagnostic_information(e));
            BOOST_THROW_EXCEPTION(e);
        }
    }
    else
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializer")
                               << LOG_DESC("channelserver privateKey doesn't exist!");
        BOOST_THROW_EXCEPTION(PrivateKeyNotExists());
    }

    std::shared_ptr<const BIGNUM> ecPrivateKey(
        EC_KEY_get0_private_key(ecKey.get()), [](const BIGNUM*) {});

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
    INITIALIZER_LOG(INFO) << LOG_BADGE("SecureInitializer") << LOG_DESC("get pub of node")
                          << LOG_KV("nodeID", keyPair.pub().hex());

    boost::asio::const_buffer keyBuffer(keyContent.data(), keyContent.size());
    sslContext->use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);

    if (!cert.empty() && !contents(cert).empty())
    {
        INITIALIZER_LOG(DEBUG) << LOG_BADGE("SecureInitializer") << LOG_DESC("use user certificate")
                               << LOG_KV("file", cert);
        sslContext->use_certificate_chain_file(cert);
        if (!SSL_CTX_get0_certificate(sslContext->native_handle()))
        {
            INITIALIZER_LOG(ERROR)
                << LOG_BADGE("SecureInitializer")
                << LOG_DESC("certificate load failed, please check") << LOG_KV("file", cert);
            ERROR_OUTPUT << LOG_BADGE("SecureInitializer")
                         << LOG_DESC("certificate load failed, please check")
                         << LOG_KV("file", cert) << std::endl;
            exit(1);
        }
    }
    else
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializer")
                               << LOG_DESC("certificate doesn't exist!");
        BOOST_THROW_EXCEPTION(CertificateNotExists());
    }

    auto caCertContent = contents(caCert);
    if (!caCert.empty() && !caCertContent.empty())
    {
        INITIALIZER_LOG(DEBUG) << LOG_BADGE("SecureInitializer") << LOG_DESC("use ca certificate")
                               << LOG_KV("file", caCert);

        sslContext->add_certificate_authority(
            boost::asio::const_buffer(caCertContent.data(), caCertContent.size()));
    }
    else
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializer")
                               << LOG_DESC("CA Certificate doesn't exist!");
        BOOST_THROW_EXCEPTION(CertificateNotExists());
    }

    if (!caPath.empty())
    {
        INITIALIZER_LOG(DEBUG) << LOG_BADGE("SecureInitializer") << LOG_DESC("use ca")
                               << LOG_KV("file", caPath);
        sslContext->add_verify_path(caPath);
    }
    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer |
                                boost::asio::ssl::verify_fail_if_no_peer_cert);
    return ConfigResult{keyPair, sslContext};
}

ConfigResult initGmConfig(const boost::property_tree::ptree& pt)
{
    std::string sectionName = "secure";
    if (pt.get_child_optional("network_security"))
    {
        sectionName = "network_security";
    }
    std::string dataPath = pt.get<std::string>(sectionName + ".data_path", "./conf/");
    std::string key = dataPath + "/" + pt.get<std::string>(sectionName + ".key", "gmnode.key");
    std::string cert = dataPath + "/" + pt.get<std::string>(sectionName + ".cert", "gmnode.crt");
    std::string caCert = dataPath + "/" + pt.get<std::string>(sectionName + ".ca_cert", "gmca.crt");
    std::string caPath = dataPath + "/" + pt.get<std::string>(sectionName + ".ca_path", "");
    std::string enKey = dataPath + pt.get<std::string>(sectionName + ".en_key", "gmennode.key");
    std::string enCert = dataPath + pt.get<std::string>(sectionName + ".en_cert", "gmennode.crt");
    std::string crypto_provider = pt.get<string>(sectionName + ".crypto_provider", "ssm");
    std::string keyId = pt.get<std::string>(sectionName + ".key_id", "");
    std::string encKeyId = pt.get<std::string>(sectionName + ".enckey_id", "");
    bool use_hsm = dev::stringCmpIgnoreCase(crypto_provider, "hsm") == 0;
    bytes keyContent;
    if (!key.empty())
    {
        try
        {
            if (g_BCOSConfig.diskEncryption.enable)
                keyContent = EncryptedFile::decryptContents(key);
            else
                keyContent = contents(key);
        }
        catch (std::exception& e)
        {
            INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                                   << LOG_DESC("open privateKey failed") << LOG_KV("file", key);
            BOOST_THROW_EXCEPTION(PrivateKeyError());
        }
    }

    std::shared_ptr<EC_KEY> ecKey;
    if (!keyContent.empty())
    {
        try
        {
            INITIALIZER_LOG(DEBUG)
                << LOG_BADGE("SecureInitializerGM") << LOG_DESC("loading privateKey");
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
                << LOG_BADGE("SecureInitializerGM") << LOG_DESC("parse privateKey failed")
                << LOG_KV("EINFO", boost::diagnostic_information(e));
            BOOST_THROW_EXCEPTION(e);
        }
    }
    else
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("privateKey doesn't exist!");
        BOOST_THROW_EXCEPTION(PrivateKeyNotExists());
    }

    std::shared_ptr<const BIGNUM> ecPrivateKey(
        EC_KEY_get0_private_key(ecKey.get()), [](const BIGNUM*) {});

    std::shared_ptr<char> privateKeyData(
        BN_bn2hex(ecPrivateKey.get()), [](char* p) { OPENSSL_free(p); });

    std::string keyHex(privateKeyData.get());
    if (keyHex.size() != 64u)
    {
        throw std::invalid_argument("Private Key file error! Missing bytes!");
    }

    KeyPair keyPair = KeyPair(Secret(keyHex));
    if (use_hsm)
    {
        if (dev::stringCmpIgnoreCase(keyId, "") == 0 || dev::stringCmpIgnoreCase(encKeyId, "") == 0)
        {
            INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                                   << LOG_DESC(
                                          " hsm module is used but key id is not configured. "
                                          "Please make sure network_security.key_id and "
                                          "network_security.enckey_id index is configured.");
            throw std::invalid_argument(
                "hsm module is used but key id is not configured. Please make sure "
                "network_security.key_id and network_security.enckey_id index is configured.");
        }
        else
        {
#ifdef FISCO_SDF
            keyPair.setKeyIndex(std::stoi(keyId.c_str()));
#endif
        }
    }

    std::shared_ptr<boost::asio::ssl::context> sslContext =
        std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);

    INITIALIZER_LOG(INFO) << LOG_BADGE("SecureInitializerGM") << LOG_DESC("get pub of node")
                          << LOG_KV("nodeID", keyPair.pub().hex());

    boost::asio::const_buffer keyBuffer(keyContent.data(), keyContent.size());
    sslContext->use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);

    if (!cert.empty() && !contents(cert).empty())
    {
        INITIALIZER_LOG(DEBUG) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("use user certificate") << LOG_KV("file", cert);
        sslContext->use_certificate_chain_file(cert);
        if (!SSL_CTX_get0_certificate(sslContext->native_handle()))
        {
            INITIALIZER_LOG(ERROR)
                << LOG_BADGE("SecureInitializer")
                << LOG_DESC("certificate load failed, please check") << LOG_KV("file", cert);
            ERROR_OUTPUT << LOG_BADGE("SecureInitializer")
                         << LOG_DESC("certificate load failed, please check")
                         << LOG_KV("file", cert) << std::endl;
            exit(1);
        }
    }
    else
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("certificate doesn't exist!");
        BOOST_THROW_EXCEPTION(CertificateNotExists());
    }
    // encrypt certificate should set after connect certificate
    sslContext->use_certificate_file(enCert, boost::asio::ssl::context::file_format::pem);
    if (SSL_CTX_use_enc_PrivateKey_file(
            sslContext->native_handle(), enKey.c_str(), SSL_FILETYPE_PEM) > 0)
    {
        INITIALIZER_LOG(DEBUG) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("use GM enc ca certificate") << LOG_KV("file", enKey);
    }
    else
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("GM enc ca certificate not exists!");
        BOOST_THROW_EXCEPTION(CertificateNotExists());
    }

    auto caCertContent = contents(caCert);
    if (!caCert.empty() && !caCertContent.empty())
    {
        INITIALIZER_LOG(DEBUG) << LOG_BADGE("SecureInitializerGM") << LOG_DESC("use ca certificate")
                               << LOG_KV("file", caCert);

        sslContext->add_certificate_authority(
            boost::asio::const_buffer(caCertContent.data(), caCertContent.size()));
    }
    else
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("CA Certificate doesn't exist!");
        BOOST_THROW_EXCEPTION(CertificateNotExists());
    }

    if (!caPath.empty())
    {
        INITIALIZER_LOG(DEBUG) << LOG_BADGE("SecureInitializerGM") << LOG_DESC("use ca")
                               << LOG_KV("file", caPath);
        sslContext->add_verify_path(caPath);
    }
    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer |
                                boost::asio::ssl::verify_fail_if_no_peer_cert);
    return ConfigResult{keyPair, sslContext};
}

void SecureInitializer::initConfigWithSMCrypto(const boost::property_tree::ptree& pt)
{
    try
    {
        ConfigResult gmConfig = initGmConfig(pt);
        m_key = gmConfig.keyPair;
        m_sslContexts[Usage::Default] = gmConfig.sslContext;
        m_sslContexts[Usage::ForP2P] = gmConfig.sslContext;
        bool smCryptoChannel = pt.get<bool>("chain.sm_crypto_channel", false);
        if (smCryptoChannel)
        {
            m_sslContexts[Usage::ForRPC] = gmConfig.sslContext;
        }
        else
        {
            std::string dataPath = pt.get<std::string>("network_security.data_path", "./conf/");
            ConfigResult originConfig = initOriginConfig(dataPath);
            m_sslContexts[Usage::ForRPC] = originConfig.sslContext;
        }
    }
    catch (Exception& e)
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("load verify file failed")
                               << LOG_KV("EINFO", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(e);
    }
}

std::shared_ptr<bas::context> SecureInitializer::SSLContextWithSMCrypto(Usage _usage)
{
    auto defaultP = m_sslContexts.find(Usage::Default);
    if (defaultP == m_sslContexts.end())
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("SecureInitializer has not been initialied");
        BOOST_THROW_EXCEPTION(SecureInitializerNotInitConfig());
    }

    auto p = m_sslContexts.find(_usage);
    if (p != m_sslContexts.end())
        return p->second;

    // if not found, return default
    return defaultP->second;
}

void SecureInitializer::initConfig(const boost::property_tree::ptree& pt)
{
    if (g_BCOSConfig.SMCrypto())
    {
        initConfigWithSMCrypto(pt);
    }
    else
    {
        initConfigWithCrypto(pt);
    }
}

std::shared_ptr<bas::context> SecureInitializer::SSLContext(Usage _usage)
{
    if (g_BCOSConfig.SMCrypto())
    {
        return SSLContextWithSMCrypto(_usage);
    }
    else
    {
        return SSLContextWithCrypto(_usage);
    }
}
