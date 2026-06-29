/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file EthWithdrawal.h
 * @brief Ethereum-standard withdrawal (EIP-4895) — standalone RLP-encoded struct
 * @date 2026/6/24
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>

using namespace bcos;
namespace bcos::protocol
{

struct EthWithdrawal
{
    uint64_t index{0};
    uint64_t validatorIndex{0};
    bcos::Address address;
    uint64_t amount{0};

    void encode(bcos::bytes& out) const
    {
        size_t payloadLen = 0;
        payloadLen += codec::rlp::length(index);
        payloadLen += codec::rlp::length(validatorIndex);
        payloadLen += codec::rlp::length(address.ref());
        payloadLen += codec::rlp::length(amount);
    
        codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = payloadLen});
        codec::rlp::encode(out, index);
        codec::rlp::encode(out, validatorIndex);
        codec::rlp::encode(out, address.ref());
        codec::rlp::encode(out, amount);
    }
    
    void decode(bcos::bytesConstRef data)
    {
        auto mutableData = data.toBytes();
        bcos::bytesRef ref(mutableData.data(), mutableData.size());
    
        auto [err, head] = codec::rlp::decodeHeader(ref);
        if (err)
            { BOOST_THROW_EXCEPTION(std::runtime_error(
                "EthWithdrawal::decode: " + err->errorMessage()));}
        if (!head.isList)
            { BOOST_THROW_EXCEPTION(std::runtime_error(
                "EthWithdrawal::decode: expected RLP list"));}
    
        if (auto e = codec::rlp::decodeItems(ref, index, validatorIndex); e != nullptr)
            { BOOST_THROW_EXCEPTION(std::runtime_error(
                "EthWithdrawal::decode: " + e->errorMessage()));}   
    
        {
            auto [e2, h2] = codec::rlp::decodeHeader(ref);
            if (e2 || h2.isList)
                { BOOST_THROW_EXCEPTION(std::runtime_error(
                    "EthWithdrawal: expected address bytes"));}
            std::memcpy(address.mutableData().data(), ref.data(),
                std::min<size_t>(h2.payloadLength, address.SIZE));
            ref = ref.getCroppedData(h2.payloadLength);
        }
    
        // amount
        if (auto e3 = codec::rlp::decode(ref, amount); e3 != nullptr)
            { BOOST_THROW_EXCEPTION(std::runtime_error(
                "EthWithdrawal::decode: amount"));}
    }
    
    bool operator==(const EthWithdrawal& other) const
    {
        return index == other.index && validatorIndex == other.validatorIndex &&
               address == other.address && amount == other.amount;
    }};

}  // namespace bcos::protocol
