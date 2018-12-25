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
#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/easylog.h>
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
    std::string dataPath = pt.get<std::string>("secure.data_path", "./fisco-bcos-data/");
    std::string key = dataPath + "/" + pt.get<std::string>("secure.key", "node.key");
    std::string cert = dataPath + "/" + pt.get<std::string>("secure.cert", "node.crt");
    std::string caCert = dataPath + "/" + pt.get<std::string>("secure.ca_cert", "ca.crt");
    std::string caPath = dataPath + "/" + pt.get<std::string>("secure.ca_path", "");
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
            INITIALIZER_LOG(ERROR)
                << "[#SecureInitializer::initConfig] open privateKey failed: [file]: " << key;
            ERROR_OUTPUT << "[#SecureInitializer::initConfig] open privateKey failed: [file]: "
                         << key << std::endl;
            exit(1);
        }
    }

    std::shared_ptr<EC_KEY> ecKey;
    if (!keyContent.empty())
    {
        try
        {
            INITIALIZER_LOG(DEBUG) << "[#SecureInitializer::initConfig] loading privateKey.";
            std::shared_ptr<BIO> bioMem(BIO_new(BIO_s_mem()), [&](BIO* p) { BIO_free(p); });
            BIO_write(bioMem.get(), keyContent.data(), keyContent.size());

            std::shared_ptr<EVP_PKEY> evpPKey(
                PEM_read_bio_PrivateKey(bioMem.get(), NULL, NULL, NULL),
                [](EVP_PKEY* p) { EVP_PKEY_free(p); });

            if (!evpPKey)
            {
                ERROR_OUTPUT << "[#SecureInitializer::initConfig] load privateKey failed"
                             << std::endl;
                exit(1);
            }

            ecKey.reset(EVP_PKEY_get1_EC_KEY(evpPKey.get()), [](EC_KEY* p) { EC_KEY_free(p); });
        }
        catch (dev::Exception& e)
        {
            INITIALIZER_LOG(ERROR)
                << "[#SecureInitializer::initConfig] parse privateKey failed: [EINFO]: "
                << boost::diagnostic_information(e);
            ERROR_OUTPUT << "[[#SecureInitializer::initConfig] parse privateKey failed: [EINFO]: "
                         << boost::diagnostic_information(e) << std::endl;
            exit(1);
        }
    }
    else
    {
        INITIALIZER_LOG(ERROR) << "[#SecureInitializer::initConfig] privateKey doesn't exist!";
        ERROR_OUTPUT << "[#SecureInitializer::initConfig] privateKey doesn't exist!" << std::endl;
        exit(1);
    }

    std::shared_ptr<const BIGNUM> ecPrivateKey(
        EC_KEY_get0_private_key(ecKey.get()), [](const BIGNUM* p) {});

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
        m_sslContext =
            std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);

        std::shared_ptr<EC_KEY> ecdh(
            EC_KEY_new_by_curve_name(NID_secp256k1), [](EC_KEY* p) { EC_KEY_free(p); });
        SSL_CTX_set_tmp_ecdh(m_sslContext->native_handle(), ecdh.get());

        m_sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_none);
        INITIALIZER_LOG(DEBUG) << "[#SecureInitializer::initConfig] [nodeID]: "
                               << m_key.pub().hex();

        boost::asio::const_buffer keyBuffer(keyContent.data(), keyContent.size());
        m_sslContext->use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);

        if (!cert.empty() && !contents(cert).empty())
        {
            INITIALIZER_LOG(DEBUG)
                << "[#SecureInitializer::initConfig] use user certificate: [file]: " << cert;
            m_sslContext->use_certificate_chain_file(cert);
            m_sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer);
        }
        else
        {
            INITIALIZER_LOG(ERROR) << "[#SecureInitializer::initConfig] certificate doesn't exist!";
            ERROR_OUTPUT << "[#SecureInitializer::initConfig] certificate doesn't exist!"
                         << std::endl;
            exit(1);
        }

        auto caCertContent = contents(caCert);
        if (!caCert.empty() && !caCertContent.empty())
        {
            INITIALIZER_LOG(DEBUG)
                << "[#SecureInitializer::initConfig] use ca certificate: [file]: " << caCert;

            m_sslContext->add_certificate_authority(
                boost::asio::const_buffer(caCertContent.data(), caCertContent.size()));
            m_sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer);
        }
        else
        {
            INITIALIZER_LOG(ERROR)
                << "[#SecureInitializer::initConfig] CA Certificate doesn't exist!";
            ERROR_OUTPUT << "[#SecureInitializer::initConfig] CA Certificate doesn't exist!"
                         << std::endl;
            exit(1);
        }

        if (!caPath.empty())
        {
            INITIALIZER_LOG(DEBUG) << "[#SecureInitializer::initConfig] use ca: [path]: " << caPath;

            m_sslContext->add_verify_path(caPath);
            m_sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer);
        }
    }
    catch (Exception& e)
    {
        INITIALIZER_LOG(ERROR)
            << "[#SecureInitializer::initConfig] load verify file failed: [EINFO]: "
            << boost::diagnostic_information(e);
        ERROR_OUTPUT << "[#SecureInitializer::initConfig] load verify file failed: [EINFO]: "
                     << boost::diagnostic_information(e) << std::endl;
        exit(1);
    }
}
