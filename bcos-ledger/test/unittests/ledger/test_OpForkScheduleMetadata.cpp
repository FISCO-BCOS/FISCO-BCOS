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
 * @file test_OpForkScheduleMetadata.cpp
 * @brief SYS_CHAIN_METADATA op-fork-schedule triple persist / resolve / fail-closed.
 */
#include "L2GenesisTestStorage.h"
#include "bcos-framework/ledger/ChainMetadata.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/ledger/OpForkScheduleCodec.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-ledger/Ledger.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <boost/test/unit_test.hpp>
#include <array>
#include <memory>
#include <set>
#include <string>
#include <string_view>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos::test
{
namespace
{
constexpr char const* c_isthmusJovianSchedule = "0:isthmus,1764691201:jovian";
// Full op-geth EL fork chain (no delta): the shape a real OP Mainnet/Base
// genesis produces once S1 emits it.
constexpr char const* c_officialHistorySchedule =
    "0:regolith,1000:canyon,2000:ecotone,3000:fjord,4000:granite,"
    "5000:holocene,6000:isthmus,7000:jovian,8000:karst";

struct OpForkScheduleMetadataFixture
{
    OpForkScheduleMetadataFixture()
    {
        m_blockFactory = createBlockFactory(createNormalCryptoSuite());
    }

    BlockFactory::Ptr m_blockFactory;

    static LedgerConfig emptyLedgerConfig()
    {
        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);
        return param;
    }

    static GenesisConfig scheduleGenesis(std::string_view schedule)
    {
        GenesisConfig genesisConfig;
        genesisConfig.m_txGasLimit = 3000000000;
        genesisConfig.m_compatibilityVersion =
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_18_0_VERSION);
        genesisConfig.m_chainID = "1";
        genesisConfig.m_groupID = "group0";
        genesisConfig.m_opstackForkSchedule = std::string(schedule);
        return genesisConfig;
    }
};

bool messageContains(std::exception const& e, std::string_view needle)
{
    return std::string_view(e.what()).find(needle) != std::string_view::npos;
}

task::Task<void> writeMetadataRow(auto& storage, std::string_view key, std::string_view value)
{
    storage::Entry entry;
    entry.set(value);
    co_await storage2::writeOne(storage,
        executor_v1::StateKey(std::string_view(SYS_CHAIN_METADATA), key), std::move(entry));
}
}  // namespace

BOOST_FIXTURE_TEST_SUITE(OpForkScheduleMetadataTest, OpForkScheduleMetadataFixture)

