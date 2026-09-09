/**
 * Copyright (C) 2026 FISCO BCOS.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file EthEngineServiceMPTStateRootTest.cpp
 * @brief Engine-driven blocks must commit an MPT state root (not XOR) on L2/MPT chains, and
 *        block N > 0 must read the parent's published state root and publish its own header.
 */

#include "engine/bcos-engine/EngineMPTStateRoot.h"
#include "engine/test/unittests/engine/EthServiceStubs.h"

#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::engine::eth_test;

namespace
{
constexpr auto kBlockVersion = static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_16_0_VERSION);
constexpr std::string_view c_senderHex = "9015bca99e8d49107c33b2cac14013a8dfd2c1b0";

ledger::Features makeL2Features()
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    features.setActivationBlock(ledger::Features::Flag::feature_l2_ethereum_compat, 0);
    return features;
}

evmc_address decodeAddress(std::string_view hex)
{
    evmc_address address{};
    boost::algorithm::unhex(hex.begin(), hex.end(), address.bytes);
    return address;
}

struct L2MptStorageFixture
{
    RealGlobalStateBackendStorage backendStorage;
    RealGlobalCheckpointBackend checkpointBackend{backendStorage};
    RealGlobalStateStorage globalStorage{checkpointBackend};
    std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite = bcos::test::createNormalCryptoSuite();
    bcos::protocol::BlockFactory::Ptr blockFactory = bcos::test::createBlockFactory(cryptoSuite);
    ledger::LedgerConfig ledgerConfig;

    L2MptStorageFixture()
    {
        ledgerConfig.setFeatures(makeL2Features());
        auto address = decodeAddress(c_senderHex);
        ledger::account::EVMAccount account{backendStorage, address, false};
        task::syncWait(account.setBalance(bcos::u256("1000000000000000000000")));
        task::syncWait(account.setNonce("0"));
    }

