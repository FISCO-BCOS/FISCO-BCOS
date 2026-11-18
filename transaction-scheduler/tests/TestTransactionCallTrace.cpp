// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
#include "SharedBaselineSchedulerMock.h"
#include "bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-transaction-scheduler/BaselineScheduler-tpp.h"
#include "ethereum-executor/EthereumExecutor.h"
#include <boost/test/unit_test.hpp>
#include <condition_variable>
#include <fakeit.hpp>
#include <future>
#include <mutex>

namespace bcos::test::transaction_trace
{
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::scheduler_v1;
using Mutable = sharedmock::SharedMutableStorage;
using Base = sharedmock::SharedBackendStorage;

// A deterministic storage barrier, below the real scheduler entry point. No sleeps or
// timing-dependent EVM workloads are needed to prove admission and shutdown behavior.
struct Backend : Base
{
    struct Control
    {
        std::mutex mutex;
        std::condition_variable condition;
        bool paused = false;
        size_t waiting = 0;
        std::function<void()> afterBodyRead;
        size_t canonicalReads = 0;
        std::function<void()> beforeFinalCanonicalRead;
    };
    std::shared_ptr<Control> control = std::make_shared<Control>();

    void pauseReceipt(auto const& key)
    {
        if (StateKeyView(key).m_table != ledger::SYS_HASH_2_RECEIPT)
            return;
        std::unique_lock lock(control->mutex);
        if (control->paused)
        {
            ++control->waiting;
            control->condition.notify_all();
            control->condition.wait(lock, [this] { return !control->paused; });
        }
    }
    void resume()
    {
        std::lock_guard lock(control->mutex);
        control->paused = false;
        control->condition.notify_all();
    }
    void bodyRead(auto const& key)
    {
        if (StateKeyView(key).m_table == ledger::SYS_NUMBER_2_TXS && control->afterBodyRead)
        {
            auto callback = std::exchange(control->afterBodyRead, {});
            callback();
        }
    }
    void canonicalRead(auto const& key)
    {
        auto view = StateKeyView(key);
        if (view.m_table != ledger::SYS_NUMBER_2_HASH || view.m_key != "1")
            return;
        if (++control->canonicalReads == 2 && control->beforeFinalCanonicalRead)
        {
            auto callback = std::exchange(control->beforeFinalCanonicalRead, {});
            callback();
        }
    }
    auto readOneRaw(auto const& key, auto&&... args) -> task::Task<DataValue>
    {
        pauseReceipt(key);
        canonicalRead(key);
        auto result = co_await Base::readOneRaw(key, std::forward<decltype(args)>(args)...);
        bodyRead(key);
        co_return result;
    }
    auto readOne(auto key, auto&&... args) -> task::Task<std::optional<storage::Entry>>
    {
        pauseReceipt(key);
        canonicalRead(key);
        auto result = co_await Base::readOne(key, std::forward<decltype(args)>(args)...);
        bodyRead(key);
        co_return result;
    }
};
using Checkpoints = TrivialCheckpointStorage<StateKey, StateValue, Backend>;
using Storage = MultiLayerStorage<Mutable, void, Checkpoints>;

// Configuration is fixed in this fixture; block data and receipt reads remain real.
inline task::Immediate<void> tag_invoke(ledger::tag_t<ledger::getLedgerConfig>, Storage::ViewType&,
    ledger::LedgerConfig& config, protocol::BlockNumber, protocol::BlockFactory&)
{
    config.setExecutorVersion(ledger::ETHEREUM_EXECUTOR_VERSION);
    return {};
}
using Scheduler = BaselineScheduler<Storage, eth::EthereumExecutor, sharedmock::SharedMockScheduler,
    ledger::LedgerInterface>;

struct Fixture
{
    protocol::BlockFactory::Ptr factory = createBlockFactory(createNormalCryptoSuite());
    Backend backend;
    Checkpoints checkpoints{backend};
    Storage storage{checkpoints};
    sharedmock::SharedMockScheduler blockScheduler;
    eth::EthereumExecutor executor{*factory->receiptFactory()};
    fakeit::Mock<ledger::LedgerInterface> ledger;
    fakeit::Mock<txpool::TxPoolInterface> pool;
    protocol::TransactionSubmitResultFactoryImpl submitFactory;
    Scheduler scheduler{storage, blockScheduler, executor, *factory, ledger.get(), pool.get(),
        submitFactory, *factory->cryptoSuite()->hashImpl()};
    EvmcForkTimestamps forks;
    crypto::HashType hash{42U};

