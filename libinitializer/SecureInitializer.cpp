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


#ifdef FISCO_SDF
static void openssl_debug_message(const std::string& _method)
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

static ENGINE* try_load_engine(const char* engine = "sdf")
{
    ::ENGINE_load_builtin_engines();
    openssl_debug_message("ENGINE_load_builtin_engines");
    ENGINE* e = ::ENGINE_by_id(engine);
    openssl_debug_message("ENGINE_by_id");
    if (!e)
    {
        e = ::ENGINE_by_id("dynamic");
        openssl_debug_message("ENGINE_by_id");
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
        openssl_debug_message("ENGINE_set_default");
    }
    else
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializer")
                               << LOG_DESC("try_load_engine failed");
        exit(1);
    }

    return e;
}

static void use_engine_key(ENGINE* e,std::shared_ptr<boost::asio::ssl::context> sslContext,boost::asio::const_buffer keyBuffer, std::string keyName){
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
        INITIALIZER_LOG(ERROR)
            << LOG_BADGE("SecureInitializerGM") << LOG_DESC("ENGINE_load_private_key error")
            << LOG_KV("keyName", keyName);
        BOOST_THROW_EXCEPTION(std::runtime_error("ENGINE_load_private_key error"));
    }

    auto ret = ::SSL_CTX_use_PrivateKey(sslContext->native_handle(), evpPKey.get());
    INITIALIZER_LOG(INFO) << LOG_BADGE("SSL_CTX_use_PrivateKey")
                            << LOG_KV("keyName", keyName) << LOG_KV("ret", ret);
    if (ret <= 0)
    {
        BOOST_THROW_EXCEPTION(
            std::runtime_error("SSL_CTX_use_PrivateKey ret: " + std::to_string(ret)));
    }
        
}

#endif

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
    bool use_hsm_key = pt.get<bool>("chain.sm_crypto_hsm_key", false);

    // init ssl context
#ifdef FISCO_SDF
    auto handle = ::SSL_CTX_new(::GMTLS_method());
    if (!handle)
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_new error"));
        openssl_debug_message("SSL_CTX_new error");
    }
    std::shared_ptr<boost::asio::ssl::context> sslContext =
        std::make_shared<boost::asio::ssl::context>(handle);
#else
    std::shared_ptr<boost::asio::ssl::context> sslContext =
        std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);