    RealGlobalStateStorage::ViewType forkMutableView()
    {
        auto view = globalStorage.fork();
        view.newMutable();
        return view;
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(EthEngineServiceMPTStateRootTest)

BOOST_FIXTURE_TEST_CASE(l2GenesisUsesMptRootNotXor, L2MptStorageFixture)
{
    auto xorView = forkMutableView();
    auto xorHeader = blockFactory->blockHeaderFactory()->createBlockHeader();
    xorHeader->setNumber(0);
    xorHeader->setVersion(kBlockVersion);
    auto xorRoot = task::syncWait(bcos::scheduler_v1::xorStateRoot(
        xorView, xorHeader->version(), *cryptoSuite->hashImpl(), ledgerConfig.features()));

    auto mptView = forkMutableView();
    auto mptHeader = blockFactory->blockHeaderFactory()->createBlockHeader();
    mptHeader->setNumber(0);
    mptHeader->setVersion(kBlockVersion);
    auto mptRoot = task::syncWait(bcos::engine::engine_common::resolveEngineBlockStateRoot(
        mptView, *mptHeader, ledgerConfig, *cryptoSuite->hashImpl(), *blockFactory));

    BOOST_CHECK_NE(mptRoot, xorRoot);
}

BOOST_FIXTURE_TEST_CASE(nonL2ChainKeepsXorRoot, L2MptStorageFixture)
{
    ledger::LedgerConfig legacyConfig;
    auto view = forkMutableView();
    auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(0);
    header->setVersion(kBlockVersion);

    auto xorRoot = task::syncWait(bcos::scheduler_v1::xorStateRoot(
        view, header->version(), *cryptoSuite->hashImpl(), legacyConfig.features()));
    auto resolved = task::syncWait(bcos::engine::engine_common::resolveEngineBlockStateRoot(
        view, *header, legacyConfig, *cryptoSuite->hashImpl(), *blockFactory));
    BOOST_CHECK_EQUAL(resolved, xorRoot);
}

/// Block N > 0: the MPT build must chain onto the PARENT's published state root (not start
/// from the empty trie) and must publish its own header for the next block. Two chained
/// blocks: block 1 over the genesis empty root, then block 2 over block 1's real root. The
/// control proves the parent read is load-bearing — with no published parent header the build
/// must fail rather than silently fall back to the empty trie.
BOOST_FIXTURE_TEST_CASE(l2BlocksChainParentRootAndPublishHeader, L2MptStorageFixture)
{
    // Genesis owner writes block 0's header row (publishPendingBlockHeaderForMPT skips block 0
    // on purpose: the genesis row belongs to Ledger::buildGenesisBlock).
    {
        auto genesis = blockFactory->blockHeaderFactory()->createBlockHeader();
        genesis->setNumber(0);
        genesis->setVersion(kBlockVersion);
        genesis->setStateRoot(bcos::ledger::mpt::emptyRootHash());
        bcos::bytes buffer;
        genesis->encode(buffer);
        bcos::storage::Entry entry;
        entry.set(std::move(buffer));
        task::syncWait(bcos::storage2::writeOne(backendStorage,
            bcos::executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, std::string{"0"}},
            std::move(entry)));
    }
    auto const seedDelta = [&](RealGlobalStateStorage::ViewType& view, char const* balance) {
        // The MPT build scans only the view's mutable (delta) layer
        // (MPTBuilder.h: storage2::range(mutableStorage(flatView))), so each block seeds its
        // own change set; the parent root is the only thing read from the backend.
        ledger::account::EVMAccount account{view, decodeAddress(c_senderHex), false};
        task::syncWait(account.setBalance(bcos::u256(balance)));
    };
    auto const resolve = [&](RealGlobalStateStorage::ViewType& view,
                             bcos::protocol::BlockHeader& header,
                             ledger::LedgerConfig const& config) {
        return task::syncWait(bcos::engine::engine_common::resolveEngineBlockStateRoot(
            view, header, config, *cryptoSuite->hashImpl(), *blockFactory));
    };

    bcos::h256 root1;
    {
        auto view = forkMutableView();
        seedDelta(view, "2000000000000000000000");
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(1);
        header->setVersion(kBlockVersion);
        root1 = resolve(view, *header, ledgerConfig);
        BOOST_CHECK_NE(root1, bcos::ledger::mpt::emptyRootHash());
        BOOST_CHECK_EQUAL(header->stateRoot().hex(), root1.hex());
        task::syncWait(globalStorage.mergeView(std::move(view)));
    }

    {
        auto view = forkMutableView();
        seedDelta(view, "3000000000000000000000");
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(2);
        header->setVersion(kBlockVersion);
        auto const root2 = resolve(view, *header, ledgerConfig);

        // Golden literal over block 2's delta chained onto block 1's root.
        BOOST_CHECK_EQUAL(
            root2.hex(), "6685f6b217415406ff97369e3e73db6e75ccbdc3f7df61ca9f639ef5bee1e92b");
        BOOST_CHECK_EQUAL(header->stateRoot().hex(), root2.hex());
        BOOST_CHECK_NE(root2, bcos::ledger::mpt::emptyRootHash());
        BOOST_CHECK_NE(root2, root1);

        // The executed header is published under block 2 for the next build.
        auto published = task::syncWait(bcos::storage2::readOne(
            view, bcos::executor_v1::StateKeyView{
                      bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, std::string{"2"}}));
        BOOST_REQUIRE(published.has_value());
        auto const publishedBytes = published->get();
        auto decoded = blockFactory->blockHeaderFactory()->createBlockHeader();
        decoded->decode(bcos::bytesConstRef(
            reinterpret_cast<const bcos::byte*>(publishedBytes.data()), publishedBytes.size()));
        BOOST_CHECK_EQUAL(decoded->number(), 2);
        BOOST_CHECK_EQUAL(decoded->stateRoot().hex(), root2.hex());
    }

    // Control: the same block-2 delta with NO published parent header must fail — the build
    // cannot silently fall back to the empty trie.
    {
        L2MptStorageFixture control;
        auto view = control.forkMutableView();
        ledger::account::EVMAccount account{view, decodeAddress(c_senderHex), false};
        task::syncWait(account.setBalance(bcos::u256("3000000000000000000000")));
        auto header = control.blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(2);
        header->setVersion(kBlockVersion);
        BOOST_CHECK_THROW(
            task::syncWait(bcos::engine::engine_common::resolveEngineBlockStateRoot(view, *header,
                control.ledgerConfig, *control.cryptoSuite->hashImpl(), *control.blockFactory)),
            bcos::ledger::NotFoundBlockHeader);
    }
}

BOOST_AUTO_TEST_SUITE_END()
