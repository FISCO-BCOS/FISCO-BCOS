/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file GenesisStateRoot.cpp
 */
#include "GenesisStateRoot.h"
#include "mpt/Constants.h"
#include "mpt/HashBuilder.h"
#include "mpt/StorageValueCodec.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/lexical_cast.hpp>
#include <cstdint>

using namespace bcos;
using namespace bcos::ledger;

namespace
{
bcos::h256 keccak(bcos::bytesConstRef data)
{
    return crypto::keccak256Hash(data);
}

// storageRoot over one account's storage slots, as a secure trie:
//   key   = keccak256(slot bytes as configured)
//   value = RLP(value-with-leading-zero-bytes-trimmed)   (mpt::encodeStorageValue)
// Zero-valued slots are skipped (no-op in Ethereum state). Empty -> emptyRoot.
// The slot keccak stays over the raw configured bytes (not mpt::slotKeyHash, which
// right-aligns into a fixed 32 bytes) to keep genesis hashing byte-identical.
bcos::task::Task<bcos::h256> storageRootOf(std::vector<Alloc::State> const& storage)
{
    storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes> nodes;
    mpt::HashBuilder builder(nodes, mpt::emptyRootHash());
    bool anySlot = false;
    for (auto const& [slotHex, valueHex] : storage)
    {
        auto valueBytes = bcos::fromHex(valueHex);
        auto rlpValue = mpt::encodeStorageValue(bcos::ref(valueBytes));
        if (rlpValue.empty())
        {
            continue;  // zero value: not part of the storage trie
        }
        anySlot = true;
        auto slotBytes = bcos::fromHex(slotHex);
        auto slotKeyHash = keccak(bcos::bytesConstRef(slotBytes.data(), slotBytes.size()));
        co_await builder.put(slotKeyHash, std::move(rlpValue));
    }
    if (!anySlot)
    {
        co_return mpt::emptyRootHash();
    }
    co_return co_await builder.commit();
}
}  // namespace

bcos::task::Task<bcos::h256> bcos::ledger::computeGenesisStateRoot(GenesisConfig const& genesis)
{
    storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes> stateNodes;
    mpt::HashBuilder stateBuilder(stateNodes, mpt::emptyRootHash());

    for (auto const& alloc : genesis.m_allocs)
    {
        auto storageRoot = co_await storageRootOf(alloc.storage);

        auto codeBytes = bcos::fromHex(alloc.code);
        bcos::h256 codeHash = codeBytes.empty() ?
                                  mpt::emptyCodeHash() :
                                  keccak(bcos::bytesConstRef(codeBytes.data(), codeBytes.size()));

        // Ethereum StateAccount RLP: [nonce, balance, storageRoot, codeHash].
        // nonce/balance encode as minimal big-endian RLP integers (0 -> 0x80);
        // storageRoot/codeHash as 32-byte RLP strings (0xa0 || 32 bytes).
        // NodeConfig::loadAllocs already validated nonce fits uint64.
        uint64_t nonce = alloc.nonce.empty() ? 0 : boost::lexical_cast<uint64_t>(alloc.nonce);
        bcos::bytes accountRlp;
        codec::rlp::encode(accountRlp, nonce, alloc.balance, storageRoot, codeHash);

        // Secure state trie: leaf key = keccak256(address20).
        auto addrBytes = bcos::fromHex(alloc.address);
        auto addrKeyHash = keccak(bcos::bytesConstRef(addrBytes.data(), addrBytes.size()));
        co_await stateBuilder.put(addrKeyHash, std::move(accountRlp));
    }

    co_return co_await stateBuilder.commit();
}
