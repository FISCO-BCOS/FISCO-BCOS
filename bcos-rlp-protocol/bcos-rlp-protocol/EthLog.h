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
 * @file EthLog.h
 * @brief Ethereum transaction-log RLP codec (rlp([address, topics, data]))
 * @date 2026/8/18
 */
#pragma once

#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/FixedBytes.h>
#include <vector>

namespace bcos::protocol
{
// Ethereum log entry: emitted by EVM LOG* opcodes, embedded in receipts.
// RLP form (Ethereum yellow paper, Appendix B): rlp([address, topics, data]).
struct EthLogData
{
    bcos::Address address;                        // 20 bytes, the emitting contract
    std::vector<bcos::crypto::HashType> topics;   // 0..n 32-byte topics
    bcos::bytes data;                             // arbitrary-length payload

    bool operator==(const EthLogData& rhs) const
    {
        return address == rhs.address && topics == rhs.topics && data == rhs.data;
    }
    bool operator!=(const EthLogData& rhs) const { return !(*this == rhs); }
};

// Class wrapper following the EthBlockHeader pattern: rlpEncode/rlpDecode plus the
// codec::rlp overloads that let EthLogData live inside lists (receipts, ...).
class EthLog
{
public:
    EthLog() = default;
    explicit EthLog(EthLogData data) : m_data(std::move(data)) {}

    void rlpEncode(bcos::bytes& out) const;
    // Decodes a single log item (a 3-element list) from `data`.
    bcos::Error::UniquePtr rlpDecode(bcos::bytesConstRef data);

    const EthLogData& data() const { return m_data; }
    EthLogData& data() { return m_data; }

private:
    EthLogData m_data;
};
}  // namespace bcos::protocol

namespace bcos::codec::rlp
{
// Overloads so EthLogData works as an item inside the generic list/vector codecs.
inline size_t length(const protocol::EthLogData& _log) noexcept
{
    return length(_log.address, _log.topics, _log.data);
}
inline void encode(bcos::bytes& _out, const protocol::EthLogData& _log) noexcept
{
    encode(_out, _log.address, _log.topics, _log.data);
}
inline bcos::Error::UniquePtr decode(bcos::bytesRef& _in, protocol::EthLogData& _log) noexcept
{
    return decode(_in, _log.address, _log.topics, _log.data);
}
}  // namespace bcos::codec::rlp

namespace bcos::protocol
{
// ADL-visible delegators: the generic list/vector codecs (bcos::codec::rlp) resolve overloads
// for element types via argument-dependent lookup, which only searches the element type's own
// namespaces (bcos::protocol). These thin wrappers let EthLogData participate in
// std::vector<EthLogData> / variadic-list encode/decode. The canonical implementations live in
// bcos::codec::rlp above (qualified codec::rlp::encode(out, log) still works).
inline size_t length(const EthLogData& _log) noexcept
{
    return codec::rlp::length(_log);
}
inline void encode(bcos::bytes& _out, const EthLogData& _log) noexcept
{
    codec::rlp::encode(_out, _log);
}
inline bcos::Error::UniquePtr decode(bcos::bytesRef& _in, EthLogData& _log) noexcept
{
    return codec::rlp::decode(_in, _log);
}
}  // namespace bcos::protocol
