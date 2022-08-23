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
 * @file ContextBuilder.cpp
 * @author: octopus
 * @date 2021-06-14
 */

#include <bcos-boostssl/context/Common.h>
#include <bcos-boostssl/context/ContextBuilder.h>
#include <bcos-boostssl/context/ContextConfig.h>
#include <bcos-utilities/BoostLog.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <exception>
#include <iostream>

using namespace bcos;
using namespace bcos::boostssl;
using namespace bcos::boostssl::context;

// static const std::string DEFAULT_CONFIG = "./boostssl.ini";

std::shared_ptr<std::string> ContextBuilder::readFileContent(boost::filesystem::path const& _file)
{
    std::shared_ptr<std::string> content = std::make_shared<std::string>();
    boost::filesystem::ifstream fileStream(_file, std::ifstream::binary);
    if (!fileStream)
    {
        return content;
    }
    fileStream.seekg(0, fileStream.end);
    auto length = fileStream.tellg();
    if (length == 0)
    {
        return content;
    }
    fileStream.seekg(0, fileStream.beg);
    content->resize(length);
    fileStream.read((char*)content->data(), length);
    return content;
}

std::shared_ptr<boost::asio::ssl::context> ContextBuilder::buildSslContext(
    bool _server, const std::string& _configPath)
{
    auto config = std::make_shared<ContextConfig>();
    config->initConfig(_configPath);
    return buildSslContext(_server, *config);
}

std::shared_ptr<boost::asio::ssl::context> ContextBuilder::buildSslContext(
    bool _server, const ContextConfig& _contextConfig)
{
    if (_contextConfig.isCertPath())
    {
        if (_contextConfig.sslType() != "sm_ssl")
        {
            return buildSslContext(_contextConfig.certConfig());
        }
        return buildSslContext(_server, _contextConfig.smCertConfig());
    }
    else
    {
        if (_contextConfig.sslType() != "sm_ssl")
        {
            return buildSslContextByCertContent(_contextConfig.certConfig());
        }
        return buildSslContextByCertContent(_server, _contextConfig.smCertConfig());
    }
}

std::shared_ptr<boost::asio::ssl::context> ContextBuilder::buildSslContext(
    const ContextConfig::CertConfig& _certConfig)
{
    std::shared_ptr<boost::asio::ssl::context> sslContext =
        std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);

    auto keyContent =
        readFileContent(boost::filesystem::path(_certConfig.nodeKey));  // node.key content
    boost::asio::const_buffer keyBuffer(keyContent->data(), keyContent->size());
    sslContext->use_private_key(keyBuffer, boost::asio::ssl::context::file_format::pem);

    // node.crt
    sslContext->use_certificate_chain_file(_certConfig.nodeCert);

    auto caCertContent = readFileContent(boost::filesystem::path(_certConfig.caCert));  // ca.crt
    sslContext->add_certificate_authority(
        boost::asio::const_buffer(caCertContent->data(), caCertContent->size()));

    std::string caPath;
    if (!caPath.empty())
    {
        sslContext->add_verify_path(caPath);
    }

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer |
                                boost::asio::ssl::verify_fail_if_no_peer_cert);

    return sslContext;
}

std::shared_ptr<boost::asio::ssl::context> ContextBuilder::buildSslContext(
    bool _server, const ContextConfig::SMCertConfig& _smCertConfig)
{
    SSL_CTX* ctx = NULL;
    if (_server)
    {
        const SSL_METHOD* meth = SSLv23_server_method();
        ctx = SSL_CTX_new(meth);
    }
    else
    {
        const SSL_METHOD* meth = CNTLS_client_method();
        ctx = SSL_CTX_new(meth);
    }

    std::shared_ptr<boost::asio::ssl::context> sslContext =
        std::make_shared<boost::asio::ssl::context>(ctx);

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_none);

    /* Load the server certificate into the SSL_CTX structure */
    if (SSL_CTX_use_certificate_file(
            sslContext->native_handle(), _smCertConfig.nodeCert.c_str(), SSL_FILETYPE_PEM) <= 0)
    {
        ERR_print_errors_fp(stderr);
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_use_certificate_file error"));
    }

    /* Load the private-key corresponding to the server certificate */
    if (SSL_CTX_use_PrivateKey_file(
            sslContext->native_handle(), _smCertConfig.nodeKey.c_str(), SSL_FILETYPE_PEM) <= 0)
    {
        ERR_print_errors_fp(stderr);
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_use_PrivateKey_file error"));
    }

    /* Check if the server certificate and private-key matches */
    if (!SSL_CTX_check_private_key(sslContext->native_handle()))
    {
        fprintf(stderr, "Private key does not match the certificate public key\n");
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_check_private_key error"));
    }

    /* Load the server encrypt certificate into the SSL_CTX structure */
    if (SSL_CTX_use_enc_certificate_file(
            sslContext->native_handle(), _smCertConfig.enNodeCert.c_str(), SSL_FILETYPE_PEM) <= 0)
    {
        ERR_print_errors_fp(stderr);
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_use_enc_certificate_file error"));
    }

    /* Load the private-key corresponding to the server encrypt certificate */
    if (SSL_CTX_use_enc_PrivateKey_file(
            sslContext->native_handle(), _smCertConfig.enNodeKey.c_str(), SSL_FILETYPE_PEM) <= 0)
    {
        ERR_print_errors_fp(stderr);
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_use_enc_PrivateKey_file error"));
    }

    /* Check if the server encrypt certificate and private-key matches */
    if (!SSL_CTX_check_enc_private_key(sslContext->native_handle()))
    {
        fprintf(stderr, "Private key does not match the certificate public key\n");
        ERR_print_errors_fp(stderr);
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_check_enc_private_key error"));
    }

    /* Load the RSA CA certificate into the SSL_CTX structure
    if (!SSL_CTX_load_verify_locations(
            sslContext->native_handle(), _smCertConfig.caCert.c_str(), NULL))
    {
        ERR_print_errors_fp(stderr);
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_load_verify_locations error"));
    }
    */
    auto caCertContent = readFileContent(boost::filesystem::path(_smCertConfig.caCert));  // ca.crt
    sslContext->add_certificate_authority(
        boost::asio::const_buffer(caCertContent->data(), caCertContent->size()));

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer |
                                boost::asio::ssl::verify_fail_if_no_peer_cert);
    return sslContext;
}