#endif
    // get key
    // try decrypt node key if diskEncryption is on
    bytes keyContent;
    if (!key.empty())
    {
        try
        {
            if (g_BCOSConfig.diskEncryption.enable){
                keyContent = EncryptedFile::decryptContents(key);
            } 
            else if (!use_hsm_key){
                keyContent = contents(key);
            } 
        }
        catch (std::exception& e)
        {
            INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                                   << LOG_DESC("open privateKey failed") << LOG_KV("file", key);
            BOOST_THROW_EXCEPTION(PrivateKeyError());
        }
    }
    
    // construct ecKey
    std::shared_ptr<EC_KEY> ecKey;
    if (!keyContent.empty() && !use_hsm_key)
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
    else if(keyContent.empty() && !use_hsm_key)
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("privateKey doesn't exist!");
        BOOST_THROW_EXCEPTION(PrivateKeyNotExists());
    }

    // load key
    // When use hsm key
    KeyPair keyPair;
    if (use_hsm_key)
    {
#ifdef FISCO_SDF
        std::string keyId = pt.get<std::string>(sectionName + ".key_id", "");
        std::string enckeyId = pt.get<std::string>(sectionName + ".enckey_id", "");
        keyPair.set_pub(cert);
        std::string keyName = "sm2_" + keyId;
        std::string encKeyName = "sm2_" + enckeyId;
        boost::asio::const_buffer keyBuffer(keyName.c_str(), keyName.length());
        boost::asio::const_buffer keyBufferEnc(encKeyName.c_str(), encKeyName.length());
        INITIALIZER_LOG(INFO) << LOG_BADGE("SecureInitializerGM use_hsm_key")
                                << LOG_KV("keyId", keyId) << LOG_KV("enckeyId", enckeyId);

        keyPair.setKeyIndex(std::stoi(keyId.c_str()));
        INITIALIZER_LOG(INFO) << LOG_BADGE("SecureInitializerGM")
                                << LOG_KV("keyPair.keyId", keyPair.keyIndex());
        ENGINE* e = ::ENGINE_get_pkey_meth_engine(EVP_PKEY_SM2);
        if (!e)
        {
            BOOST_THROW_EXCEPTION(std::runtime_error("ENGINE_get_pkey_meth_engine error"));
        }
        use_engine_key(e,sslContext,keyBuffer,keyName);
        use_engine_key(e,sslContext,keyBufferEnc,encKeyName);
#else
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("You are trying to use hardware secure module, while your fisco-bcos binary not support. Please recompile your FISCO-BCOS code and with option -DUSE_HSM_SDF=on") ;
        exit(0);
#endif
    }else{
        std::shared_ptr<const BIGNUM> ecPrivateKey(
        EC_KEY_get0_private_key(ecKey.get()), [](const BIGNUM*) {});
        std::shared_ptr<char> privateKeyData(
            BN_bn2hex(ecPrivateKey.get()), [](char* p) { OPENSSL_free(p); });

        std::string keyHex(privateKeyData.get());
        if (keyHex.size() != 64u)
        {
            throw std::invalid_argument("Private Key file error! Missing bytes!");
        }

        keyPair = KeyPair(Secret(keyHex));
    
        // load sign key
        INITIALIZER_LOG(INFO) << LOG_BADGE("SecureInitializerGM") << LOG_DESC("get pub of node")
                            << LOG_KV("nodeID", keyPair.pub().hex());
        boost::asio::const_buffer keyBuffer(keyContent.data(), keyContent.size());
        sslContext->use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);
    
        // load encrypt key
#ifdef FISCO_SDF
        bytes keyContentEnc = contents(enKey);
        boost::asio::const_buffer keyBufferEnc(keyContentEnc.data(), keyContentEnc.size());
        sslContext->use_private_key(keyBufferEnc, boost::asio::ssl::context::file_format::pem);
#else
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
#endif
    }

    // load certificate
#ifdef FISCO_SDF
    if(!cert.empty() && !contents(cert).empty() && !enCert.empty() && !contents(enCert).empty()){

        INITIALIZER_LOG(DEBUG) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("use user certificate") << LOG_KV("file", cert)
                               << LOG_DESC("use user enc certificate") << LOG_KV("file", enCert);
        sslContext->use_certificate_file(cert, boost::asio::ssl::context::file_format::pem);
        sslContext->use_certificate_file(enCert, boost::asio::ssl::context::file_format::pem);
        if (!SSL_CTX_get0_certificate(sslContext->native_handle()))
        {
            INITIALIZER_LOG(ERROR)
                << LOG_BADGE("SecureInitializer")
                << LOG_DESC("certificate load failed, please check") << LOG_KV("file", cert)
                << LOG_DESC("certificate load failed, please check") << LOG_KV("file", enCert);
            ERROR_OUTPUT << LOG_BADGE("SecureInitializer")
                         << LOG_DESC("certificate load failed, please check")
                         << LOG_KV("file", cert) << LOG_KV("file", enCert) << std::endl;
            exit(1);
        }
#else
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
#endif
    }
    else
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("certificate doesn't exist!");
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
        
        bool use_hsm_key = pt.get<bool>("chain.sm_crypto_hsm_key", false);
        if (use_hsm_key){
#ifdef FISCO_SDF
            try_load_engine("sdf");
#else
            INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializerGM")
                               << LOG_DESC("You are trying to use hardware secure module, while your fisco-bcos binary is not support. Please recompile your FISCO-BCOS code and with option -DUSE_HSM_SDF=on") ;
            exit(0);
#endif
        }
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
