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

#define OPENSSL_LOAD_CONF

#include "SecureInitializer.h"
#include "libdevcrypto/CryptoInterface.h"
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcrypto/Common.h>
#include <libsecurity/EncryptedFile.h>
#include <openssl/engine.h>
#include <openssl/rsa.h>
#ifdef FISCO_SDF
#include <openssl/evp.h>
#endif
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

    // Read disk encryption key file
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

    // Load disk encryption key content to ecKey
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
        throw std::invalid_argument("privateKey doesn't exist!");
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
    SslInitializer sslInitializer;
    std::string originDataPath = _dataPath + "/origin_cert/";
    std::string key = originDataPath + "node.key";
    std::string cert = originDataPath + "node.crt";
    std::string caCert = originDataPath + "ca.crt";
    std::string caPath = originDataPath;

    // Get KeyContent
    bytes keyContent = sslInitializer.decryptKey(key, g_BCOSConfig.diskEncryption.enable, false);

    // init ssl context
    std::shared_ptr<boost::asio::ssl::context> sslContext = sslInitializer.initSslContext();

    // Load Key Pair
    KeyPair keyPair = sslInitializer.loadKeyPair(keyContent);

    // Set verify mode
    std::shared_ptr<EC_KEY> ecdh(
        EC_KEY_new_by_curve_name(NID_secp256k1), [](EC_KEY* p) { EC_KEY_free(p); });
    SSL_CTX_set_tmp_ecdh(sslContext->native_handle(), ecdh.get());

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_none);
    INITIALIZER_LOG(INFO) << LOG_BADGE("SecureInitializer") << LOG_DESC("get pub of node")
                          << LOG_KV("nodeID", keyPair.pub().hex());

    // Use key and cert
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

    // Set ca certificate
    sslInitializer.setCaCert(sslContext, caCert, caPath);

    // Return ConfigResult
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
    INITIALIZER_LOG(INFO) << LOG_BADGE("SecureInitializerGM") << LOG_KV("sectionName", sectionName)
                          << LOG_KV("crypto_provider", crypto_provider)
                          << LOG_KV("use_hsm", use_hsm);

    SslInitializer sslInitializer;

    // check config and load hsm engine
    if (use_hsm)
    {
#ifdef FISCO_SDF
        sslInitializer.tryLoadEngine("sdf");
#else
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC(
                                      "You are trying to use hardware secure module, while your "
                                      "fisco-bcos binary is not support. Please recompile your "
                                      "FISCO-BCOS code with option -DUSE_HSM_SDF=on");
        throw std::invalid_argument(
            "You are trying to use hardware secure module, while your fisco-bcos binary is not "
            "support. Please recompile your FISCO-BCOS code with option -DUSE_HSM_SDF=on");
#endif
    }

    // init ssl context
    std::shared_ptr<boost::asio::ssl::context> sslContext = sslInitializer.initSslContext();

    // Get KeyContent
    bytes keyContent = sslInitializer.decryptKey(key, g_BCOSConfig.diskEncryption.enable, use_hsm);

    // Construct keypair and load key
    KeyPair keyPair;
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
            keyPair = sslInitializer.loadKeyPairById(cert, keyId, encKeyId);
            sslInitializer.useEngineKey(sslContext, keyId, encKeyId);
