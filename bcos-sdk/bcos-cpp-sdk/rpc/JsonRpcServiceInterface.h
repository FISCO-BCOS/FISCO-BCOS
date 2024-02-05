/*
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @file JsonRpcServiceInterface.h
 * @author: octopus
 * @date 2023-02-22
 */

#pragma once
#include <bcos-cpp-sdk/rpc/JsonRpcInterface.h>
#include <bcos-cpp-sdk/ws/Service.h>
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <functional>

namespace bcos::cppsdk::jsonrpc
{

class JsonRpcServiceInterface
{
public:
    using Ptr = std::shared_ptr<JsonRpcServiceInterface>;
    using UniquePtr = std::unique_ptr<JsonRpcServiceInterface>;

    JsonRpcServiceInterface() = default;
    virtual ~JsonRpcServiceInterface() = default;

public:
    virtual std::string sendTransaction(const bcos::crypto::KeyPairInterface& _keyPair,
        const std::string& _groupID, const std::string& _nodeName, const std::string& _to,
        bcos::bytes&& _data, std::string _abi, int32_t _attribute, std::string _extraData,
        RespFunc _respFunc) = 0;
};

}  // namespace bcos::cppsdk::jsonrpc
