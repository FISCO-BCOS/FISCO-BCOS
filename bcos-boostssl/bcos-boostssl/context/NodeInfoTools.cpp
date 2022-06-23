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
 * @file NodeInfoTools.cpp
 * @author: lucasli
 * @date 2022-03-07
 */

#include <bcos-boostssl/context/Common.h>
#include <bcos-boostssl/context/NodeInfoTools.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FileUtility.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ini_parser.hpp>

using namespace bcos::boostssl::context;

/*
 * @brief : functions called after openssl handshake,
 *          maily to get node id and verify whether the certificate has been
 * expired
 * @param nodeIDOut : also return value, pointer points to the node id string
 * @return std::function<bool(bool, boost::asio::ssl::verify_context&)>:
 *  return true: verify success
 *  return false: verify failed
 * modifications 2019.03.20: append subject name and issuer name after nodeIDOut
 * for demand of fisco-bcos-browser
 */
std::function<bool(bool, boost::asio::ssl::verify_context&)> NodeInfoTools::newVerifyCallback(
    std::shared_ptr<std::string> nodeIDOut)
{
    return [nodeIDOut](bool preverified, boost::asio::ssl::verify_context& ctx) {
        try
        {
            /// return early when the certificate is invalid
            if (!preverified)
            {
                return false;
            }
            /// get the object points to certificate
            X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
            if (!cert)
            {
                NODEINFO_LOG(WARNING) << LOG_DESC("Get cert failed");
                return preverified;
            }

            auto sslContextPubHexHandler = initSSLContextPubHexHandler();
            if (!sslContextPubHexHandler(cert, *nodeIDOut.get()))
            {
                return preverified;
            }

            int crit = 0;
            BASIC_CONSTRAINTS* basic =
                (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, &crit, NULL);
            if (!basic)
            {
                NODEINFO_LOG(WARNING) << LOG_DESC("Get ca basic failed");
                return preverified;
            }

            if (basic->ca)
            {
                // ca or agency certificate
                NODEINFO_LOG(TRACE) << LOG_DESC("Ignore CA certificate");
                BASIC_CONSTRAINTS_free(basic);
                return preverified;
            }

            BASIC_CONSTRAINTS_free(basic);
            return preverified;
        }
        catch (std::exception& e)
        {
            NODEINFO_LOG(WARNING) << LOG_DESC("Cert verify failed")
                                  << boost::diagnostic_information(e);
            return preverified;
        }
    };
}

std::function<bool(const std::string& priKey, std::string& pubHex)>
NodeInfoTools::initCert2PubHexHandler()
{
    return [](const std::string& _cert, std::string& _pubHex) -> bool {
        std::string errorMessage;
        do
        {
            auto certContent = readContentsToString(boost::filesystem::path(_cert));
            if (!certContent || certContent->empty())
            {
                errorMessage = "unable to load cert content, cert: " + _cert;
                break;
            }

            NODEINFO_LOG(INFO) << LOG_DESC("initCert2PubHexHandler") << LOG_KV("cert", _cert)
                               << LOG_KV("certContent: ", certContent);

            std::shared_ptr<BIO> bioMem(BIO_new(BIO_s_mem()), [](BIO* p) {
                if (p != NULL)
                {
                    BIO_free(p);
                }
            });

            if (!bioMem)
            {
                errorMessage = "BIO_new error";
                break;
            }

            BIO_write(bioMem.get(), certContent->data(), certContent->size());
            std::shared_ptr<X509> x509Ptr(
                PEM_read_bio_X509(bioMem.get(), NULL, NULL, NULL), [](X509* p) {
                    if (p != NULL)
                    {
                        X509_free(p);
                    }
                });

            if (!x509Ptr)
            {
                errorMessage = "PEM_read_bio_X509 error";
                break;
            }

            ASN1_BIT_STRING* pubKey = X509_get0_pubkey_bitstr(x509Ptr.get());
            if (pubKey == NULL)
            {
                errorMessage = "X509_get0_pubkey_bitstr error";
                break;
            }

            auto hex = bcos::toHexString(pubKey->data, pubKey->data + pubKey->length, "");
            _pubHex = *hex.get();

            NODEINFO_LOG(INFO) << LOG_DESC("initCert2PubHexHandler ") << LOG_KV("cert", _cert)
                               << LOG_KV("pubHex: ", _pubHex);
            return true;
        } while (0);

        NODEINFO_LOG(WARNING) << LOG_DESC("initCert2PubHexHandler") << LOG_KV("cert", _cert)
                              << LOG_KV("errorMessage", errorMessage);
        return false;
    };
}

std::function<bool(X509* cert, std::string& pubHex)> NodeInfoTools::initSSLContextPubHexHandler()
{
    return [](X509* x509, std::string& _pubHex) -> bool {
        ASN1_BIT_STRING* pubKey =
            X509_get0_pubkey_bitstr(x509);  // csc->current_cert is an X509 struct
        if (pubKey == NULL)
        {
            return false;
        }

        auto hex = bcos::toHexString(pubKey->data, pubKey->data + pubKey->length, "");
        _pubHex = *hex.get();

        NODEINFO_LOG(INFO) << LOG_DESC("[NEW]SSLContext pubHex: " + _pubHex);
        return true;
    };
}