BOOST_AUTO_TEST_CASE(genesisPersistsScheduleMetadataTriple)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(
            *ledger, scheduleGenesis(c_isthmusJovianSchedule), emptyLedgerConfig()));

        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        const auto ledgerGenesisHash = block->blockHeader()->hash();

        const auto metadata = co_await readOpForkScheduleMetadata(*storage, ledgerGenesisHash);
        BOOST_REQUIRE(metadata.has_value());
        BOOST_CHECK_EQUAL(metadata->schedule, c_isthmusJovianSchedule);
        BOOST_CHECK_EQUAL(metadata->genesisHash, ledgerGenesisHash);
        // Hardcoded keccak256("0:isthmus,1764691201:jovian") — not derived from the
        // function under test (F12).
        constexpr char const* c_isthmusJovianScheduleHash =
            "ee13c471cf47a2a991c84a5790834730611bcca5e68c951e0d05c519a79567a7";
        BOOST_CHECK_EQUAL(metadata->scheduleHash.hex(), c_isthmusJovianScheduleHash);
        BOOST_CHECK_EQUAL(
            keccakOpForkScheduleHash(c_isthmusJovianSchedule).hex(), c_isthmusJovianScheduleHash);

        BOOST_CHECK_EQUAL(
            resolveOpForkScheduleCanonical(metadata, std::nullopt, false, ledgerGenesisHash),
            c_isthmusJovianSchedule);
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(hashMismatchFailClosed)
{
    const auto goodHash = keccakOpForkScheduleHash(c_isthmusJovianSchedule);
    auto badHash = goodHash;
    badHash[0] ^= 0x01;

    OpForkScheduleMetadata stored{
        .schedule = c_isthmusJovianSchedule,
        .scheduleHash = badHash,
        .genesisHash = HashType{},
    };

    BOOST_CHECK_EXCEPTION(
        (void)resolveOpForkScheduleCanonical(stored, std::nullopt, true, HashType{}),
        InvalidOpForkSchedule,
        [](InvalidOpForkSchedule const& e) { return messageContains(e, "hash mismatch"); });

    task::syncWait([this, badHash]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(
            *ledger, scheduleGenesis(c_isthmusJovianSchedule), emptyLedgerConfig()));
        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        const auto ledgerGenesisHash = block->blockHeader()->hash();

        co_await writeMetadataRow(*storage, OP_FORK_SCHEDULE_HASH_KEY, badHash.hex());

        bool threw = false;
        try
        {
            (void)co_await readOpForkScheduleMetadata(*storage, ledgerGenesisHash);
        }
        catch (InvalidOpForkSchedule const& e)
        {
            threw = true;
            BOOST_CHECK(messageContains(e, "hash mismatch"));
        }
        BOOST_CHECK(threw);
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(genesisBranchReturnsNormalizedCanonical)
{
    BOOST_CHECK_EQUAL(
        resolveOpForkScheduleCanonical(std::nullopt, std::string{"0:Isthmus"}, false, HashType{}),
        "0:isthmus");
}

BOOST_AUTO_TEST_CASE(storedBranchReturnsNormalizedCanonical)
{
    constexpr char const* leadingZero = "00:isthmus";
    OpForkScheduleMetadata storedLeadingZero{
        .schedule = leadingZero,
        .scheduleHash = keccakOpForkScheduleHash(leadingZero),
        .genesisHash = HashType{},
    };
    BOOST_CHECK_EQUAL(
        resolveOpForkScheduleCanonical(storedLeadingZero, std::nullopt, false, HashType{}),
        "0:isthmus");

    constexpr char const* mixedCase = "0:Isthmus";
    OpForkScheduleMetadata storedMixedCase{
        .schedule = mixedCase,
        .scheduleHash = keccakOpForkScheduleHash(mixedCase),
        .genesisHash = HashType{},
    };
    BOOST_CHECK_EQUAL(
        resolveOpForkScheduleCanonical(storedMixedCase, std::nullopt, false, HashType{}),
        "0:isthmus");
}

BOOST_AUTO_TEST_CASE(emptyMetadataFallsBackToLegacy)
{
    BOOST_CHECK_EQUAL(resolveOpForkScheduleCanonical(std::nullopt, std::nullopt, true, HashType{}),
        std::string(c_legacyJovianCanonical));
    BOOST_CHECK_EQUAL(resolveOpForkScheduleCanonical(std::nullopt, std::nullopt, false, HashType{}),
        std::string(c_legacyIsthmusCanonical));
}

BOOST_AUTO_TEST_CASE(storedScheduleDivergesFromGenesis)
{
    BOOST_CHECK(!storedOpForkScheduleDivergesFromGenesis(c_legacyIsthmusCanonical, std::nullopt));
    BOOST_CHECK(!storedOpForkScheduleDivergesFromGenesis(
        c_legacyIsthmusCanonical, std::string{"0:Isthmus"}));
    BOOST_CHECK(storedOpForkScheduleDivergesFromGenesis(
        c_legacyIsthmusCanonical, std::string{c_legacyJovianCanonical}));
    BOOST_CHECK(!storedOpForkScheduleDivergesFromGenesis("00:isthmus", std::string{"0:isthmus"}));
    BOOST_CHECK(!storedOpForkScheduleDivergesFromGenesis("0:Isthmus", std::string{"0:isthmus"}));
    BOOST_CHECK(storedOpForkScheduleDivergesFromGenesis("0:isthmus", std::string{"0:jovian"}));
}

BOOST_AUTO_TEST_CASE(partialTripleIsNotAbsent)
{
    const auto genesisHash = HashType{};
    OpForkScheduleMetadataRows oneOfThree;
    oneOfThree.schedule = c_isthmusJovianSchedule;
    BOOST_CHECK_EXCEPTION((void)validateOpForkScheduleMetadataRows(oneOfThree, genesisHash),
        InvalidOpForkSchedule,
        [](InvalidOpForkSchedule const& e) { return messageContains(e, "partial"); });

    OpForkScheduleMetadataRows twoOfThree;
    twoOfThree.schedule = c_isthmusJovianSchedule;
    twoOfThree.scheduleHash = keccakOpForkScheduleHash(c_isthmusJovianSchedule).hex();
    BOOST_CHECK_EXCEPTION((void)validateOpForkScheduleMetadataRows(twoOfThree, genesisHash),
        InvalidOpForkSchedule,
        [](InvalidOpForkSchedule const& e) { return messageContains(e, "partial"); });

    task::syncWait([]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        co_await writeMetadataRow(*storage, OP_FORK_SCHEDULE_KEY, c_isthmusJovianSchedule);

        bool oneKeyThrew = false;
        try
        {
            auto const maybe = co_await readOpForkScheduleMetadata(*storage, HashType{});
            BOOST_CHECK(!maybe.has_value());
        }
        catch (InvalidOpForkSchedule const& e)
        {
            oneKeyThrew = true;
            BOOST_CHECK(messageContains(e, "partial"));
        }
        BOOST_CHECK(oneKeyThrew);

        co_await writeMetadataRow(*storage, OP_FORK_SCHEDULE_HASH_KEY,
            keccakOpForkScheduleHash(c_isthmusJovianSchedule).hex());

        bool twoKeysThrew = false;
        try
        {
            auto const maybe = co_await readOpForkScheduleMetadata(*storage, HashType{});
            BOOST_CHECK(!maybe.has_value());
        }
        catch (InvalidOpForkSchedule const& e)
        {
            twoKeysThrew = true;
            BOOST_CHECK(messageContains(e, "partial"));
        }
        BOOST_CHECK(twoKeysThrew);
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(genesisBindingMismatchFailClosed)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(
            *ledger, scheduleGenesis(c_isthmusJovianSchedule), emptyLedgerConfig()));
        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        const auto ledgerGenesisHash = block->blockHeader()->hash();
        auto wrongGenesisHash = ledgerGenesisHash;
        wrongGenesisHash[0] ^= 0x01;

        bool readThrew = false;
        try
        {
            (void)co_await readOpForkScheduleMetadata(*storage, wrongGenesisHash);
        }
        catch (InvalidOpForkSchedule const& e)
        {
            readThrew = true;
            BOOST_CHECK(messageContains(e, "genesis binding mismatch"));
        }
        BOOST_CHECK(readThrew);

        const auto metadata = co_await readOpForkScheduleMetadata(*storage, ledgerGenesisHash);
        BOOST_REQUIRE(metadata.has_value());
        BOOST_CHECK_EXCEPTION(
            (void)resolveOpForkScheduleCanonical(metadata, std::nullopt, true, wrongGenesisHash),
            InvalidOpForkSchedule, [](InvalidOpForkSchedule const& e) {
                return messageContains(e, "genesis binding mismatch");
            });
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(badHexIsInvalidOpForkSchedule)
{
    const auto genesisHash = HashType{};
    OpForkScheduleMetadataRows badScheduleHash;
    badScheduleHash.schedule = c_isthmusJovianSchedule;
    badScheduleHash.scheduleHash = "not-a-hex-hash";
    badScheduleHash.genesisHash = genesisHash.hex();
    BOOST_CHECK_EXCEPTION((void)validateOpForkScheduleMetadataRows(badScheduleHash, genesisHash),
        InvalidOpForkSchedule, [](InvalidOpForkSchedule const& e) {
            return messageContains(e, "hex") || messageContains(e, "hash");
        });

    OpForkScheduleMetadataRows badGenesisHash;
    badGenesisHash.schedule = c_isthmusJovianSchedule;
    badGenesisHash.scheduleHash = keccakOpForkScheduleHash(c_isthmusJovianSchedule).hex();
    badGenesisHash.genesisHash = "gg";
    BOOST_CHECK_EXCEPTION((void)validateOpForkScheduleMetadataRows(badGenesisHash, genesisHash),
        InvalidOpForkSchedule, [](InvalidOpForkSchedule const& e) {
            return messageContains(e, "hex") || messageContains(e, "hash");
        });

    OpForkScheduleMetadataRows shortHash;
    shortHash.schedule = c_isthmusJovianSchedule;
    shortHash.scheduleHash = "aa";
    shortHash.genesisHash = std::string(64, '0');
    BOOST_CHECK_EXCEPTION((void)validateOpForkScheduleMetadataRows(shortHash, genesisHash),
        InvalidOpForkSchedule,
        [](InvalidOpForkSchedule const& e) { return messageContains(e, "64 characters"); });
}

BOOST_AUTO_TEST_CASE(persistNormalizesCanonicalText)
{
    const auto genesisHash = HashType{};
    const auto metadata = buildOpForkScheduleMetadata("0:Isthmus", genesisHash);
    BOOST_CHECK_EQUAL(metadata.schedule, "0:isthmus");
    BOOST_CHECK_EQUAL(metadata.scheduleHash, keccakOpForkScheduleHash("0:isthmus"));

    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(
            *ledger, scheduleGenesis("0:Isthmus"), emptyLedgerConfig()));
        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        const auto stored =
            co_await readOpForkScheduleMetadata(*storage, block->blockHeader()->hash());
        BOOST_REQUIRE(stored.has_value());
        BOOST_CHECK_EQUAL(stored->schedule, "0:isthmus");
        co_return;
    }());
}

