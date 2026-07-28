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
 * @file StorageValueCodec.cpp
 * @brief Storage-slot key transform and value encoding for the secure trie (spec §5.3)
 */
#include "StorageValueCodec.h"
#include "Errors.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
// AnyHasher.h defines the free bcos::crypto::hasher::hash(); OpenSSLHasher.h only defines the
// hasher type. A unity build can mask a missing include of the former (a sibling TU's include
// leaks in), so keep both explicit.
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>

namespace bcos::ledger::mpt
{

bcos::h256 slotKeyHash(bcos::h256 const& slot)
{
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    return slotKeyHash(slot, hasher);
}

bcos::bytes encodeStorageValue(bcos::bytesConstRef value)
{
    size_t offset = 0;
    while (offset < value.size() && value[offset] == 0)
    {
        ++offset;
    }
    if (offset == value.size())
    {
        return {};  // zero value: not part of the storage trie
    }
    bcos::bytes out;
    codec::rlp::encode(out, value.getCroppedData(offset));
    return out;
}

bcos::u256 decodeStorageValue(bcos::bytesConstRef leaf)
{
    // The RLP cursor API advances a mutable ref; decode only reads, so the cast is safe (same
    // shape as NodeDecoder.cpp / Account.cpp).
    bcos::bytesRef cursor{const_cast<bcos::byte*>(leaf.data()), leaf.size()};
    bcos::u256 value;
    if (auto error = bcos::codec::rlp::decode(cursor, value); error)
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "storage leaf: bad RLP value: " + error->errorMessage()));
    }
    if (!cursor.empty())
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "storage leaf: trailing bytes after the RLP value"));
    }
    return value;
}

}  // namespace bcos::ledger::mpt
