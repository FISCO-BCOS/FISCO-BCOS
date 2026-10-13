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
 * @file BlockHeaderHash.h
 * @brief The single source of a block's identity hash across the three lanes.
 */

#pragma once

#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>

namespace bcos::protocol
{
/// True for an OP-Stack block. OP blocks are stored as NON_ETH headers (the OP build path
/// deliberately does not stamp a fork version) but always carry the Shanghai+ fork fields
/// that OP's ExecutionPayload fills in; a native FISCO NON_ETH header has neither.
/// This is the lane discriminator: consumers derive it, they do not re-spell it.
[[nodiscard]] bool isOpEthereumBlock(BlockHeader const& header);

/// The block's canonical identity hash — the one value every consumer must publish and
/// compare: the Ethereum block hash (keccak256 over the RLP header built from the header's
/// fork fields) for an OP block, the header's own stored hash otherwise. Callers must not
/// choose between the two themselves: that choice belongs here, once.
[[nodiscard]] crypto::HashType canonicalBlockHash(BlockHeader const& header);

/// A committed block's base fee under the lane rules. A NON_ETH header's optional base fee is
/// authoritative: OP-Stack headers are NON_ETH on every fork (rebuildOpEthHeader deliberately
/// leaves ethBlockVersion NON_ETH) and a pre-Canyon one (Bedrock-Delta) carries a base fee but no
/// withdrawalsRoot, so isOpEthereumBlock is not the gate here -- keying on it read 0 for those
/// heads. A native FISCO header has no base-fee field and yields 0. Eth-lane headers take the
/// London+ rule (0 pre-London).
[[nodiscard]] inline u256 blockBaseFee(BlockHeader const& header)
{
    if (header.ethBlockVersion() == EthBlockVersion::NON_ETH)
    {
        return header.baseFee().value_or(0);
    }
    if (static_cast<std::uint8_t>(header.ethBlockVersion()) <
        static_cast<std::uint8_t>(EthBlockVersion::LONDON))
    {
        return 0;
    }
    return header.baseFee().value_or(0);
}
}  // namespace bcos::protocol
