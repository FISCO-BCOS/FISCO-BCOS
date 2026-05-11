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


#define CONTEXT_LOG(LEVEL) BCOS_LOG(LEVEL) << "[BOOSTSSL][CTX]"
#define NODEINFO_LOG(LEVEL) BCOS_LOG(LEVEL) << "[BOOSTSSL][NODEINFO]"

namespace bcos::boostssl
{
X509* toX509(const char* _pemBuffer);

EVP_PKEY* toEvpPkey(const char* _pemBuffer);

}  // namespace bcos::boostssl
