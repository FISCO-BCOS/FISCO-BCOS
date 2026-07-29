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
 * @file OpDepositEncode.cpp
 * @date 2026-07-28
 */

#include "OpDepositEncode.h"
#include "RLPEncode.h"

namespace bcos::codec::rlp
{

bcos::bytes encodeDepositEnvelope(OpDepositFields const& fields)
{
    bcos::bytes out;
    out.push_back(OpDepositFields::kDepositTxType);

    // `to` nilability changes the RLP shape (empty string vs. a present 20-byte address), so it
    // cannot be folded into a single call with a "zero means absent" default — see the header
    // comment on OpDepositFields::to.
    //
    // Fully qualified (bcos::codec::rlp::encode, not unqualified encode(...)): this function's
    // own name (`encodeDepositEnvelope`) doesn't collide with the free RLP `encode` overload
    // set, so unqualified lookup would in fact resolve correctly here — but EthBlockHeader.cpp
    // hit exactly this class of bug the other way (a member function literally named `encode`
    // shadowing the RLPEncode.h free functions, "too many arguments" at compile time, see its
    // comment), so this call is qualified defensively to rule the same failure mode out here.
    //
    // uint32_t, not uint8_t: RLPEncode.h's generic scalar encode(bytes&, UnsignedByte auto)
    // calls bcos::toCompactBigEndian(b) — for T == bcos::byte (== uint8_t) that odr-uses
    // bcos-utilities/DataConvertUtility.h's non-template `inline bytes
    // toCompactBigEndian(byte, unsigned)` overload (preferred over the template on an equally
    // exact match), which is declared `inline` in the header but its only definition lives in
    // DataConvertUtility.cpp — not visible in this TU (found via -Wundefined-inline -Werror in
    // build verification, a pre-existing bcos-utilities header/library-boundary defect, not
    // this file's to fix). uint32_t sidesteps it: it only matches the fully header-defined
    // template overload, with byte-identical resulting RLP bytes for a 0/1 value regardless of
    // the C++ operand width.
    const uint32_t isSystemTransactionByte = fields.isSystemTransaction ? 1 : 0;
    if (fields.to.has_value())
    {
        bcos::codec::rlp::encode(out, fields.sourceHash, fields.from, *fields.to, fields.mint,
            fields.value, fields.gas, isSystemTransactionByte, fields.data);
    }
    else
    {
        bcos::codec::rlp::encode(out, fields.sourceHash, fields.from, bcos::bytesConstRef{},
            fields.mint, fields.value, fields.gas, isSystemTransactionByte, fields.data);
    }
    return out;
}

}  // namespace bcos::codec::rlp