    Fixture()
    {
        forks.londonTime = 0;
        forks.parisTime = 0;
        scheduler.setTraceForkSchedule(forks, 1);
    }
    using Result = std::pair<Error::Ptr, std::shared_ptr<bcos::scheduler::CallTrace>>;
    Result trace()
    {
        auto promise = std::make_shared<std::promise<Result>>();
        auto future = promise->get_future();
        scheduler.traceTransaction(hash, {}, [promise](auto error, auto result) {
            promise->set_value({std::move(error), std::move(result)});
        });
        if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
            throw std::runtime_error("trace callback was not delivered");
        return future.get();
    }
    void receipt(uint64_t gasUsed = 0)
    {
        bcostars::protocol::TransactionReceiptImpl receipt;
        receipt.inner().data.blockNumber = 1;
        receipt.inner().data.gasUsed = std::to_string(gasUsed);
        bytes encoded;
        receipt.encode(encoded);
        storage::Entry entry;
        entry.set(std::move(encoded));
        task::syncWait(writeOne(backend,
            StateKey{ledger::SYS_HASH_2_RECEIPT, concepts::bytebuffer::toView(hash)},
            std::move(entry)));
    }
    protocol::Block::Ptr block(int64_t number, h256 parent, h256 transactionsRoot,
        protocol::Transaction::Ptr transaction = {})
    {
        protocol::EthBlockHeaderData data;
        data.number = number;
        data.timestamp = static_cast<uint64_t>(number + 1);
        data.parentInfo = {number - 1, parent};
        data.stateRoot = ledger::mpt::emptyRootHash();
        data.txsRoot = transactionsRoot;
        data.receiptsRoot = ledger::mpt::emptyRootHash();
        data.uncleHash = protocol::c_emptyOmmersHash;
        data.baseFee = transaction ? 0 : 1;
        data.gasLimit = 30000000;
        auto block = factory->createBlock();
        if (transaction)
        {
            block->appendTransaction(transaction);
            data.txsRoot = calculateEthereumTransactionRoot(*block);
            bytes encoded;
            transaction->encode(encoded);
            storage::Entry entry;
            entry.set(std::move(encoded));
            task::syncWait(writeOne(backend,
                StateKey{ledger::SYS_HASH_2_TX, concepts::bytebuffer::toView(transaction->hash())},
                std::move(entry)));
        }
        block->setBlockHeader(makeExecutionBlockHeader(data, *factory, 0, forks));
        protocol::EthBlockHeader::calculateRLPHash(*block->blockHeader());
        auto rows = ledger::buildPrewriteBlockRows(
            block, nullptr, *factory, protocol::ethHeaderHash(data), false);
        for (auto& [key, value] : rows.rows)
            task::syncWait(writeOne(backend, std::move(key), std::move(value)));
        return block;
    }
    void successfulTransfer()
    {
        // Public test key 1, chain 1, nonce/value/gasPrice zero, gas 21000, to 0xcafe.
        // The signature and both transaction commitments take the real decode/storage path.
        auto raw = fromHex(
            "f85f808082520894000000000000000000000000000000000000cafe808026a0"
            "35bf6363c9e9c7c612c0ef45c4bd6c64e3cd7bd99e17efe474f9c0c53a4cca92a0"
            "516687ffea0628883485381bc5a0faa682d382a7b936663ad50d66855ffeac9d");
        auto transaction = rpc::decodeWeb3RawTransaction(
            bytesConstRef(raw.data(), raw.size()), *factory->cryptoSuite()->hashImpl());
        hash = transaction->hash();
        auto parent = block(0, {}, ledger::mpt::emptyRootHash());
        block(1, protocol::canonicalBlockHash(*parent->blockHeader()), {}, std::move(transaction));
        receipt(21000);
    }
};

BOOST_FIXTURE_TEST_SUITE(TransactionCallTraceScheduler, Fixture)

BOOST_AUTO_TEST_CASE(UnknownAndUnavailableStateAreDistinctAndReleaseCapacity)
{
    for (int i = 0; i < 4; ++i)
    {
        auto [error, result] = trace();
        BOOST_REQUIRE(error);
        BOOST_CHECK_EQUAL(error->errorMessage(), "transaction not found");
        BOOST_CHECK(!result);
    }
    receipt();
    for (int i = 0; i < 4; ++i)
    {
        auto [error, result] = trace();
        BOOST_REQUIRE(error);
        BOOST_CHECK_EQUAL(error->errorCode(), bcos::scheduler::SchedulerError::MPTStateUnavailable);
        BOOST_CHECK(!result);
    }
}

BOOST_AUTO_TEST_CASE(PendingReceiptIsNotACommittedTransaction)
{
    bcostars::protocol::TransactionReceiptImpl receipt;
    receipt.inner().data.blockNumber = 1;
    bytes encoded;
    receipt.encode(encoded);
    storage::Entry entry;
    entry.set(std::move(encoded));
    auto pending = storage.fork();
    pending.newMutable();
    task::syncWait(
        writeOne(pending, StateKey{ledger::SYS_HASH_2_RECEIPT, concepts::bytebuffer::toView(hash)},
            std::move(entry)));
    storage.pushView(std::move(pending));

    auto [error, result] = trace();
    BOOST_REQUIRE(error);
    BOOST_CHECK_EQUAL(error->errorMessage(), "transaction not found");
    BOOST_CHECK(!result);
}

BOOST_AUTO_TEST_CASE(BlockBodyMustMatchTheCapturedHeader)
{
    receipt();
    auto parent = block(0, {}, ledger::mpt::emptyRootHash());
    // A header from version A with an empty transaction list from version B.
    block(1, protocol::canonicalBlockHash(*parent->blockHeader()), h256{99U});
    auto [error, result] = trace();
    BOOST_REQUIRE(error);
    BOOST_CHECK(error->errorMessage().find("body does not match") != std::string::npos);
    BOOST_CHECK(!result);
}

BOOST_AUTO_TEST_CASE(CanonicalChangeDuringBodyReadFailsClosed)
{
    receipt();
    auto parent = block(0, {}, ledger::mpt::emptyRootHash());
    block(1, protocol::canonicalBlockHash(*parent->blockHeader()), ledger::mpt::emptyRootHash());
    backend.control->afterBodyRead = [&] {
        storage::Entry changed;
        changed.set(h256{99U}.asBytes());
        task::syncWait(
            writeOne(backend, StateKey{ledger::SYS_NUMBER_2_HASH, "1"}, std::move(changed)));
    };
    auto [error, result] = trace();
    BOOST_REQUIRE(error);
    BOOST_CHECK(error->errorMessage().find("canonical chain changed") != std::string::npos);
    BOOST_CHECK(!result);
}

BOOST_AUTO_TEST_CASE(CanonicalChangeAfterSuccessfulReplayFailsClosed)
{
    successfulTransfer();
    auto [positiveError, positiveTree] = trace();
    BOOST_REQUIRE_MESSAGE(!positiveError,
        (positiveError ? positiveError->errorMessage() : "valid transfer replay failed"));
    BOOST_REQUIRE(positiveTree);
    BOOST_CHECK_EQUAL(positiveTree->type, "CALL");
    BOOST_CHECK_EQUAL(positiveTree->gasUsed, "0x5208");
    BOOST_CHECK_EQUAL(backend.control->canonicalReads, 2);
    fakeit::VerifyNoOtherInvocations(pool);

    backend.control->canonicalReads = 0;
    backend.control->beforeFinalCanonicalRead = [&] {
        storage::Entry changed;
        changed.set(h256{99U}.asBytes());
        task::syncWait(
            writeOne(backend, StateKey{ledger::SYS_NUMBER_2_HASH, "1"}, std::move(changed)));
    };
    auto [error, tree] = trace();
    BOOST_REQUIRE(error);
    BOOST_CHECK_EQUAL(error->errorCode(), bcos::scheduler::SchedulerError::UnknownError);
    BOOST_CHECK_EQUAL(
        error->errorMessage(), "canonical chain changed during transaction tracing; retry");
    BOOST_CHECK(!tree);
    BOOST_CHECK_EQUAL(backend.control->canonicalReads, 2);
    fakeit::VerifyNoOtherInvocations(pool);
}

BOOST_AUTO_TEST_CASE(AdmissionIsBoundedAndStopDrainsActiveRequests)
{
    std::promise<void> stopStarted;
    auto started = stopStarted.get_future();
    std::future<Result> first, second, excess;
    std::future<void> stopping;
    // Destroy the release guard before any future joins, including when thread
    // creation or future.get() throws while the backend is still paused.
    struct ReleaseBarrier
    {
        Backend& backend;
        ~ReleaseBarrier() { backend.resume(); }
    } release{backend};
    backend.control->paused = true;
    first = std::async(std::launch::async, [&] { return trace(); });
    second = std::async(std::launch::async, [&] { return trace(); });
    bool bothWaiting;
    {
        std::unique_lock lock(backend.control->mutex);
        bothWaiting = backend.control->condition.wait_for(
            lock, std::chrono::seconds(5), [&] { return backend.control->waiting == 2; });
    }
    if (!bothWaiting)
    {
        backend.resume();
        (void)first.get();
        (void)second.get();
        BOOST_FAIL("two admitted requests did not reach the storage barrier");
        return;
    }
    excess = std::async(std::launch::async, [&] { return trace(); });
    auto rejected = excess.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    if (!rejected)
        backend.resume();
    auto [error, result] = excess.get();
    BOOST_CHECK(rejected);
    BOOST_CHECK(error != nullptr);
    if (error)
        BOOST_CHECK(error->errorMessage().find("concurrency limit") != std::string::npos);
    BOOST_CHECK(!result);

    stopping = std::async(std::launch::async, [&] {
        stopStarted.set_value();
        scheduler.stop();
    });
    started.wait();
    auto drainedEarly =
        stopping.wait_for(std::chrono::milliseconds(20)) == std::future_status::ready;
    backend.resume();
    auto a = first.get();
    auto b = second.get();
    stopping.get();
    BOOST_CHECK(!drainedEarly);
    BOOST_REQUIRE(a.first);
    BOOST_REQUIRE(b.first);
    BOOST_CHECK_EQUAL(a.first->errorMessage(), "transaction not found");
    BOOST_CHECK_EQUAL(b.first->errorMessage(), "transaction not found");
    auto afterStop = trace();
    BOOST_REQUIRE(afterStop.first);
    BOOST_CHECK(afterStop.first->errorMessage().find("stopping") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test::transaction_trace
