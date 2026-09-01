/*
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
 * @brief an account that only holds a balance must look codeless to the EVM
 * @file EOACodeVisibilityTest.cpp
 */

#include "../bcos-transaction-executor/precompiled/PrecompiledManager.h"
#include "../bcos-transaction-executor/vm/HostContext.h"
#include "TestMemoryStorage.h"
#include "bcos-executor/src/Common.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-executor/RollbackableStorage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <evmc/evmc.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>

using namespace bcos;
using namespace bcos::task;
using namespace bcos::executor_v1;
using namespace bcos::executor_v1::hostcontext;

namespace
{
evmc_address eoaAddress()
{
    evmc_address address{};
    address.bytes[sizeof(address.bytes) - 1] = 0xe0;
    return address;
}

const bytes REAL_CODE{0x60, 0x01, 0x60, 0x00, 0x55, 0x00};

struct EOACodeVisibilityFixture
{
    crypto::Hash::Ptr hashImpl = std::make_shared<crypto::Keccak256>();
    MutableStorage storage;
    Rollbackable<MutableStorage> rollbackableStorage{storage};
    using TransientStorageType = storage2::memory_storage::MemoryStorage<StateKey, StateValue,
        storage2::memory_storage::Attribute(
            storage2::memory_storage::ORDERED | storage2::memory_storage::LOGICAL_DELETION)>;
    TransientStorageType transientStorage;
    Rollbackable<TransientStorageType> rollbackableTransientStorage{transientStorage};
    std::optional<PrecompiledManager> precompiledManager;
    ledger::LedgerConfig ledgerConfig;
    bcostars::protocol::BlockHeaderImpl blockHeader;
    int64_t seq = 0;

    EOACodeVisibilityFixture()
    {
        if (!bcos::executor::GlobalHashImpl::g_hashImpl)
        {
            bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
        }
        precompiledManager.emplace(hashImpl);
        blockHeader.setVersion(
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_17_1_VERSION));
        blockHeader.calculateHash(*hashImpl);
    }

    // Write `code` at `address` the way a deployment (or an account creation)
    // does: bytes into s_code_binary keyed by their hash, hash into the account
    // table.
    void seedCode(const evmc_address& address, const bytes& code)
    {
        ledger::account::EVMAccount account(rollbackableStorage, address, false);
        auto codeHash = hashImpl->hash(bytesConstRef(code.data(), code.size()));
        syncWait(account.create());
        syncWait(account.setCode(code, std::string{}, codeHash));
    }

    // The first two segments match what AccountPrecompiled /
    // AccountManagerPrecompiled / BalancePrecompiled write when they materialise
    // an account that only holds a balance ("[PRECOMPILED],<ACCOUNT_ADDRESS>,").
    // They use getAccountTableName for the third segment and this uses the
    // contract table path; the predicate only looks at the prefix.
    void seedAccountMarker(const evmc_address& address)
    {
        ledger::account::EVMAccount account(rollbackableStorage, address, false);
        auto tableName = std::string(syncWait(account.path()));
        auto marker =
            precompiled::getDynamicPrecompiledCodeString(precompiled::ACCOUNT_ADDRESS, tableName);
        seedCode(address, bytes(marker.begin(), marker.end()));
    }

    void enableFilter(bool enable)
    {
        ledger::Features features;
        if (enable)
        {
            features.set(ledger::Features::Flag::bugfix_v1_eoa_as_contract);
        }
        ledgerConfig.setFeatures(features);
    }

