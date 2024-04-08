/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file Web3Endpoint.cpp
 * @author: kyonGuo
 * @date 2024/3/21
 */

#include "Web3Endpoint.h"
#include "include/BuildInfo.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
using namespace bcos::rpc;
bcos::task::Task<void> Web3Endpoint::clientVersion(
    const Json::Value& request, Json::Value& response)
{
    std::string version = "FISCO-BCOS-Web3RPC/" + std::string(FISCO_BCOS_PROJECT_VERSION) + "-" +
                          std::string(FISCO_BCOS_BUILD_TYPE) + "/" +
                          std::string(FISCO_BCOS_BUILD_PLATFORM) + "/" +
                          std::string(FISCO_BCOS_COMMIT_HASH);
    Json::Value result = version;
    buildJsonContent(result, response);
    co_return;
}
bcos::task::Task<void> Web3Endpoint::sha3(const Json::Value& request, Json::Value& response)
{
    // sha3 in eth means keccak256, not sha3-256, ref:
    // https://ethereum.org/zh/developers/docs/apis/json-rpc/#web3_sha3
    auto const msg = toView(request[0U]);
    auto const bytes = fromHexWithPrefix(msg);
    crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    hasher.update(bytesConstRef(bytes.data(), bytes.size()));
    crypto::HashType out;
    hasher.final(out);
    Json::Value result = out.hexPrefixed();
    buildJsonContent(result, response);
    co_return;
}
