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
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcrypto/Common.h>
#include <libsecurity/EncryptedFile.h>
#include <openssl/engine.h>
#include <openssl/rsa.h>
#include <boost/algorithm/string/replace.hpp>
#include <iostream>

using namespace dev;
using namespace dev::initializer;

void SecureInitializer::initConfig(const boost::property_tree::ptree& pt)
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

std::shared_ptr<bas::context> SecureInitializer::SSLContext(Usage _usage)
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
