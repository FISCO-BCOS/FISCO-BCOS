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
 * @brief the analyzed-code cache must not leak code between storage views
 * @file ExecutableCacheIsolationTest.cpp
 */

#include "../bcos-transaction-executor/vm/HostContext.h"
#include "TestMemoryStorage.h"
#include "bcos-concepts/ByteBuffer.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Common.h"
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
evmc_address makeAddress(uint8_t tag)
{
    evmc_address address{};
    address.bytes[sizeof(address.bytes) - 1] = tag;
    return address;
}

// Minimal but distinct runtime blobs; evmone analyzes arbitrary bytes.
const bytes CODE_A{0x60, 0x01, 0x60, 0x00, 0x55, 0x00};  // PUSH1 1 PUSH1 0 SSTORE STOP
const bytes CODE_B{0x60, 0x02, 0x60, 0x00, 0x55, 0x00};  // PUSH1 2 PUSH1 0 SSTORE STOP

struct ExecutableCacheIsolationFixture
{
    crypto::Hash::Ptr hashImpl = std::make_shared<crypto::Keccak256>();

    ExecutableCacheIsolationFixture()
    {
        if (!bcos::executor::GlobalHashImpl::g_hashImpl)
        {
            bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<crypto::Keccak256>();
        }
    }

    // Write `code` at `address` exactly the way a deployment does: the bytes go
    // into s_code_binary keyed by their hash, the account table records the hash.
    void seedCode(MutableStorage& storage, const evmc_address& address, const bytes& code) const
    {
        ledger::account::EVMAccount account(storage, address, false);
        auto codeHash = hashImpl->hash(bytesConstRef(code.data(), code.size()));
        syncWait(account.create());
        syncWait(account.setCode(code, std::string{}, codeHash));
    }
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(ExecutableCacheIsolation, ExecutableCacheIsolationFixture)

// A contract deployed in a storage view that is later discarded (eth_call,
// eth_estimateGas, a block that fails sync verification, an abandoned consensus
// proposal) must not become visible to any other view. Keying the process-wide
// executable cache by address made it visible, so EXTCODESIZE reported code for
// an address that holds none and the node diverged from the network.
BOOST_AUTO_TEST_CASE(discardedViewDoesNotPublishCode)
{
    auto address = makeAddress(0xa1);

    MutableStorage speculativeView;
    seedCode(speculativeView, address, CODE_A);
    BOOST_REQUIRE(syncWait(getExecutable(speculativeView, address, EVMC_CANCUN, false)) != nullptr);

    // A different view that never saw that deployment. The speculative view is
    // simply dropped here, exactly as BaselineScheduler drops a forked view it
    // never pushes.
    MutableStorage consensusView;
    BOOST_CHECK(syncWait(getExecutable(consensusView, address, EVMC_CANCUN, false)) == nullptr);
}

// The same address holding different code in two views must resolve to that
// view's code, never to whichever one populated the cache first.
BOOST_AUTO_TEST_CASE(codeIsResolvedPerView)
{
    auto address = makeAddress(0xa2);

    MutableStorage firstView;
    seedCode(firstView, address, CODE_A);
    MutableStorage secondView;
    seedCode(secondView, address, CODE_B);

    auto first = syncWait(getExecutable(firstView, address, EVMC_CANCUN, false));
    auto second = syncWait(getExecutable(secondView, address, EVMC_CANCUN, false));

    BOOST_REQUIRE(first && first->m_code);
    BOOST_REQUIRE(second && second->m_code);
    BOOST_CHECK_EQUAL(
        first->m_code->get(), std::string_view((const char*)CODE_A.data(), CODE_A.size()));
    BOOST_CHECK_EQUAL(
        second->m_code->get(), std::string_view((const char*)CODE_B.data(), CODE_B.size()));
}

// The cache is still doing its job, and now keys on content: proxies sharing one
// runtime blob (the OpenZeppelin BeaconProxy / ERC1967 pattern) share a single
// analysis instead of taking one LRU slot each.
BOOST_AUTO_TEST_CASE(identicalCodeSharesOneAnalysis)
{
    MutableStorage storage;
    auto first = makeAddress(0xb1);
    auto second = makeAddress(0xb2);
    seedCode(storage, first, CODE_A);
    seedCode(storage, second, CODE_A);

    auto firstExecutable = syncWait(getExecutable(storage, first, EVMC_CANCUN, false));
    auto secondExecutable = syncWait(getExecutable(storage, second, EVMC_CANCUN, false));

    BOOST_REQUIRE(firstExecutable);
    BOOST_CHECK_EQUAL(firstExecutable.get(), secondExecutable.get());
}

// evmone::baseline::analyze() is revision-specific (VMFactory::create passes the
// revision through), so the revision belongs in the key. HostContext currently
// pins m_revision to EVMC_CANCUN, so no production path varies it today; this
// guards the key against the day one does.
BOOST_AUTO_TEST_CASE(revisionIsPartOfTheCacheKey)
{
    MutableStorage storage;
    auto address = makeAddress(0xc1);
    seedCode(storage, address, CODE_B);

    auto shanghai = syncWait(getExecutable(storage, address, EVMC_SHANGHAI, false));
    auto cancun = syncWait(getExecutable(storage, address, EVMC_CANCUN, false));

    BOOST_REQUIRE(shanghai);
    BOOST_REQUIRE(cancun);
    BOOST_CHECK_NE(shanghai.get(), cancun.get());
}

// An account with no code hash resolves to no executable and publishes nothing:
// this is the path every EOA and every never-touched address takes.
BOOST_AUTO_TEST_CASE(accountWithoutCodeYieldsNoExecutable)
{
    MutableStorage storage;
    auto address = makeAddress(0xd1);
    ledger::account::EVMAccount account(storage, address, false);
    syncWait(account.create());

    BOOST_CHECK(syncWait(getExecutable(storage, address, EVMC_CANCUN, false)) == nullptr);
}

// Contracts deployed before code was split into s_code_binary keep their bytes
// in the account table's own code field. EVMAccount::code() falls back to it and
// getExecutable must stay on that path.
BOOST_AUTO_TEST_CASE(legacyCodeFieldIsStillResolved)
{
    MutableStorage storage;
    auto address = makeAddress(0xd2);
    auto codeHash = hashImpl->hash(bytesConstRef(CODE_A.data(), CODE_A.size()));

    // Account table records the hash and the code, but s_code_binary is empty.
    ledger::account::EVMAccount account(storage, address, false);
    syncWait(account.create());
    auto tableName = std::string(syncWait(account.path()));
    syncWait(storage2::writeOne(storage,
        executor_v1::StateKey{tableName, ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH},
        storage::Entry{bcos::concepts::bytebuffer::toView(codeHash)}));
    syncWait(storage2::writeOne(storage,
        executor_v1::StateKey{tableName, ledger::ACCOUNT_TABLE_FIELDS::CODE},
        storage::Entry{std::string_view((const char*)CODE_A.data(), CODE_A.size())}));

    auto executable = syncWait(getExecutable(storage, address, EVMC_CANCUN, false));
    BOOST_REQUIRE(executable);
    BOOST_REQUIRE(executable->m_code);
    BOOST_CHECK_EQUAL(
        executable->m_code->get(), std::string_view((const char*)CODE_A.data(), CODE_A.size()));
}

BOOST_AUTO_TEST_SUITE_END()
