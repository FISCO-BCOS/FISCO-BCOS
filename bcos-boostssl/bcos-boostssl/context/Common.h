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
 * @file Common.h
 * @author: octopus
 * @date 2021-06-14
 */

#pragma once
#include <bcos-utilities/BoostLog.h>
#include <openssl/bio.h>
#include <openssl/pem.h>


#define CONTEXT_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE(m_moduleName) << "[BOOSTSSL][CTX]"
#define NODEINFO_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE(m_moduleName) << "[BOOSTSSL][NODEINFO]"

namespace bcos
{  // namespace bcos
namespace boostssl
{
inline X509* toX509(const char* _pemBuffer)
{
    BIO* bio_mem = BIO_new(BIO_s_mem());
    BIO_puts(bio_mem, _pemBuffer);
    X509* x509 = PEM_read_bio_X509(bio_mem, NULL, NULL, NULL);
    BIO_free(bio_mem);
    // X509_free(x509);
    return x509;
}

inline EVP_PKEY* toEvpPkey(const char* _pemBuffer)
{
    BIO* bio_mem = BIO_new_mem_buf(_pemBuffer, -1);
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio_mem, NULL, NULL, NULL);
    BIO_free(bio_mem);
    // EVP_PKEY_free(pkey);
    return pkey;
}

}  // namespace boostssl
}  // namespace bcos
