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
 * @file Web3RawTransaction.h
 * @brief Reassembly of full signed raw Ethereum transaction bytes
 */

#pragma once

#include <bcos-utilities/Common.h>

namespace bcostars::protocol
{

/// Reassemble the full signed raw transaction bytes (EIP-2718 typed envelope or legacy
/// RLP list) from the signing payload (Transaction::extraTransactionBytes()) and the
/// 65-byte r||s||yParity signature (Transaction::signatureData()).
///
/// This is the byte-splice introduced for the canonical Web3 txHash recompute (FIB-New1);
/// keccak256 of the returned bytes is that canonical hash. The Engine API getPayload path
/// uses the returned bytes directly as the transaction wire form.
///
/// Throws std::invalid_argument when the payload is not a decodable Web3 signing payload
/// or the signature is not 65 bytes.
bcos::bytes reassembleWeb3RawTransaction(
    bcos::bytesConstRef payload, bcos::bytesConstRef signature);

}  // namespace bcostars::protocol
