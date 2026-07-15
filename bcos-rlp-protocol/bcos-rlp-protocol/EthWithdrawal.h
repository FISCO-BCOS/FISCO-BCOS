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

namespace bcos::protocol
{

struct EthWithdrawal
{
    uint64_t index{0};
    uint64_t validatorIndex{0};
    bcos::Address address;
    uint64_t amount{0};

    size_t encodedLength() const
    {
        return codec::rlp::lengthOfItems(index, validatorIndex, address.ref(), amount);
    }

    void encode(bcos::bytes& out) const
    {
        codec::rlp::encode(out, index, validatorIndex, address.ref(), amount);
    }
    
    bcos::Error::UniquePtr decode(bcos::bytesConstRef data)
    {
        auto mutableData = data.toBytes();
        bcos::bytesRef ref(mutableData.data(), mutableData.size());
    
        auto error = codec::rlp::decode(ref, index, validatorIndex, address, amount);
        if (error)
        {
            return error;
        }
        return nullptr;
    }
    
    bool operator==(const EthWithdrawal& other) const = default;
};
}  // namespace bcos::protocol