// Baseline may be any EL fork, so `0:karst` now writes; a gap is still rejected,
// by the general contiguity rule rather than the Karst/Jovian special case.
BOOST_AUTO_TEST_CASE(genesisWriteAcceptsKarstBaselineButRejectsSkippedFork)
{
    task::syncWait([this]() -> task::Task<void> {
        {
            constexpr auto* karstBaseline = "0:karst";
            auto storage = makeL2GenesisTestStorage();
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            BOOST_REQUIRE(co_await ledger::buildGenesisBlock(
                *ledger, scheduleGenesis(karstBaseline), emptyLedgerConfig()));
            auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
            BOOST_REQUIRE(block);
            const auto stored =
                co_await readOpForkScheduleMetadata(*storage, block->blockHeader()->hash());
            BOOST_REQUIRE(stored.has_value());
            BOOST_CHECK_EQUAL(stored->schedule, karstBaseline);
        }

        for (auto const* schedule : {"0:isthmus,1:karst", "0:regolith,1:ecotone"})
        {
            auto storage = makeL2GenesisTestStorage();
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            bool threw = false;
            try
            {
                (void)co_await ledger::buildGenesisBlock(
                    *ledger, scheduleGenesis(schedule), emptyLedgerConfig());
            }
            catch (InvalidOpForkSchedule const& e)
            {
                threw = true;
                BOOST_CHECK(messageContains(e, "forks out of protocol order"));
            }
            BOOST_CHECK(threw);
        }
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(officialHistoryScheduleRoundTrips)
{
    const auto resolved = resolveOpForkScheduleCanonical(
        std::nullopt, std::string{c_officialHistorySchedule}, false, HashType{});
    BOOST_CHECK_EQUAL(resolved, c_officialHistorySchedule);
    BOOST_CHECK_EQUAL(keccakOpForkScheduleHash(resolved).hex(),
        keccakOpForkScheduleHash(c_officialHistorySchedule).hex());
}

// A full nine-fork schedule must survive a ledger reopen. StateStorage writes only
// land in the front layer (asyncSetRow never propagates to prev; prev is a read
// fallback), so flush the front into the backing store first (the in-memory
// stand-in for persisting to the DB). The front storage and its ledger are scoped
// so they go out of scope before the reopened read — a fresh L2GenesisTestStorage
// over the same backing is a process restart for the read path.
BOOST_AUTO_TEST_CASE(nineForkScheduleSurvivesAReopen)
{
    task::syncWait([this]() -> task::Task<void> {
        auto backing = std::make_shared<storage::StateStorage>(nullptr, false);
        backing->setEnableTraverse(true);

        crypto::HashType ledgerGenesisHash;
        std::optional<OpForkScheduleMetadata> before;
        {
            auto storage = std::make_shared<L2GenesisTestStorage>(backing);
            storage->setEnableTraverse(true);
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            BOOST_REQUIRE(co_await ledger::buildGenesisBlock(
                *ledger, scheduleGenesis(c_officialHistorySchedule), emptyLedgerConfig()));
            auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
            BOOST_REQUIRE(block);
            ledgerGenesisHash = block->blockHeader()->hash();

            backing->merge(false, *storage);

            before = co_await readOpForkScheduleMetadata(*storage, ledgerGenesisHash);
            BOOST_REQUIRE(before.has_value());
            BOOST_CHECK_EQUAL(before->schedule, c_officialHistorySchedule);
            BOOST_CHECK_EQUAL(before->genesisHash, ledgerGenesisHash);

            // Hardcoded keccak256(c_officialHistorySchedule) — not derived from the
            // function under test (F12), like c_isthmusJovianScheduleHash above.
            constexpr char const* c_officialHistoryScheduleHash =
                "ccd46a84811fb4c2c668ca1887a5157eae4d51684fb62852c7b6da254f270486";
            BOOST_CHECK_EQUAL(before->scheduleHash.hex(), c_officialHistoryScheduleHash);
            BOOST_CHECK_EQUAL(keccakOpForkScheduleHash(c_officialHistorySchedule).hex(),
                c_officialHistoryScheduleHash);
        }

        // Reopened read path: a new object over the same backing. The front that
        // wrote the rows is out of scope, so the reopened store's only prev is the
        // backing.
        auto reopened = std::make_shared<L2GenesisTestStorage>(backing);
        reopened->setEnableTraverse(true);
        const auto after = co_await readOpForkScheduleMetadata(*reopened, ledgerGenesisHash);
        BOOST_REQUIRE_MESSAGE(
            after.has_value(), "reopened store missing schedule metadata — flush/merge failed?");
        BOOST_CHECK_EQUAL(after->schedule, before->schedule);
        BOOST_CHECK_EQUAL(after->scheduleHash, before->scheduleHash);
        BOOST_CHECK_EQUAL(after->genesisHash, before->genesisHash);
        // And the reopened triple still resolves through the boot-time entry point.
        BOOST_CHECK_EQUAL(
            resolveOpForkScheduleCanonical(after, std::nullopt, false, ledgerGenesisHash),
            std::string(c_officialHistorySchedule));

        // After the reopen the persisted text still parses to the full ladder: 9
        // activations at the 9 boundary timestamps, 9 distinct forks. bcos-evm is
        // not linked into this target, so the framework codec stands in for
        // OpForkSchedule::forkAt (its parse enforces the same protocol order).
        const auto ladder = parseOpForkSchedule(after->schedule);
        BOOST_REQUIRE_EQUAL(ladder.size(), std::size_t{9});
        constexpr std::array<std::uint64_t, 9> c_ladderBoundaries{
            0, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000};
        std::set<std::string> forks;
        for (std::size_t i = 0; i < ladder.size(); ++i)
        {
            BOOST_CHECK_EQUAL(ladder[i].timestamp, c_ladderBoundaries[i]);
            forks.insert(ladder[i].forkName);
        }
        BOOST_CHECK_EQUAL(forks.size(), std::size_t{9});
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(genesisWriteAcceptsKarstAfterJovian)
{
    task::syncWait([this]() -> task::Task<void> {
        constexpr auto* schedule = "0:jovian,1:karst";
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
        BOOST_REQUIRE(co_await ledger::buildGenesisBlock(
            *ledger, scheduleGenesis(schedule), emptyLedgerConfig()));
        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        const auto stored =
            co_await readOpForkScheduleMetadata(*storage, block->blockHeader()->hash());
        BOOST_REQUIRE(stored.has_value());
        BOOST_CHECK_EQUAL(stored->schedule, schedule);
        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
