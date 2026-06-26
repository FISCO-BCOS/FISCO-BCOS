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
 * @file Account.cpp
 * @brief Ethereum account 4-tuple RLP encode/decode (spec §5.4)
 */
#include "Account.h"
#include "Constants.h"
#include "Errors.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <boost/throw_exception.hpp>
#include <string>

namespace bcos::ledger::mpt
{

Account::Account() : storageRoot(emptyRootHash()), codeHash(emptyCodeHash()) {}

// Alias the RLP codec; we must fully qualify encode()/decode() below because the
// unqualified names would otherwise bind to Account's own member functions.
namespace rlp = bcos::codec::rlp;

namespace
{
// Decode one RLP string item into `out`, rejecting any payload whose length != 32. The generic
// FixedBytes<32> decoder zero-pads a short payload (RLPDecode.h: FixedBytes<32>{getCroppedData(0,
// payloadLength)}), silently accepting malformed input; Ethereum consensus requires storageRoot
// and codeHash to be exactly 32-byte strings, so enforce that here.
void decodeHash32(bcos::bytesRef& cursor, bcos::h256& out, char const* field)
{
    auto [error, header] = rlp::decodeHeader(cursor);
    if (error)
    {
        BOOST_THROW_EXCEPTION(
            MPTDecodeError{} << bcos::errinfo_comment(
                std::string("Account RLP: bad ") + field + " header: " + error->errorMessage()));
    }
    if (header.isList)
    {
        BOOST_THROW_EXCEPTION(
            MPTDecodeError{} << bcos::errinfo_comment(
                std::string("Account RLP: ") + field + " must be a 32-byte string, got a list"));
    }
    if (header.payloadLength != bcos::h256::SIZE)
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  std::string("Account RLP: ") + field + " RLP payload is " +
                                  std::to_string(header.payloadLength) + " bytes, expected 32"));
    }
    out = bcos::h256(bcos::bytesConstRef(cursor.data(), header.payloadLength));
    cursor = cursor.getCroppedData(header.payloadLength);
}
}  // namespace

bcos::bytes Account::encode() const
{
    bcos::bytes out;
    // u256 nonce/balance encode big-endian-trimmed (0 → empty string 0x80);
    // the two hashes encode as fixed 32-byte strings.
    size_t const payloadLength = rlp::length(nonce) + rlp::length(balance) +
                                 rlp::length(storageRoot) + rlp::length(codeHash);
    // Pre-size as the variadic rlp::encode(out, a, b, ...) path does, to avoid 1-2 reallocations.
    out.reserve(rlp::lengthOfLength(payloadLength) + payloadLength);
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payloadLength});
    rlp::encode(out, nonce);
    rlp::encode(out, balance);
    rlp::encode(out, storageRoot);
    rlp::encode(out, codeHash);
    return out;
}

Account Account::decode(bcos::bytesConstRef rlp)
{
    bcos::bytesRef cursor{const_cast<bcos::byte*>(rlp.data()), rlp.size()};

    auto [headerError, header] = ::bcos::codec::rlp::decodeHeader(cursor);
    if (headerError)
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "Account RLP: bad list header: " + headerError->errorMessage()));
    }
    if (!header.isList)
    {
        BOOST_THROW_EXCEPTION(
            MPTDecodeError{} << bcos::errinfo_comment("Account RLP: expected a list"));
    }
    if (header.payloadLength != cursor.size())
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "Account RLP: declared payload length does not match input"));
    }

    Account account;
    if (auto error = ::bcos::codec::rlp::decode(cursor, account.nonce); error)
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "Account RLP: bad nonce: " + error->errorMessage()));
    }
    if (auto error = ::bcos::codec::rlp::decode(cursor, account.balance); error)
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "Account RLP: bad balance: " + error->errorMessage()));
    }
    // storageRoot/codeHash must be exactly 32-byte RLP strings (decodeHash32 rejects shorter
    // payloads that the generic FixedBytes<32> decoder would silently zero-pad).
    decodeHash32(cursor, account.storageRoot, "storageRoot");
    decodeHash32(cursor, account.codeHash, "codeHash");

    // The 4 fields must consume exactly the declared payload — no trailing bytes,
    // no missing fields.
    if (!cursor.empty())
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "Account RLP: trailing bytes after the 4 fields"));
    }
    return account;
}

}  // namespace bcos::ledger::mpt
