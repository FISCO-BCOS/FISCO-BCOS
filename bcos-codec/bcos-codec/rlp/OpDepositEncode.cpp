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
    const uint8_t isSystemTransactionByte = fields.isSystemTransaction ? 1 : 0;
    if (fields.to.has_value())
    {
        encode(out, fields.sourceHash, fields.from, *fields.to, fields.mint, fields.value,
            fields.gas, isSystemTransactionByte, fields.data);
    }
    else
    {
        encode(out, fields.sourceHash, fields.from, bcos::bytesConstRef{}, fields.mint,
            fields.value, fields.gas, isSystemTransactionByte, fields.data);
    }
    return out;
}

}  // namespace bcos::codec::rlp
