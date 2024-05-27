/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file CallValidator.h
 * @author: kyonGuo
 * @date 2023/4/18
 */

#pragma once
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/multigroup/ChainNodeInfo.h>
#include <bcos-protocol/TransactionStatus.h>
namespace bcos::rpc
{
class CallValidator
{
public:
    CallValidator() = default;
    ~CallValidator() = default;
    CallValidator(const CallValidator&) = delete;
    CallValidator(CallValidator&&) = default;
    CallValidator& operator=(const CallValidator&) = delete;
    CallValidator& operator=(CallValidator&&) = default;

    static std::pair<bool, bytes> verify(std::string_view _to, std::string_view _data,
        std::string_view _sign, group::NodeCryptoType);
};
}  // namespace bcos::rpc
