/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * @brief Before Spurious Dragon (EIP-161) a touched empty account is a leaf: a zero-value CALL
 *        to a fresh address leaves {nonce 0, balance 0, keccak256(""), emptyRoot} in the state
 *        trie (EEST frontier/precompiles fixtures pin it). buildAndCollect's clearEmptyAccounts
 *        switch selects the era; the default keeps the EIP-161 clearing.
 */
#include "TestHelpers.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-ledger/mpt/Account.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/MPTBuilder.h>
#include <bcos-ledger/mpt/MPTReadView.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>

namespace bcos::ledger::mpt::test
{
BOOST_AUTO_TEST_SUITE(MPTBuilderPreSpuriousDragonSuite)

namespace
{
using NodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;

bcos::storage::Entry codeHashEntry(bcos::h256 const& hash)
{
    return makeEntry(
        std::string_view{reinterpret_cast<char const*>(hash.data()), bcos::h256::SIZE});
}

/// The rows EthereumState::applyToStorage writes for a touched empty account before Spurious
/// Dragon (phase 1 writes every modified account there; from that fork on it skips the empty
/// ones).
void writeTouchedEmptyAccount(auto& view, bcos::Address const& addr)
{
    writeFlatRow(view, accountFieldKey(addr, ROW_NONCE), makeEntry("0"));
    writeFlatRow(view, accountFieldKey(addr, ROW_BALANCE), makeEntry("0"));
    writeFlatRow(view, accountFieldKey(addr, ROW_CODE_HASH), codeHashEntry(emptyCodeHash()));
}
}  // namespace

// Frontier..Tangerine Whistle: the touched empty account becomes a leaf with Yellow Paper
// defaults, and the root is the trie holding exactly that leaf.
BOOST_AUTO_TEST_CASE(PreSpuriousDragonTouchedEmptyAccountIsALeaf)
{
    NodeStorage storage;
    auto const addr = makeAddress(0x01);  // the ecrecover precompile, the classic case
    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeTouchedEmptyAccount(view, addr);

    auto output = bcos::task::syncWait(buildAndCollect(storage, emptyRootHash(), view,
        /*l2Mode=*/false, bcos::ledger::account::AddressTableMode::Hex,
        /*trackRefCounts=*/false, /*clearEmptyAccounts=*/false));

    Account expected;  // nonce 0, balance 0, emptyCodeHash, emptyRoot
    BOOST_CHECK(output.stateRoot == referenceRoot({{accountKeyHash(addr), expected.encode()}}));
    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    auto leaf = bcos::task::syncWait(readView.readAccount(addr));
    BOOST_REQUIRE(leaf.has_value());
    BOOST_CHECK_EQUAL(leaf->nonce, bcos::u256(0));
    BOOST_CHECK_EQUAL(leaf->balance, bcos::u256(0));
    BOOST_CHECK(leaf->codeHash == emptyCodeHash());
}

// The same rows under EIP-161 (the default) write nothing: the switch, not the rows, decides.
BOOST_AUTO_TEST_CASE(SpuriousDragonClearsTheSameTouch)
{
    NodeStorage storage;
    auto const addr = makeAddress(0x01);
    FlatBackendStorage flatBackend;
    auto view = makeFlatView(flatBackend);
    writeTouchedEmptyAccount(view, addr);

    auto output = bcos::task::syncWait(buildAndCollect(storage, emptyRootHash(), view,
        /*l2Mode=*/false, bcos::ledger::account::AddressTableMode::Hex));

    BOOST_CHECK(output.stateRoot == emptyRootHash());
    MPTReadView<NodeStorage> readView(storage, output.stateRoot);
    BOOST_CHECK(!bcos::task::syncWait(readView.readAccount(addr)).has_value());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ledger::mpt::test