    auto makeHostContext(const evmc_address& address)
    {
        // HostContext keeps a reference_wrapper to origin, so it must outlive
        // the returned context; message is stored by value and need not.
        static evmc_address origin{};
        evmc_message message{};
        message.kind = EVMC_CALL;
        message.recipient = address;
        message.code_address = address;
        return HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>(
            rollbackableStorage, rollbackableTransientStorage, blockHeader, message, origin, "", 0,
            seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0, bcos::task::syncWait);
    }
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(EOACodeVisibility, EOACodeVisibilityFixture)

// An account that only holds a balance is stored as a dynamic account
// precompiled. Its marker string is an internal dispatch token; the EVM must see
// an account with no code, or `addr.code.length != 0` is true for every funded
// wallet - which is what made OpenZeppelin's ERC-721 safe transfers call
// onERC721Received on plain wallets.
BOOST_AUTO_TEST_CASE(balanceOnlyAccountHasNoCodeForTheEvm)
{
    auto address = eoaAddress();
    seedAccountMarker(address);
    enableFilter(true);
    auto hostContext = makeHostContext(address);

    BOOST_CHECK(!syncWait(hostContext.code(address)));
    BOOST_CHECK_EQUAL(syncWait(hostContext.codeSizeAt(address)), 0U);
    BOOST_CHECK_EQUAL(syncWait(hostContext.codeHashAt(address)), h256{});
}

// The filter is consensus visible, so it must stay off for chains below 3.17.1:
// replaying their history has to reproduce the roots they already committed.
BOOST_AUTO_TEST_CASE(markerStaysVisibleWithoutTheFeature)
{
    auto address = eoaAddress();
    seedAccountMarker(address);
    enableFilter(false);
    auto hostContext = makeHostContext(address);

    auto code = syncWait(hostContext.code(address));
    BOOST_REQUIRE(code);
    BOOST_CHECK_GT(code->get().size(), 0U);
    BOOST_CHECK_GT(syncWait(hostContext.codeSizeAt(address)), 0U);
    BOOST_CHECK_NE(syncWait(hostContext.codeHashAt(address)), h256{});
}

// Real contract code is untouched by the filter.
BOOST_AUTO_TEST_CASE(contractCodeIsUnaffected)
{
    evmc_address address{};
    address.bytes[sizeof(address.bytes) - 1] = 0xc0;
    seedCode(address, REAL_CODE);
    enableFilter(true);
    auto hostContext = makeHostContext(address);

    auto code = syncWait(hostContext.code(address));
    BOOST_REQUIRE(code);
    BOOST_CHECK_EQUAL(code->get().size(), REAL_CODE.size());
    BOOST_CHECK_EQUAL(syncWait(hostContext.codeSizeAt(address)), REAL_CODE.size());
    BOOST_CHECK_NE(syncWait(hostContext.codeHashAt(address)), h256{});
}

// The filter must only hide the marker from the code-reading path. This asserts
// the step executeCall depends on - getExecutable still returning the marker -
// rather than dispatch itself; processDynamicPrecompiled consumes that same
// m_executable->m_code to route the call to the account precompiled.
BOOST_AUTO_TEST_CASE(dispatchStillSeesTheMarker)
{
    auto address = eoaAddress();
    seedAccountMarker(address);
    enableFilter(true);

    auto executable = syncWait(getExecutable(rollbackableStorage, address, EVMC_CANCUN, false));
    BOOST_REQUIRE(executable);
    BOOST_REQUIRE(executable->m_code);
    BOOST_CHECK(bcos::precompiled::matchDynamicAccountCode(executable->m_code->get()));
}

// The activation version must not move: chains at 3.17.0 and below committed
// blocks with the marker visible.
BOOST_AUTO_TEST_CASE(featureActivatesAt3_17_1)
{
    ledger::Features at3_17_0;
    at3_17_0.setGenesisFeatures(bcos::protocol::BlockVersion::V3_17_0_VERSION);
    BOOST_CHECK(!at3_17_0.get(ledger::Features::Flag::bugfix_v1_eoa_as_contract));

    ledger::Features at3_17_1;
    at3_17_1.setGenesisFeatures(bcos::protocol::BlockVersion::V3_17_1_VERSION);
    BOOST_CHECK(at3_17_1.get(ledger::Features::Flag::bugfix_v1_eoa_as_contract));
}

// BaselineScheduler::getCode (eth_getCode) shares this predicate with
// HostContext::code. Asserting it directly covers the RPC half without standing
// up a scheduler.
BOOST_AUTO_TEST_CASE(sharedPredicateGatesOnTheFeature)
{
    ledger::account::EVMAccount account(rollbackableStorage, eoaAddress(), false);
    auto tableName = std::string(syncWait(account.path()));
    auto marker =
        precompiled::getDynamicPrecompiledCodeString(precompiled::ACCOUNT_ADDRESS, tableName);

    ledger::Features off;
    ledger::Features on;
    on.set(ledger::Features::Flag::bugfix_v1_eoa_as_contract);

    BOOST_CHECK(precompiled::hideDynamicAccountCode(on, marker));
    BOOST_CHECK(!precompiled::hideDynamicAccountCode(off, marker));

    // Real bytecode is never hidden.
    BOOST_CHECK(!precompiled::hideDynamicAccountCode(
        on, std::string_view((const char*)REAL_CODE.data(), REAL_CODE.size())));

    // Table precompileds carry the same [PRECOMPILED] envelope under a different
    // address and must keep their code.
    auto tableMarker = precompiled::getDynamicPrecompiledCodeString(
        precompiled::KV_TABLE_ADDRESS, "/tables/t_test");
    BOOST_CHECK(!precompiled::hideDynamicAccountCode(on, tableMarker));
}

BOOST_AUTO_TEST_SUITE_END()