#endif
        }
    }
    else
    {
        keyPair = sslInitializer.loadKeyPair(keyContent);
#ifdef FISCO_SDF
        sslInitializer.useSwKey(sslContext, enKey, keyContent);
#else
        sslInitializer.useKey(sslContext, enKey, enCert, keyContent);
#endif
    }

    // load certificate
    if (!cert.empty() && !contents(cert).empty() && !enCert.empty() && !contents(enCert).empty())
    {
        INITIALIZER_LOG(DEBUG) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("use user certificate") << LOG_KV("file", cert)
                               << LOG_DESC("use user enc certificate") << LOG_KV("file", enCert);
#ifdef FISCO_SDF
        sslContext->use_certificate_file(cert, boost::asio::ssl::context::file_format::pem);
        sslContext->use_certificate_file(enCert, boost::asio::ssl::context::file_format::pem);
#else
        sslContext->use_certificate_chain_file(cert);
#endif
        if (!SSL_CTX_get0_certificate(sslContext->native_handle()))
        {
            INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializer")
                                   << LOG_DESC("certificate load failed, please check")
                                   << LOG_KV("file", cert) << LOG_KV("file", enCert);
            ERROR_OUTPUT << LOG_BADGE("SecureInitializer")
                         << LOG_DESC("certificate load failed, please check")
                         << LOG_KV("file", cert) << LOG_KV("file", enCert) << std::endl;
            exit(1);
        }
    }
    else
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("certificate doesn't exist!");
        BOOST_THROW_EXCEPTION(CertificateNotExists());
    }

    // Set ca certificate
    sslInitializer.setCaCert(sslContext, caCert, caPath);

    // Return ConfigResult
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
#ifdef FISCO_SDF
        INITIALIZER_LOG(ERROR)
            << LOG_BADGE("SecureInitializerGM")
            << LOG_DESC(
                   "Your fisco-bcos binary is the SM HSM special edition, non SM is not support in "
                   "this edition. Please use build_gm_hsm_chain.sh to build a gm chain, or use "
                   "fisco-bcos other editions.");
        throw std::invalid_argument(
            "Your fisco-bcos binary is the SM HSM special edition, non SM is not support in this "
            "edition. Please use build_gm_hsm_chain.sh to build a gm chain,  or use fisco-bcos "
            "other editions.");
#else
        initConfigWithCrypto(pt);
#endif
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


#ifdef FISCO_SDF
void SslInitializer::opensslDebugMessage(const std::string& _method)
{
    char buf[256] = {0};
    auto error = ::ERR_get_error();
    ::ERR_error_string_n(error, buf, sizeof(buf));
    if (error != 0)
    {
        INITIALIZER_LOG(WARNING) << LOG_BADGE("OpenSSL") << LOG_DESC("openssl error message")
                                 << LOG_KV("method", _method) << LOG_KV("error", error)
                                 << LOG_KV("desc", std::string(buf));
    }
}

ENGINE* SslInitializer::tryLoadEngine(const char* engine)
{
    ::ENGINE_load_builtin_engines();
    opensslDebugMessage("ENGINE_load_builtin_engines");
    ENGINE* e = ::ENGINE_by_id(engine);
    opensslDebugMessage("ENGINE_by_id");
    if (!e)
    {
        e = ::ENGINE_by_id("dynamic");
        opensslDebugMessage("ENGINE_by_id");
        if (e)
        {
            if (!::ENGINE_ctrl_cmd_string(e, "SO_PATH", engine, 0) ||
                !::ENGINE_ctrl_cmd_string(e, "LOAD", NULL, 0))
            {
                ::ENGINE_free(e);
                e = NULL;
            }
        }
    }

    if (e)
    {
        ::ENGINE_set_default(e, ENGINE_METHOD_ALL);
        opensslDebugMessage("ENGINE_set_default");
    }
    else
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializer")
                               << LOG_DESC("try_load_engine failed");
        exit(1);
    }

    return e;
}

void SslInitializer::useOpensslEngineKey(ENGINE* e,
    std::shared_ptr<boost::asio::ssl::context> sslContext, boost::asio::const_buffer keyBuffer,
    std::string keyName)
{
    char key_id[32] = {0};
    memcpy(key_id, keyBuffer.data(), keyBuffer.size());

    std::shared_ptr<EVP_PKEY> evpPKey(
        ::ENGINE_load_private_key(e, key_id, NULL, NULL), [](EVP_PKEY* p) {
            if (p)
            {
                ::EVP_PKEY_free(p);
            }
        });

    if (!evpPKey)
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("ENGINE_load_private_key error")
                               << LOG_KV("keyName", keyName);
        BOOST_THROW_EXCEPTION(std::runtime_error("ENGINE_load_private_key error"));
    }

    auto ret = ::SSL_CTX_use_PrivateKey(sslContext->native_handle(), evpPKey.get());
    INITIALIZER_LOG(INFO) << LOG_BADGE("SSL_CTX_use_PrivateKey") << LOG_KV("keyName", keyName)
                          << LOG_KV("ret", ret);
    if (ret <= 0)
    {
        BOOST_THROW_EXCEPTION(
            std::runtime_error("SSL_CTX_use_PrivateKey ret: " + std::to_string(ret)));
    }
}
void SslInitializer::useSwKey(
    std::shared_ptr<boost::asio::ssl::context> sslContext, std::string enKey, bytes keyContent)
{
    boost::asio::const_buffer keyBuffer(keyContent.data(), keyContent.size());
    bytes enKeyContent = contents(enKey);
    boost::asio::const_buffer enKeyBuffer(enKeyContent.data(), enKeyContent.size());
    sslContext->use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);
    sslContext->use_private_key(enKeyBuffer, boost::asio::ssl::context::file_format::pem);
}


