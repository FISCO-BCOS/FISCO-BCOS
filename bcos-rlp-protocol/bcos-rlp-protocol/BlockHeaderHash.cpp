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
 * @file BlockHeaderHash.cpp
 * @brief The single source of a block's identity hash across the three lanes.
 */

#include "BlockHeaderHash.h"
#include <bcos-rlp-protocol/EthBlockHeader.h>

bool bcos::protocol::isOpEthereumBlock(bcos::protocol::BlockHeader const& header)
{
    if (header.ethBlockVersion() != bcos::protocol::EthBlockVersion::NON_ETH)
    {
        return false;
    }
    return header.withdrawalsRoot().has_value() && header.baseFee().has_value();
}

bcos::crypto::HashType bcos::protocol::canonicalBlockHash(bcos::protocol::BlockHeader const& header)
{
    if (bcos::protocol::isOpEthereumBlock(header))
    {
        return bcos::protocol::EthBlockHeader::computeHash(header);
    }
    return header.hash();
}
