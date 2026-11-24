// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

#include "libinitializer/OpAdmissionState.h"
#include "engine/test/unittests/engine/OpReorgFixture.h"
#include <bcos-framework/storage/Serialize.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
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
    task::syncWait(fixture->multiLayerStorage.mergeView(std::move(view)));

    auto const chain = op_engine_reorg::setupB1B2a(*fixture);
    auto read = initializer::makeOpAdmissionChainState(
        std::weak_ptr{fixture->opDelegate}, fixture, fixture->blockFactory, fixture->forkSchedule);
    bcostars::protocol::TransactionImpl tx;
    tx.forceSender(bytes(
        std::begin(op_engine_reorg::kAccount.bytes), std::end(op_engine_reorg::kAccount.bytes)));
    auto probe = [&] {
        return task::syncWait(read(tx, txvalidator::Check::Balance | txvalidator::Check::ChainId));
    };

    auto const first = probe();
    BOOST_REQUIRE(first.sender);
    BOOST_REQUIRE(first.config->chainId());
    BOOST_CHECK_EQUAL(
        fromBigEndian<u256>(first.config->chainId()->bytes), op_engine_reorg::kChainId);
    BOOST_CHECK(first.revision == EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(first.config->hash().hex(), chain.h2a.hex());
    BOOST_CHECK(first.config->baseFeePerGas() == chain.b2a.executionPayload.baseFeePerGas);
    BOOST_CHECK_EQUAL(first.sender->balance, 20);

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
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