KeyPair SslInitializer::loadKeyPairById(std::string cert, std::string keyId, std::string encKeyId)
{
    KeyPair keyPair;
    std::string keyName = "sm2_" + keyId;
    std::string encKeyName = "sm2_" + encKeyId;
    keyPair.set_pub(cert);
    boost::asio::const_buffer keyBuffer(keyName.c_str(), keyName.length());
    boost::asio::const_buffer keyBufferEnc(encKeyName.c_str(), encKeyName.length());
    INITIALIZER_LOG(INFO) << LOG_BADGE("SecureInitializerGM use hsm") << LOG_KV("keyId", keyId)
                          << LOG_KV("enckeyId", encKeyId);
    keyPair.setKeyIndex(std::stoi(keyId.c_str()));
    INITIALIZER_LOG(INFO) << LOG_BADGE("SecureInitializerGM")
                          << LOG_KV("keyPair.keyId", keyPair.keyIndex());
    return keyPair;
}

void SslInitializer::useEngineKey(
    std::shared_ptr<boost::asio::ssl::context> sslContext, std::string keyId, std::string encKeyId)
{
    std::string keyName = "sm2_" + keyId;
    std::string encKeyName = "sm2_" + encKeyId;
    ENGINE* e = ::ENGINE_get_pkey_meth_engine(EVP_PKEY_SM2);
    if (!e)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("ENGINE_get_pkey_meth_engine error"));
    }
    boost::asio::const_buffer keyBuffer(keyName.c_str(), keyName.length());
    boost::asio::const_buffer keyBufferEnc(encKeyName.c_str(), encKeyName.length());
    useOpensslEngineKey(e, sslContext, keyBuffer, keyName);
    useOpensslEngineKey(e, sslContext, keyBufferEnc, encKeyName);
}
#endif

#ifndef FISCO_SDF
void SslInitializer::useKey(std::shared_ptr<boost::asio::ssl::context> sslContext,
    std::string enKey, std::string enCert, bytes keyContent)
{
    boost::asio::const_buffer keyBuffer(keyContent.data(), keyContent.size());
    sslContext->use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);
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
}
#endif

void SslInitializer::setCaCert(
    std::shared_ptr<boost::asio::ssl::context> sslContext, std::string caCert, std::string caPath)
{
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
}

bytes SslInitializer::decryptKey(std::string key, bool diskEncryptionEnable, bool useHsm)
{
    if (useHsm)
    {
        return bytes();
    }
    if (!key.empty())
    {
        try
        {
            if (diskEncryptionEnable)
                return EncryptedFile::decryptContents(key);
            else
                return contents(key);
        }
        catch (std::exception& e)
        {
            INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializer")
                                   << LOG_DESC("open privateKey failed") << LOG_KV("file", key);

            BOOST_THROW_EXCEPTION(PrivateKeyError());
        }
    }
    else
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializer")
                               << LOG_DESC("privateKey doesn't exist!");
        BOOST_THROW_EXCEPTION(PrivateKeyNotExists());
    }
}

KeyPair SslInitializer::loadKeyPair(bytes keyContent)
{
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
    return KeyPair(Secret(keyHex));
}

// init ssl context
std::shared_ptr<boost::asio::ssl::context> SslInitializer::initSslContext()
{
#ifdef FISCO_SDF
    auto handle = ::SSL_CTX_new(::GMTLS_method());
    if (!handle)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_new error"));
        opensslDebugMessage("SSL_CTX_new error");
    }
    return std::make_shared<boost::asio::ssl::context>(handle);
#else
    return std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);
#endif
}