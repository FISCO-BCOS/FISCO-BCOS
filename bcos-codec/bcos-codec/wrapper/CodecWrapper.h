/**
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
 * @file CodecWrapper.h
 * @author: kyonRay
 * @date 2021-06-02
 */

#pragma once

#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-utilities/Overloaded.h"

namespace bcos
{
namespace codec::rlp
{
class RLPWrapper
{
};
}  // namespace codec::rlp
class CodecWrapper
{
public:
    using Ptr = std::shared_ptr<CodecWrapper>;
    explicit CodecWrapper(const crypto::Hash& _hash) : m_hash(std::addressof(_hash)) {}
    explicit CodecWrapper(crypto::Hash::Ptr _hash) : m_hash(std::move(_hash)) {}
    template <typename... Args>
    bytes encode(Args&&... _args) const
    {
        // Note: the codec is not thread-safe, so we can't share this object
        codec::abi::ContractABICodec abi(hash());
        return abi.abiIn("", _args...);
    }
    template <typename... Args>
    bytes encodeWithSig(const std::string& _sig, Args&&... _args) const
    {
        // Note: the codec is not thread-safe, so we can't share this object
        codec::abi::ContractABICodec abi(hash());
        return abi.abiIn(_sig, _args...);
    }

    bytes encodeWithSig(const std::string& _sig) const
    {
        // Note: the codec is not thread-safe, so we can't share this object
        codec::abi::ContractABICodec abi(hash());
        return abi.abiIn(_sig);
    }

    template <typename... T>
    void decode(bytesConstRef _data, T&... _t) const
    {
        codec::abi::ContractABICodec abi(hash());
        abi.abiOut(_data, _t...);
    }

private:
    std::variant<const crypto::Hash*, crypto::Hash::Ptr> m_hash;

    const crypto::Hash& hash() const
    {
        return std::visit(
            bcos::overloaded([](const crypto::Hash* hash) -> const crypto::Hash& { return *hash; },
                [](const crypto::Hash::Ptr& hash) -> const crypto::Hash& { return *hash; }),
            m_hash);
    }
};
}  // namespace bcos
