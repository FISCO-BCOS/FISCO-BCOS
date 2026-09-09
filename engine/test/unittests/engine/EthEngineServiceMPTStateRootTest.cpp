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
 * @brief Engine-driven blocks must commit an MPT state root (not XOR) on L2/MPT chains.
 */

#include "engine/bcos-engine/EngineMPTStateRoot.h"
#include "engine/test/unittests/engine/EthServiceStubs.h"

#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
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
    auto xorRoot = task::syncWait(bcos::engine::engine_common::xorStateRoot(
        xorView, xorHeader->version(), *cryptoSuite->hashImpl()));

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

    auto xorRoot = task::syncWait(bcos::engine::engine_common::xorStateRoot(
        view, header->version(), *cryptoSuite->hashImpl()));
    auto resolved = task::syncWait(bcos::engine::engine_common::resolveEngineBlockStateRoot(
        view, *header, legacyConfig, *cryptoSuite->hashImpl(), *blockFactory));
    BOOST_CHECK_EQUAL(resolved, xorRoot);
}

BOOST_AUTO_TEST_SUITE_END()
