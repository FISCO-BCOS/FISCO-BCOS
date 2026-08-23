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
 * @brief Ethereum withdrawal RLP codec (EIP-4895, rlp([index, validatorIndex, address, amount]))
 * @date 2026/8/18
 */
#pragma once

#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/FixedBytes.h>

namespace bcos::protocol
{
// EIP-4895 withdrawal: a validator's partial stake withdrawal, embedded in the block body
// (Shanghai+) and committed to by the header's withdrawalsRoot trie.
// RLP form: rlp([index, validatorIndex, address, amount]).
struct EthWithdrawalData
{
    uint64_t index{0};           // monotonically increasing withdrawal index
    uint64_t validatorIndex{0};  // validator registry index
    bcos::Address address;       // 20 bytes, the recipient
    uint64_t amount{0};          // Gwei

    bool operator==(const EthWithdrawalData& rhs) const
    {
        return index == rhs.index && validatorIndex == rhs.validatorIndex &&
               address == rhs.address && amount == rhs.amount;
    }
    bool operator!=(const EthWithdrawalData& rhs) const { return !(*this == rhs); }
};

// Class wrapper following the EthBlockHeader pattern: rlpEncode/rlpDecode plus the
// codec::rlp overloads that let EthWithdrawalData live inside lists (block bodies, ...).
class EthWithdrawal
{
public:
    EthWithdrawal() = default;
    explicit EthWithdrawal(EthWithdrawalData _data) : m_data(_data) {}

    void rlpEncode(bcos::bytes& out) const;
    // Decodes a single withdrawal item (a 4-element list) from `data`.
    bcos::Error::UniquePtr rlpDecode(bcos::bytesConstRef data);

    const EthWithdrawalData& data() const { return m_data; }
    EthWithdrawalData& data() { return m_data; }

private:
    EthWithdrawalData m_data;
};
}  // namespace bcos::protocol

namespace bcos::codec::rlp
{
// Overloads so EthWithdrawalData works as an item inside the generic list/vector codecs.
inline size_t length(const protocol::EthWithdrawalData& _withdrawal) noexcept
{
    return length(_withdrawal.index, _withdrawal.validatorIndex, _withdrawal.address,
        _withdrawal.amount);
}
inline void encode(bcos::bytes& _out, const protocol::EthWithdrawalData& _withdrawal) noexcept
{
    encode(_out, _withdrawal.index, _withdrawal.validatorIndex, _withdrawal.address,
        _withdrawal.amount);
}
inline bcos::Error::UniquePtr decode(
    bcos::bytesRef& _in, protocol::EthWithdrawalData& _withdrawal) noexcept
{
    return decode(
        _in, _withdrawal.index, _withdrawal.validatorIndex, _withdrawal.address, _withdrawal.amount);
}
}  // namespace bcos::codec::rlp

namespace bcos::protocol
{
// ADL-visible delegators (see EthLog.h): let EthWithdrawalData participate in
// std::vector<EthWithdrawalData> / variadic-list encode/decode.
inline size_t length(const EthWithdrawalData& _withdrawal) noexcept
{
    return codec::rlp::length(_withdrawal);
}
inline void encode(bcos::bytes& _out, const EthWithdrawalData& _withdrawal) noexcept
{
    codec::rlp::encode(_out, _withdrawal);
}
inline bcos::Error::UniquePtr decode(
    bcos::bytesRef& _in, EthWithdrawalData& _withdrawal) noexcept
{
    return codec::rlp::decode(_in, _withdrawal);
}
}  // namespace bcos::protocol
