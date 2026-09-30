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
 * @file CanonicalReads.cpp
 */
#include "CanonicalReads.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-ledger/LedgerMethods.h"
#include <bcos-utilities/Error.h>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
[[noreturn]] void throwNotOnCanonicalChain(protocol::BlockNumber blockNumber)
{
    BOOST_THROW_EXCEPTION(BCOS_ERROR(bcos::ledger::LedgerError::GetStorageError,
        "block " + std::to_string(blockNumber) + " is not on the canonical chain"));
}
}  // namespace

task::Task<protocol::BlockNumber> bcos::rpc::canonicalLatestNumber(NodeService& nodeService)
{
    if (auto const& reader = nodeService.opCanonicalReader())
    {
        co_return (co_await reader->head()).number;
    }
    co_return co_await ledger::getCurrentBlockNumber(*nodeService.ledger());
}

task::Task<protocol::Block::Ptr> bcos::rpc::canonicalBlockByNumber(
    NodeService& nodeService, protocol::BlockNumber blockNumber, int32_t blockFlag)
{
    if (auto const& reader = nodeService.opCanonicalReader())
    {
        if (blockNumber > (co_await reader->finalized()).number)
        {
            auto const hash = co_await reader->canonicalHashAt(blockNumber);
            if (!hash)
            {
                throwNotOnCanonicalChain(blockNumber);
            }
            auto block = co_await reader->unfinalizedBlock(*hash, blockFlag);
            if (!block)
            {
                // Finalized between the two lookups: the ledger has it now.
                co_return co_await ledger::getBlockData(
                    *nodeService.ledger(), blockNumber, blockFlag);
            }
            co_return block;
        }
    }
    co_return co_await ledger::getBlockData(*nodeService.ledger(), blockNumber, blockFlag);
}

task::Task<protocol::Block::Ptr> bcos::rpc::canonicalBlockByHash(
    NodeService& nodeService, crypto::HashType const& blockHash, int32_t blockFlag)
{
    if (auto const& reader = nodeService.opCanonicalReader())
    {
        if (reader->unfinalizedNumberOf(blockHash))
        {
            if (auto block = co_await reader->unfinalizedBlock(blockHash, blockFlag))
            {
                co_return block;
            }
            // Finalized (or pruned) between the two lookups: fall through to the ledger.
        }
    }
    auto const number = co_await ledger::getBlockNumber(*nodeService.ledger(), blockHash);
    co_return co_await ledger::getBlockData(*nodeService.ledger(), number, blockFlag);
}

task::Task<crypto::HashType> bcos::rpc::canonicalBlockHashAt(
    NodeService& nodeService, protocol::BlockNumber blockNumber)
{
    if (auto const& reader = nodeService.opCanonicalReader())
    {
        if (blockNumber > (co_await reader->finalized()).number)
        {
            auto const hash = co_await reader->canonicalHashAt(blockNumber);
            if (!hash)
            {
                // Same contract as the ledger's getBlockHash for a missing row.
                throwNotOnCanonicalChain(blockNumber);
            }
            co_return *hash;
        }
    }
    co_return co_await ledger::getBlockHash(*nodeService.ledger(), blockNumber);
}
