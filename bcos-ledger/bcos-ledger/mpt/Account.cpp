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

namespace bcos::ledger::mpt
{

Account::Account() : storageRoot(emptyRootHash()), codeHash(emptyCodeHash()) {}

// Alias the RLP codec; we must fully qualify encode()/decode() below because the
// unqualified names would otherwise bind to Account's own member functions.
namespace rlp = bcos::codec::rlp;

bcos::bytes Account::encode() const
{
    bcos::bytes out;
    // u256 nonce/balance encode big-endian-trimmed (0 → empty string 0x80);
    // the two hashes encode as fixed 32-byte strings.
    size_t const payloadLength = rlp::length(nonce) + rlp::length(balance) +
                                 rlp::length(storageRoot) + rlp::length(codeHash);
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
    if (auto error = ::bcos::codec::rlp::decode(cursor, account.storageRoot); error)
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "Account RLP: bad storageRoot: " + error->errorMessage()));
    }
    if (auto error = ::bcos::codec::rlp::decode(cursor, account.codeHash); error)
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "Account RLP: bad codeHash: " + error->errorMessage()));
    }

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
