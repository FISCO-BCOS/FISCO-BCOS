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
 * @file PruneMetadata.cpp
 * @brief Key layout and value codec for the MPT pruning metadata rows (spec §4.8)
 */
#include "PruneMetadata.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <boost/throw_exception.hpp>
#include <string>

namespace bcos::ledger::mpt
{

namespace rlp = bcos::codec::rlp;

namespace
{
// Fixed-width big-endian u64, the byte order that makes lexicographic key order numeric order.
void appendBigEndian64(uint64_t value, std::string& out)
{
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<char>((value >> shift) & 0xFFU));
    }
}

uint64_t readBigEndian64(bcos::bytesConstRef bytes)
{
    uint64_t value = 0;
    for (auto const byte : bytes)
    {
        value = (value << 8) | static_cast<uint64_t>(byte);
    }
    return value;
}
}  // namespace

bcos::executor_v1::StateKey pruneRefKey(bcos::h256 const& hash)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {kPruneRefTable, std::string_view(reinterpret_cast<const char*>(hash.data()),
                                bcos::h256::SIZE)};
}

bcos::executor_v1::StateKey pruneQueueKey(uint64_t targetBlock, bcos::h256 const& hash)
{
    std::string keyPart;
    keyPart.reserve(8 + bcos::h256::SIZE);
    appendBigEndian64(targetBlock, keyPart);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    keyPart.append(reinterpret_cast<const char*>(hash.data()), bcos::h256::SIZE);
    return {kPruneQueueTable, keyPart};
}

std::pair<uint64_t, bcos::h256> decodeQueueKeyPart(std::string_view keyPart)
{
    if (keyPart.size() != 8 + bcos::h256::SIZE)
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "PruneMetadata: queue key part is " +
                                  std::to_string(keyPart.size()) + " bytes, expected 40"));
    }
    auto const targetBlock = readBigEndian64(
        bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(keyPart.data()), 8));
    auto const hash = bcos::h256(bcos::bytesConstRef(
        reinterpret_cast<bcos::byte const*>(keyPart.data() + 8), bcos::h256::SIZE));
    return {targetBlock, hash};
}

bcos::executor_v1::StateKey watermarkKey()
{
    return {kPruneMetaTable, kWatermarkRowKey};
}

bcos::bytes encodeRefCount(PruneRefCount const& refCount)
{
    bcos::bytes out;
    rlp::encode(out, refCount.count, refCount.pendingDeleteAt);
    return out;
}

PruneRefCount decodeRefCount(bcos::bytesConstRef encoded)
{
    bcos::bytesRef cursor{const_cast<bcos::byte*>(encoded.data()), encoded.size()};
    PruneRefCount out;
    if (auto error = rlp::decode(cursor, out.count, out.pendingDeleteAt); error)
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "PruneMetadata: bad refcount RLP: " + error->errorMessage()));
    }
    if (!cursor.empty())
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "PruneMetadata: trailing bytes after the refcount list"));
    }
    return out;
}

bcos::bytes encodeWatermark(uint64_t blockNumber)
{
    bcos::bytes out(8);
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        out[(56 - shift) / 8] = static_cast<bcos::byte>((blockNumber >> shift) & 0xFFU);
    }
    return out;
}

uint64_t decodeWatermark(bcos::bytesConstRef encoded)
{
    if (encoded.size() != 8)
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "PruneMetadata: watermark value is " +
                                  std::to_string(encoded.size()) + " bytes, expected 8"));
    }
    return readBigEndian64(encoded);
}

}  // namespace bcos::ledger::mpt
