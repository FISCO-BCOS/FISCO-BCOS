// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

#include "libinitializer/OpAdmissionState.h"
#include "bcos-tx-validator/test/unittests/AdmissionHarness.h"
#include "engine/test/unittests/engine/OpReorgFixture.h"
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/storage/Serialize.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(OpAdmissionStateTest)

BOOST_AUTO_TEST_CASE(admissionReadsSelectedWindowHeadAndRewinds)
{
    auto fixture = std::make_shared<op_engine_reorg::ReorgFixture>();
    auto view = fixture->multiLayerStorage.forkCommitted();
    view.newMutable();
    auto putConfig = [&](std::string key, std::string value) {
        task::syncWait(
            storage2::writeOne(view, executor_v1::StateKey{ledger::SYS_CONFIG, std::move(key)},
                storage::Entry{storage::serialize::encode(ledger::SystemConfigEntry{value, 0})}));
    };
    putConfig("executor_version", std::to_string(ledger::OPSTACK_EXECUTOR_VERSION));
    putConfig("web3_chain_id", std::to_string(op_engine_reorg::kChainId));
    auto setOperatorFeeConstant = [](auto& layer, uint8_t constant) {
        auto l1Block = executor_v1::eth::ethViewAccount(layer, opeth::OP_L1_BLOCK);
        evmc_bytes32 slot{};
        slot.bytes[31] = 8;
        evmc_bytes32 value{};
        value.bytes[31] = constant;
        task::syncWait(l1Block.setStorage(slot, value));
    };
    auto l1Block = executor_v1::eth::ethViewAccount(view, opeth::OP_L1_BLOCK);
    task::syncWait(l1Block.create());
    task::syncWait(l1Block.setNonce("1"));
    setOperatorFeeConstant(view, 5);
    task::syncWait(fixture->multiLayerStorage.mergeView(std::move(view)));
    fixture->seedGenesis();

    auto const chain = op_engine_reorg::setupB1B2a(*fixture);
    auto makeProvider = [&] {
        return initializer::makeOpAdmissionChainState(std::weak_ptr{fixture->opDelegate}, fixture,
            fixture->blockFactory, fixture->forkSchedule);
    };
    auto readAdmissionState = makeProvider();
    auto tx = admitTx();
    tx->forceSender(bytes(
        std::begin(op_engine_reorg::kAccount.bytes), std::end(op_engine_reorg::kAccount.bytes)));
    constexpr auto checks =
        txvalidator::Check::Balance | txvalidator::Check::ChainId | txvalidator::Check::L1Cost;
    auto probe = [&] { return task::syncWait(readAdmissionState(*tx, checks)); };

    auto const first = probe();
    BOOST_REQUIRE(first.sender);
    BOOST_REQUIRE(first.config->chainId());
    BOOST_CHECK_EQUAL(
        fromBigEndian<u256>(first.config->chainId()->bytes), op_engine_reorg::kChainId);
    BOOST_CHECK(first.revision == EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(first.config->hash().hex(), chain.h2a.hex());
    BOOST_CHECK(first.config->baseFeePerGas() == chain.b2a.executionPayload.baseFeePerGas);
    BOOST_CHECK_EQUAL(first.sender->balance, 20);
    BOOST_REQUIRE(first.rollupCost);
    BOOST_CHECK_EQUAL(*first.rollupCost, 5);

    auto const sibling = op_engine_reorg::deriveB2b(*fixture, chain);
    auto const second = probe();
    BOOST_REQUIRE(second.sender);
    BOOST_CHECK_EQUAL(second.config->hash().hex(), sibling.h2b.hex());
    BOOST_CHECK_EQUAL(second.sender->balance, 15);

    BOOST_REQUIRE(fixture->fcu(fixture->fc(chain.h1, chain.h1, chain.h1)).payloadStatus.status ==
                  engine::PayloadValidationStatus::Valid);
    auto const rewind = probe();
    BOOST_REQUIRE(rewind.sender);
    BOOST_REQUIRE(rewind.sender->nonce);
    BOOST_REQUIRE(first.sender->nonce);
    BOOST_CHECK_EQUAL(rewind.config->hash().hex(), chain.h1.hex());
    BOOST_CHECK(rewind.config->baseFeePerGas() == chain.b1.executionPayload.baseFeePerGas);
    BOOST_CHECK(chain.b1.executionPayload.baseFeePerGas > chain.b2a.executionPayload.baseFeePerGas);
    BOOST_CHECK_EQUAL(rewind.sender->balance, 10);
    BOOST_CHECK_EQUAL(*rewind.sender->nonce + 1, *first.sender->nonce);
    BOOST_CHECK_EQUAL(fixture->backendNumber(), 1);

    auto pending = fixture->multiLayerStorage.fork();
    pending.newMutable();
    ledger::account::EVMAccount pendingAccount(pending, ledger::account::FromTableName{},
        ledger::account::ethLaneAccountTableName(op_engine_reorg::kAccount));
    task::syncWait(pendingAccount.setBalance(99));
    task::syncWait(pendingAccount.setNonce("99"));
    setOperatorFeeConstant(pending, 99);
    fixture->multiLayerStorage.pushView(std::move(pending));
    // A fresh provider has no cached fee, so it must read from the selected committed view.
    auto pendingProbe = makeProvider();
    auto const ignoresPending = task::syncWait(pendingProbe(*tx, checks));
    BOOST_REQUIRE(ignoresPending.sender);
    BOOST_CHECK_EQUAL(ignoresPending.sender->balance, rewind.sender->balance);
    BOOST_CHECK(ignoresPending.sender->nonce == rewind.sender->nonce);
    BOOST_REQUIRE(ignoresPending.rollupCost);
    BOOST_CHECK_EQUAL(*ignoresPending.rollupCost, 5);
    fixture->multiLayerStorage.popFrontStorage();

    // Model a restored header whose identity disagrees with the height index.
    auto committed = fixture->multiLayerStorage.forkCommitted();
    auto block =
        task::syncWait(ledger::getBlockData(committed, 1, ledger::HEADER, *fixture->blockFactory));
    BOOST_REQUIRE(block && block->blockHeader());
    auto header = block->blockHeader();
    header->setTimestamp(header->timestamp() + 1000);
    BOOST_REQUIRE(protocol::canonicalBlockHash(*header) != chain.h1);
    bytes encoded;
    header->encode(encoded);
    committed.newMutable();
    task::syncWait(
        storage2::writeOne(committed, executor_v1::StateKey{ledger::SYS_NUMBER_2_BLOCK_HEADER, "1"},
            storage::Entry{std::move(encoded)}));
    task::syncWait(fixture->multiLayerStorage.mergeView(std::move(committed)));

    // A warmed provider still uses the already validated header. A new provider rejects it.
    auto const cached = probe();
    BOOST_REQUIRE(cached.sender);
    BOOST_CHECK(cached.config == rewind.config);
    BOOST_CHECK(cached.revision == rewind.revision);
    BOOST_CHECK_EQUAL(cached.sender->balance, rewind.sender->balance);
    auto cold = makeProvider();
    BOOST_CHECK_EXCEPTION(
        task::syncWait(cold(*tx, checks)), std::runtime_error, [](std::runtime_error const& error) {
            return std::string_view(error.what()) ==
                   "OP admission: canonical head header does not match its hash";
        });
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
