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
 */

#include <bcos-boostssl/context/Common.h>

using namespace bcos::boostssl;

X509* bcos::boostssl::toX509(const char* _pemBuffer)
{
    BIO* bio_mem = BIO_new(BIO_s_mem());
    BIO_puts(bio_mem, _pemBuffer);
    X509* x509 = PEM_read_bio_X509(bio_mem, NULL, NULL, NULL);
    BIO_free(bio_mem);
    return x509;
}

EVP_PKEY* bcos::boostssl::toEvpPkey(const char* _pemBuffer)
{
    BIO* bio_mem = BIO_new_mem_buf(_pemBuffer, -1);
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio_mem, NULL, NULL, NULL);
    BIO_free(bio_mem);
    return pkey;
}