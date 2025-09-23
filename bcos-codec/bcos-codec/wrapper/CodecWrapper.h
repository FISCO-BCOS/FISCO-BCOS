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
#include "bcos-codec/scale/ScaleDecoderStream.h"
#include "bcos-codec/scale/ScaleEncoderStream.h"
#include "bcos-utilities/Overloaded.h"

namespace bcos
{
namespace codec::rlp
{
class RLPWrapper
{
};
}  // namespace codec::rlp
enum VMType
{
    EVM,
    WASM,
    UNDEFINED
};
class CodecWrapper
{
public:
    using Ptr = std::shared_ptr<CodecWrapper>;
    CodecWrapper(const crypto::Hash& _hash, bool _isWasm)
      : m_type(_isWasm ? VMType::WASM : VMType::EVM), m_hash(std::addressof(_hash))
    {}
    CodecWrapper(crypto::Hash::Ptr _hash, bool _isWasm)
      : m_type(_isWasm ? VMType::WASM : VMType::EVM), m_hash(std::move(_hash))
    {}
    template <typename... Args>
    bytes encode(Args&&... _args) const
    {
        assert(m_type != VMType::UNDEFINED);
        if (m_type == VMType::EVM)
        {
            // Note: the codec is not thread-safe, so we can't share this object
            codec::abi::ContractABICodec abi(hash());
            return abi.abiIn("", _args...);
        }

        codec::scale::ScaleEncoderStream s;
        (s << ... << std::forward<Args>(_args));
        return s.data();
    }
    template <typename... Args>
    bytes encodeWithSig(const std::string& _sig, Args&&... _args) const
    {
        assert(m_type != VMType::UNDEFINED);
        if (m_type == VMType::EVM)
        {
            // Note: the codec is not thread-safe, so we can't share this object
            codec::abi::ContractABICodec abi(hash());
            return abi.abiIn(_sig, _args...);
        }

        codec::scale::ScaleEncoderStream s;
        (s << ... << std::forward<Args>(_args));
        return hash().hash(_sig).ref().getCroppedData(0, 4).toBytes() + s.data();
    }

    bytes encodeWithSig(const std::string& _sig) const
    {
        assert(m_type != VMType::UNDEFINED);
        if (m_type == VMType::EVM)
        {
            // Note: the codec is not thread-safe, so we can't share this object
            codec::abi::ContractABICodec abi(hash());
            return abi.abiIn(_sig);
        }

        codec::scale::ScaleEncoderStream s;
        return hash().hash(_sig).ref().getCroppedData(0, 4).toBytes() + s.data();
    }

    template <typename... T>
    void decode(bytesConstRef _data, T&... _t) const
    {
        assert(m_type != VMType::UNDEFINED);
        if (m_type == VMType::EVM)
        {
            codec::abi::ContractABICodec abi(hash());
            abi.abiOut(_data, _t...);
        }
        else if (m_type == VMType::WASM)
        {
            auto&& t = _data.toBytes();
            codec::scale::ScaleDecoderStream stream(gsl::make_span(t));
            decodeScale(stream, _t...);
        }
    }
    template <typename T, typename... U>
    void decodeScale(codec::scale::ScaleDecoderStream& _s, T& _t, U&... _u) const
    {
        _s >> _t;
        decodeScale(_s, _u...);
    }
    template <typename T>
    void decodeScale(codec::scale::ScaleDecoderStream& _s, T& _t) const
    {
        _s >> _t;
    }

    void decodeScale(codec::scale::ScaleDecoderStream&) const {}

private:
    VMType m_type = VMType::UNDEFINED;
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
