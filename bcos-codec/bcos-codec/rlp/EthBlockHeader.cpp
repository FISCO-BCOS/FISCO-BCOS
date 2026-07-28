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
 * @file EthBlockHeader.cpp
 * @date 2026-07-28
 */

#include "EthBlockHeader.h"
#include "RLPEncode.h"
#include <bcos-crypto/hash/Keccak256.h>

namespace bcos::codec::rlp
{

bcos::bytes EthBlockHeader::encode() const
{
    bcos::bytes out;
    // Field order is load-bearing: it is the spec §5.1 21-field order (== go-ethereum
    // core/types.Header field order), verified by manual RLP walkthrough against
    // golden.encodedHeaderHex (see task-3-report.md). Unqualified encode() resolves via
    // ordinary lookup to the overload set declared in this same namespace (RLPEncode.h).
    encode(out, parentHash, ommersHash, feeRecipient, stateRoot, transactionsRoot, receiptsRoot,
        logsBloom, difficulty, number, gasLimit, gasUsed, timestamp, extraData, prevRandao, nonce,
        baseFeePerGas, withdrawalsRoot, blobGasUsed, excessBlobGas, parentBeaconBlockRoot,
        requestsHash);
    return out;
}

bcos::h256 EthBlockHeader::hash() const
{
    auto encoded = encode();
    return bcos::crypto::keccak256Hash(bcos::ref(encoded));
}

}  // namespace bcos::codec::rlp