std::shared_ptr<boost::asio::ssl::context> ContextBuilder::buildSslContextByCertContent(
    const ContextConfig::CertConfig& _certConfig)
{
    std::shared_ptr<boost::asio::ssl::context> sslContext =
        std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);

    sslContext->use_private_key(
        boost::asio::const_buffer(_certConfig.nodeKey.data(), _certConfig.nodeKey.size()),
        boost::asio::ssl::context::file_format::pem);
    sslContext->use_certificate_chain(
        boost::asio::const_buffer(_certConfig.nodeCert.data(), _certConfig.nodeCert.size()));

    sslContext->add_certificate_authority(
        boost::asio::const_buffer(_certConfig.caCert.data(), _certConfig.caCert.size()));

    std::string caPath;
    if (!caPath.empty())
    {
        sslContext->add_verify_path(caPath);
    }

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer |
                                boost::asio::ssl::verify_fail_if_no_peer_cert);

    return sslContext;
}

std::shared_ptr<boost::asio::ssl::context> ContextBuilder::buildSslContextByCertContent(
    bool _server, const ContextConfig::SMCertConfig& _smCertConfig)
{
    SSL_CTX* ctx = NULL;
    if (_server)
    {
        const SSL_METHOD* meth = SSLv23_server_method();
        ctx = SSL_CTX_new(meth);
    }
    else
    {
        const SSL_METHOD* meth = CNTLS_client_method();
        ctx = SSL_CTX_new(meth);
    }

    std::shared_ptr<boost::asio::ssl::context> sslContext =
        std::make_shared<boost::asio::ssl::context>(ctx);

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_none);

    sslContext->use_certificate_chain(boost::asio::const_buffer(
        _smCertConfig.nodeCert.data(), _smCertConfig.nodeCert.size()));  // node.crt

    sslContext->use_private_key(
        boost::asio::const_buffer(_smCertConfig.nodeKey.data(), _smCertConfig.nodeKey.size()),
        boost::asio::ssl::context::file_format::pem);  // node.key

    /* Check if the server certificate and private-key matches */
    if (!SSL_CTX_check_private_key(sslContext->native_handle()))
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_check_private_key error"));
    }

    /* Load the server encrypt certificate into the SSL_CTX structure */
    if (!SSL_CTX_use_enc_certificate(
            sslContext->native_handle(), toX509(_smCertConfig.enNodeCert.c_str())))
    {
        ERR_print_errors_fp(stderr);
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_use_enc_certificate_file error"));
    }

    /* Load the private-key corresponding to the server encrypt certificate */
    if (!SSL_CTX_use_enc_PrivateKey(
            sslContext->native_handle(), toEvpPkey(_smCertConfig.enNodeKey.c_str())))
    {
        ERR_print_errors_fp(stderr);
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_use_enc_PrivateKey_file error"));
    }

    /* Check if the server encrypt certificate and private-key matches */
    if (!SSL_CTX_check_enc_private_key(sslContext->native_handle()))
    {
        ERR_print_errors_fp(stderr);
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_check_enc_private_key error"));
    }

    sslContext->add_certificate_authority(boost::asio::const_buffer(
        _smCertConfig.caCert.data(), _smCertConfig.caCert.size()));  // ca.crt

    /* Load the RSA CA certificate into the SSL_CTX structure
    if (!SSL_CTX_load_verify_locations(
            sslContext->native_handle(), _smCertConfig.caCert.c_str(), NULL))
    {
        ERR_print_errors_fp(stderr);
        BOOST_THROW_EXCEPTION(std::runtime_error("SSL_CTX_load_verify_locations error"));
    }
    */

    sslContext->set_verify_mode(boost::asio::ssl::context_base::verify_peer |
                                boost::asio::ssl::verify_fail_if_no_peer_cert);
    return sslContext;
}