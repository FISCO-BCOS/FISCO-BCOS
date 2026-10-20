#include "../src/Executive.h"
#include "bcos-crypto/ChecksumAddress.h"
#include "bcos-framework/bcos-framework/testutils/faker/FakeBlock.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/executor/NativeExecutionMessage.h"
#include "bcos-scheduler/src/DmcExecutor.h"
#include "bcos-scheduler/src/DmcStepRecorder.h"
#include "bcos-scheduler/src/GraphKeyLocks.h"
#include "bcos-scheduler/src/ShardingDmcExecutor.h"
#include "mock/MockDmcExecutor.h"
#include <bcos-crypto/bcos-crypto/ChecksumAddress.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace std;
using namespace bcos;
using namespace bcos::scheduler;
using namespace bcos::crypto;

namespace bcos::test
{
struct DmcExecutorFixture
{
    DmcExecutorFixture()
    {
        auto hashImpl = std::make_shared<Keccak256>();
        auto signatureImpl = std::make_shared<Secp256k1Crypto>();
        cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
        blockFactory = createBlockFactory(cryptoSuite);
        executor1 = std::make_shared<MockDmcExecutor>("executor1");
        keyLocks = std::make_shared<GraphKeyLocks>();
        dmcRecorder = std::make_shared<DmcStepRecorder>();
    }
    bcos::scheduler::DmcExecutor::Ptr dmcExecutor;
    std::shared_ptr<MockDmcExecutor> executor1;
    bcos::scheduler::GraphKeyLocks::Ptr keyLocks;
    bcos::scheduler::DmcStepRecorder::Ptr dmcRecorder;
    CryptoSuite::Ptr cryptoSuite = nullptr;
    bcos::protocol::BlockFactory::Ptr blockFactory;
};

BOOST_FIXTURE_TEST_SUITE(TestDmcExecutor, DmcExecutorFixture)

struct DmcFlagStruct
{
    using Ptr = std::shared_ptr<DmcFlagStruct>;
    bool schedulerOutFlag = false;
    bool callFlag = false;
    bool DmcFlag = false;
    bool switchFlag = false;
    bool finishFlag = false;
    bool lockedFlag = false;
    std::atomic_size_t round = 0;
    std::atomic_size_t paused = 0;
    std::atomic_size_t finished = 0;
    std::atomic_size_t error = 0;
};

bcos::protocol::ExecutionMessage::UniquePtr createMessage(
    int contextID, int seq, int type, std::string toAddress, bool staticCall)
{
    auto message = std::make_unique<bcos::executor::NativeExecutionMessage>();
    message->setStaticCall(staticCall);
    message->setType(bcos::protocol::ExecutionMessage::Type(type));
    message->setContextID(contextID);
    message->setSeq(seq);
    message->setFrom("0xeeffaabb");
    message->setTo(toAddress);
    return message;
}

BOOST_AUTO_TEST_CASE(handleCreateMessageFollowsTheCreateSaltFormula)
{
    // A producer still speaking the pre-0.24 protocol hands createSalt down with an
    // empty `to` and expects the EIP-1014 address: the DMC lane must derive it through
    // the shared helper (the same formula the host-side create2 hook uses), not
    // silently re-address the create with the legacy FISCO formula.
    auto hashImpl = std::make_shared<Keccak256>();
    auto block = blockFactory->createBlock();
    auto blockHeader = blockFactory->blockHeaderFactory()->createBlockHeader();
    blockHeader->setNumber(1);
    blockHeader->calculateHash(*hashImpl);
    block->setBlockHeader(blockHeader);
    // handleCreateMessage is protected; lift it for the wiring pin.
    class TestableDmcExecutor : public bcos::scheduler::DmcExecutor
    {
    public:
        using bcos::scheduler::DmcExecutor::DmcExecutor;
        using bcos::scheduler::DmcExecutor::handleCreateMessage;
        using bcos::scheduler::DmcExecutor::handleExecutiveMessage;
    };
    auto executor = std::make_shared<TestableDmcExecutor>("DmcExecutorSalt", "0xaabbccdd", block,
        executor1, keyLocks, hashImpl, dmcRecorder, false, ledger::Features{});

    constexpr auto type = static_cast<int>(bcos::protocol::ExecutionMessage::Type::MESSAGE);
    auto salted = createMessage(0, 0, type, "", false);
    salted->setData(bcos::bytes{0x60, 0x00, 0x60, 0x00, 0x53});
    salted->setCreateSalt(bcos::u256(0x1234));
    executor->handleCreateMessage(salted, 7);
    const auto expected =
        bcos::newCreate2EVMAddress(hashImpl, salted->from(), salted->data(), bcos::u256(0x1234));
    BOOST_CHECK(!salted->to().empty());
    BOOST_CHECK_EQUAL(salted->to(), expected);

    // The same message without a salt keeps the legacy FISCO formula — a different
    // address, so the two derivations cannot silently substitute for each other.
    auto legacy = createMessage(0, 0, type, "", false);
    legacy->setData(bcos::bytes{0x60, 0x00, 0x60, 0x00, 0x53});
    executor->handleCreateMessage(legacy, 7);
    BOOST_CHECK(!legacy->to().empty());
    BOOST_CHECK_NE(legacy->to(), expected);
}

BOOST_AUTO_TEST_CASE(stateSwitchTest)
{
    DmcFlagStruct dmcFlagStruct;

    auto hashImpl = std::make_shared<Keccak256>();
    auto block = blockFactory->createBlock();
    auto blockHeader = blockFactory->blockHeaderFactory()->createBlockHeader();
    blockHeader->setNumber(1);
    blockHeader->calculateHash(*hashImpl);
    block->setBlockHeader(blockHeader);
    // block = fakeBlock(cryptoSuite, blockFactory, 1, 1, 1);
    auto dmcExecutor = std::make_shared<DmcExecutor>("DmcExecutor1", "0xaabbccdd", block, executor1,
        keyLocks, hashImpl, dmcRecorder, false, ledger::Features{});

    dmcExecutor->setSchedulerOutHandler(
        [this, &dmcFlagStruct](bcos::scheduler::ExecutiveState::Ptr executiveState) {
            dmcFlagStruct.schedulerOutFlag = true;
            auto to = std::string(executiveState->message->to());
            auto hashImpl = std::make_shared<Keccak256>();
            auto block = blockFactory->createBlock();
            auto blockHeader = blockFactory->blockHeaderFactory()->createBlockHeader();
            blockHeader->setNumber(2);
            blockHeader->calculateHash(*hashImpl);
            block->setBlockHeader(blockHeader);
            auto dmcExecutor2 = std::make_shared<DmcExecutor>("DmcExecutor2", to, block, executor1,
                keyLocks, hashImpl, dmcRecorder, false, ledger::Features{});
            dmcExecutor2->scheduleIn(executiveState);
        });

    dmcExecutor->setOnTxFinishedHandler(
        [&dmcFlagStruct](bcos::protocol::ExecutionMessage::UniquePtr output) {
            auto outputBytes = output->data();
            std::string outputStr((char*)outputBytes.data(), outputBytes.size());
            SCHEDULER_LOG(DEBUG) << LOG_KV("output data is ", outputStr);
            if (outputStr == "Call Finished!")
            {
                dmcFlagStruct.callFlag = true;
                dmcFlagStruct.finishFlag = true;
            }
            else if (outputStr == "DMCExecuteTransaction Finish!")
            {
                dmcFlagStruct.DmcFlag = true;
                dmcFlagStruct.finishFlag = true;
            }
            else if (outputStr == "DMCExecuteTransaction Finish, I am keyLock!")
            {
                dmcFlagStruct.lockedFlag = true;
            }
            else
            {
                dmcFlagStruct.finishFlag = true;
            }
        });

    dmcExecutor->setOnNeedSwitchEventHandler([&dmcFlagStruct]() {
        SCHEDULER_LOG(DEBUG) << "Transaction Perform Error , Need Switch.";
        dmcFlagStruct.switchFlag = true;
    });


    auto executorCallback = [&dmcFlagStruct](
                                bcos::Error::UniquePtr error, DmcExecutor::Status status) {
        if (error || status == DmcExecutor::Status::ERROR)
        {
            ++dmcFlagStruct.error;
            ++dmcFlagStruct.round;
            SCHEDULER_LOG(DEBUG) << LOG_BADGE("DmcExecutor")
                                 << LOG_KV("dmcExecutor go error", dmcFlagStruct.error)
                                 << LOG_KV("total is ", dmcFlagStruct.round);
        }
        if (status == DmcExecutor::Status::PAUSED || status == DmcExecutor::Status::NEED_PREPARE)
        {
            ++dmcFlagStruct.paused;
            ++dmcFlagStruct.round;
            SCHEDULER_LOG(DEBUG) << LOG_BADGE("DmcExecutor")
                                 << LOG_KV("dmcExecutor go paused or need  prepare",
                                        dmcFlagStruct.paused)
                                 << LOG_KV("total is ", dmcFlagStruct.round);
        }
        if (status == DmcExecutor::Status::FINISHED)
        {
            ++dmcFlagStruct.finished;
            ++dmcFlagStruct.round;
            SCHEDULER_LOG(DEBUG) << LOG_BADGE("DmcExecutor")
                                 << LOG_KV("dmcExecutor go Finished", dmcFlagStruct.finished)
                                 << LOG_KV("total is ", dmcFlagStruct.round);
        }
    };


    // TXHASH = 0,  // Received an new transaction from scheduler
    // MESSAGE,     // Send/Receive an external call to/from another contract
    // FINISHED,    // Send a finish to another contract
    // KEY_LOCK,    // Send a wait key lock to scheduler, or release key lock
    // SEND_BACK,   // Send a dag refuse to scheduler
    // REVERT,      // Send/Receive a revert to/from previous external call

    // TXHASH  DMCEXECUTE
    auto message = createMessage(0, 0, 0, "0xaabbccdd", false);
    dmcExecutor->submit(std::move(message), false);
    SCHEDULER_LOG(DEBUG) << "prepare begin";


    // MESSAGE
    auto message1 = createMessage(1, 0, 1, "0xaabbccdd", false);
    dmcExecutor->submit(std::move(message1), false);

    // SEND_BACK DMC_EXECUTE  (TXHASH)
    auto message2 = createMessage(2, 0, 4, "0xaabbccdd", false);
    dmcExecutor->submit(std::move(message2), false);


    // SEND_BACK    (MESSAGE)
    auto message3 = createMessage(3, 0, 4, "", false);
    message3->setTransactionHash(h256(123));
    dmcExecutor->submit(std::move(message3), false);

    // NEED_SCHEDULE_OUT
    auto message4 = createMessage(3, 0, 2, "0xccddeeff", false);
    dmcExecutor->submit(std::move(message4), false);

    auto needScheduler_out = dmcExecutor->prepare();
    BOOST_CHECK(needScheduler_out);
    dmcExecutor->go(executorCallback);
    SCHEDULER_LOG(DEBUG) << LOG_BADGE("DmcExecutor") << LOG_KV("round is ", dmcFlagStruct.round)
                         << LOG_KV("finished is ", dmcFlagStruct.finished)
                         << LOG_KV("paused is ", dmcFlagStruct.paused)
                         << LOG_KV("error is ", dmcFlagStruct.error);
    dmcExecutor->prepare();
    // dmcExecutor->go(executorCallback);
    SCHEDULER_LOG(DEBUG) << LOG_BADGE("DmcExecutor") << LOG_KV("round is ", dmcFlagStruct.round)
                         << LOG_KV("finished is ", dmcFlagStruct.finished)
                         << LOG_KV("paused is ", dmcFlagStruct.paused)
                         << LOG_KV("error is ", dmcFlagStruct.error);

    BOOST_CHECK(dmcFlagStruct.DmcFlag && dmcFlagStruct.finishFlag);
    BOOST_CHECK(dmcFlagStruct.schedulerOutFlag);
    BOOST_CHECK_EQUAL(dmcFlagStruct.paused, 1);
    BOOST_CHECK_EQUAL(dmcFlagStruct.round, 1);
    BOOST_CHECK(!dmcFlagStruct.callFlag);
    dmcFlagStruct.DmcFlag = false;
    dmcFlagStruct.finishFlag = false;
    dmcFlagStruct.schedulerOutFlag = false;


    // call
    dmcExecutor->setIsCall(true);
    auto callMessage = createMessage(4, 0, 1, "0xaabbccdd", true);
    dmcExecutor->submit(std::move(callMessage), false);
    dmcExecutor->prepare();
    dmcExecutor->go(executorCallback);
    dmcExecutor->prepare();

    SCHEDULER_LOG(DEBUG) << LOG_BADGE("DmcExecutor") << LOG_KV("total is ", dmcFlagStruct.round)
                         << LOG_KV("finished is ", dmcFlagStruct.finished)
                         << LOG_KV("paused is ", dmcFlagStruct.paused)
                         << LOG_KV("error is ", dmcFlagStruct.error);
    BOOST_CHECK(dmcFlagStruct.callFlag && dmcFlagStruct.finishFlag);
    BOOST_CHECK(!dmcFlagStruct.DmcFlag && !dmcFlagStruct.schedulerOutFlag);
    BOOST_CHECK(!dmcFlagStruct.lockedFlag && !dmcFlagStruct.switchFlag);
}

BOOST_AUTO_TEST_CASE(keyLocksTest)
{
    DmcFlagStruct dmcFlagStruct;

    auto hashImpl = std::make_shared<Keccak256>();
    auto block = blockFactory->createBlock();
    auto blockHeader = blockFactory->blockHeaderFactory()->createBlockHeader();
    blockHeader->setNumber(1);
    blockHeader->calculateHash(*hashImpl);
    block->setBlockHeader(blockHeader);
    // block = fakeBlock(cryptoSuite, blockFactory, 1, 1, 1);
    auto dmcExecutor = std::make_shared<DmcExecutor>("DmcExecutor1", "0xaabbccdd", block, executor1,
        keyLocks, hashImpl, dmcRecorder, false, ledger::Features{});

    dmcExecutor->setSchedulerOutHandler(
        [this, &dmcFlagStruct](bcos::scheduler::ExecutiveState::Ptr executiveState) {
            dmcFlagStruct.schedulerOutFlag = true;
            auto to = std::string(executiveState->message->to());
            auto hashImpl = std::make_shared<Keccak256>();
            auto block = blockFactory->createBlock();
            auto blockHeader = blockFactory->blockHeaderFactory()->createBlockHeader();
            blockHeader->setNumber(2);
            blockHeader->calculateHash(*blockFactory->cryptoSuite()->hashImpl());
            block->setBlockHeader(blockHeader);
            auto dmcExecutor2 = std::make_shared<DmcExecutor>("DmcExecutor2", to, block, executor1,
                keyLocks, hashImpl, dmcRecorder, false, ledger::Features{});
            dmcExecutor2->scheduleIn(executiveState);
        });

    dmcExecutor->setOnTxFinishedHandler(
        [&dmcFlagStruct](bcos::protocol::ExecutionMessage::UniquePtr output) {
            auto outputBytes = output->data();
            std::string outputStr((char*)outputBytes.data(), outputBytes.size());
            SCHEDULER_LOG(DEBUG) << LOG_KV("output data is ", outputStr);
            if (outputStr == "Call Finished!")
            {
                dmcFlagStruct.callFlag = true;
                dmcFlagStruct.finishFlag = true;
            }
            else if (outputStr == "DMCExecuteTransaction Finish!")
            {
                dmcFlagStruct.DmcFlag = true;
                dmcFlagStruct.finishFlag = true;
            }
            else if (outputStr == "DMCExecuteTransaction Finish, I am keyLock!")
            {
                dmcFlagStruct.lockedFlag = true;
            }
            else
            {
                dmcFlagStruct.finishFlag = true;
            }
        });

    dmcExecutor->setOnNeedSwitchEventHandler([&dmcFlagStruct]() {
        SCHEDULER_LOG(DEBUG) << "Transaction Perform Error , Need Switch.";
        dmcFlagStruct.switchFlag = true;
    });

    auto executorCallback = [&dmcFlagStruct](
                                bcos::Error::UniquePtr error, DmcExecutor::Status status) {
        if (error || status == DmcExecutor::Status::ERROR)
        {
            ++dmcFlagStruct.error;
            ++dmcFlagStruct.round;
            SCHEDULER_LOG(DEBUG) << LOG_BADGE("DmcExecutor")
                                 << LOG_KV("dmcExecutor go error", dmcFlagStruct.error)
                                 << LOG_KV("total is ", dmcFlagStruct.round);
        }
        if (status == DmcExecutor::Status::PAUSED || status == DmcExecutor::Status::NEED_PREPARE)
        {
            ++dmcFlagStruct.paused;
            ++dmcFlagStruct.round;
            SCHEDULER_LOG(DEBUG) << LOG_BADGE("DmcExecutor")
                                 << LOG_KV("dmcExecutor go paused or need  prepare",
                                        dmcFlagStruct.paused)
                                 << LOG_KV("total is ", dmcFlagStruct.round);
        }
        if (status == DmcExecutor::Status::FINISHED)
        {
            ++dmcFlagStruct.finished;
            ++dmcFlagStruct.round;
            SCHEDULER_LOG(DEBUG) << LOG_BADGE("DmcExecutor")
                                 << LOG_KV("dmcExecutor go Finished", dmcFlagStruct.finished)
                                 << LOG_KV("total is ", dmcFlagStruct.round);
        }
    };


    // TXHASH = 0,  // Received an new transaction from scheduler
    // MESSAGE,     // Send/Receive an external call to/from another contract
    // FINISHED,    // Send a finish to another contract
    // KEY_LOCK,    // Send a wait key lock to scheduler, or release key lock
    // SEND_BACK,   // Send a dag refuse to scheduler
    // REVERT,      // Send/Receive a revert to/from previous external call

    // TXHASH  DMCEXECUTE
    auto lockMessage1 = createMessage(0, 0, 3, "0xaabbccdd", false);
    lockMessage1->setKeyLocks({"key1"});
    lockMessage1->setKeyLockAcquired("key2");
    dmcExecutor->submit(std::move(lockMessage1), false);
    auto lockMessage2 = createMessage(1, 0, 3, "0xaabbccdd", false);
    lockMessage2->setKeyLocks({"key2"});
    lockMessage2->setKeyLockAcquired("key3");
    dmcExecutor->submit(std::move(lockMessage2), false);
    auto lockMessage3 = createMessage(2, 0, 3, "0xaabbccdd", false);
    lockMessage3->setKeyLocks({"key3"});
    lockMessage3->setKeyLockAcquired("key1");
    dmcExecutor->submit(std::move(lockMessage3), false);

    dmcExecutor->prepare();
    auto locked = dmcExecutor->unlockPrepare();
    auto found = dmcExecutor->detectLockAndRevert();
    BOOST_CHECK(found && locked);
    SCHEDULER_LOG(DEBUG) << "no need_prepare, found deadlock and revert";
    dmcExecutor->releaseOutdatedLock();
    dmcExecutor->go(executorCallback);
    // dmcExecutor->prepare();
    SCHEDULER_LOG(DEBUG) << LOG_BADGE("DmcExecutor") << LOG_KV("round is ", dmcFlagStruct.round)
                         << LOG_KV("finished is ", dmcFlagStruct.finished)
                         << LOG_KV("paused is ", dmcFlagStruct.paused)
                         << LOG_KV("error is ", dmcFlagStruct.error);
    // BOOST_CHECK(dmcFlagStruct.DmcFlag && dmcFlagStruct.finishFlag);
    BOOST_CHECK_EQUAL(dmcFlagStruct.paused, 1);
    BOOST_CHECK_EQUAL(dmcFlagStruct.round, 1);
    // dmcExecutor->go(executorCallback);
}


// [R5 F1] Every parent-resume arm of the DMC scheduler stamps the resumed message
// with the tx-context's last-consumed seq (ExecutiveState::currentSeq - 1), so the
// coroutine parent's CREATE hook can derive f(currentSeq) like the base's
// scheduler-side arm did. Pinned on the classic FINISHED arm: the child's frame at
// seq 12 finishes, the parent (entered at seq 9) resumes, and the message carries
// seq=9 for routing plus schedulerSeq=6 = currentSeq(7) - 1 for the derivation.
BOOST_AUTO_TEST_CASE(finishedResumeHandsBackSchedulerSeq)
{
    auto block = blockFactory->createBlock();
    auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(3);
    header->calculateHash(*cryptoSuite->hashImpl());
    block->setBlockHeader(header);

    struct ResumeProbeExecutor : public DmcExecutor
    {
        using DmcExecutor::DmcExecutor;
        using DmcExecutor::handleExecutiveMessage;
    };
    ResumeProbeExecutor dmc("DmcExecutor-resume", "0xaabbccdd", block, executor1, keyLocks,
        std::make_shared<Keccak256>(), dmcRecorder, false, ledger::Features{});

    auto message = std::make_unique<executor::NativeExecutionMessage>();
    message->setType(protocol::ExecutionMessage::FINISHED);
    message->setContextID(9);
    message->setSeq(12);
    message->setFrom("0xaabbccdd");
    message->setTo("0xaabbccdd");  // == m_contractAddress: reach the FINISHED arm

    auto state = std::make_shared<scheduler::ExecutiveState>(9, std::move(message), false);
    state->callStack.push(9);   // the parent frame's entry seq
    state->callStack.push(12);  // the finished child
    state->currentSeq = 7;      // six hops consumed so far in this tx context

    auto& stateMessage = state->message;
    auto hint = dmc.handleExecutiveMessage(state);
    BOOST_CHECK_EQUAL(
        static_cast<int>(hint), static_cast<int>(DmcExecutor::MessageHint::NEED_SEND));
    BOOST_CHECK_EQUAL(stateMessage->seq(), 9);           // routing: parent's entry seq
    BOOST_CHECK_EQUAL(stateMessage->schedulerSeq(), 6);  // derivation: currentSeq - 1
}

// [R6 F1] The V2 getCode resume arm stamps the schedulerSeq hand-back: a getCode
// response resumes a live frame, and the frame's next nested-CREATE derives from the
// stamped last-consumed seq — an unstamped reply would poison the sentinel and halt
// the node. Pinned at the arm (the same shape the delegate-no-code arm shares).
BOOST_AUTO_TEST_CASE(getCodeResumeArmStampsSchedulerSeq)
{
    auto block = blockFactory->createBlock();
    auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(3);
    header->calculateHash(*cryptoSuite->hashImpl());
    block->setBlockHeader(header);

    struct GetCodeProbeExecutor : public DmcExecutor
    {
        using DmcExecutor::DmcExecutor;
        using DmcExecutor::handleExecutiveMessageV2;
    };
    GetCodeProbeExecutor dmc("DmcExecutor-getcode", "0xaabbccdd", block, executor1, keyLocks,
        std::make_shared<Keccak256>(), dmcRecorder, false, ledger::Features{});
    dmc.setOnGetCodeHandler([](std::string_view) { return bcos::bytes{0x60, 0x00}; });

    auto message = std::make_unique<executor::NativeExecutionMessage>();
    message->setType(protocol::ExecutionMessage::MESSAGE);
    message->setContextID(9);
    message->setSeq(12);
    message->setFrom("0x1111111111111111111111111111111111111111");
    message->setTo("0xaabbccdd");  // == m_contractAddress: reach the handler
    message->setData(bcos::protocol::GET_CODE_INPUT_BYTES);
    message->setDelegateCallAddress("0x2222222222222222222222222222222222222222");

    auto state = std::make_shared<scheduler::ExecutiveState>(9, std::move(message), false);
    state->currentSeq = 7;

    auto hint = dmc.handleExecutiveMessageV2(state);
    BOOST_CHECK_EQUAL(
        static_cast<int>(hint), static_cast<int>(DmcExecutor::MessageHint::NEED_PREPARE));
    BOOST_CHECK_EQUAL(state->message->schedulerSeq(), 6);  // currentSeq - 1
}

// [R6 F1] The V2 delegate-call-without-code resume arm stamps the schedulerSeq
// hand-back the same way as the getCode arm: the REVERT it hands back resumes a live
// frame, and the frame's next nested-CREATE derives from the stamped last-consumed seq.
BOOST_AUTO_TEST_CASE(delegateNoCodeResumeArmStampsSchedulerSeq)
{
    auto block = blockFactory->createBlock();
    auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(3);
    header->calculateHash(*cryptoSuite->hashImpl());
    block->setBlockHeader(header);

    struct DelegateNoCodeProbeExecutor : public DmcExecutor
    {
        using DmcExecutor::DmcExecutor;
        using DmcExecutor::handleExecutiveMessageV2;
    };
    DelegateNoCodeProbeExecutor dmc("DmcExecutor-delegate", "0xaabbccdd", block, executor1,
        keyLocks, std::make_shared<Keccak256>(), dmcRecorder, false, ledger::Features{});
    dmc.setOnGetCodeHandler([](std::string_view) { return bcos::bytes{}; });  // no code

    auto message = std::make_unique<executor::NativeExecutionMessage>();
    message->setType(protocol::ExecutionMessage::MESSAGE);
    message->setContextID(9);
    message->setSeq(12);
    message->setFrom("0x1111111111111111111111111111111111111111");
    message->setTo("0xaabbccdd");
    message->setDelegateCall(true);
    message->setDelegateCallAddress("0x2222222222222222222222222222222222222222");

    auto state = std::make_shared<scheduler::ExecutiveState>(9, std::move(message), false);
    state->currentSeq = 7;

    auto hint = dmc.handleExecutiveMessageV2(state);
    BOOST_CHECK_EQUAL(
        static_cast<int>(hint), static_cast<int>(DmcExecutor::MessageHint::NEED_PREPARE));
    // The delegate arm consumes newSeq first (currentSeq++), so currentSeq - 1
    // is the just-consumed seq — the value the frame resumes from.
    BOOST_CHECK_EQUAL(state->message->schedulerSeq(), 7);
}

// Empty-to create addresses follow the version-gated lane rule (review F2):
// with bugfix_dmc_create_address_rule AND feature_evm_address on, the DMC lane derives
// the same keccak(rlp(sender, nonce)) address as SerialBlockExecutive; with the flag off
// (or feature_evm_address off) it keeps the historical formula, so replay is unchanged.
BOOST_AUTO_TEST_CASE(emptyToCreateAddressFollowsFeatureGateTest)
{
    auto const from = std::string("0x00000000000000000000000000000000000000aa");
    auto const nonce = std::string("7");

    auto run = [&](ledger::Features features) -> std::string {
        auto block = blockFactory->createBlock();
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(3);
        header->calculateHash(*cryptoSuite->hashImpl());
        block->setBlockHeader(header);

        auto dmc = std::make_shared<DmcExecutor>("DmcExecutor-create", "0xaabbccdd", block,
            executor1, keyLocks, std::make_shared<Keccak256>(), dmcRecorder, false, features);

        std::string capturedTo;
        dmc->setSchedulerOutHandler([&capturedTo](ExecutiveState::Ptr state) {
            capturedTo = std::string(state->message->to());
        });
        dmc->setOnNeedSwitchEventHandler([] {});
        dmc->setOnTxFinishedHandler([](protocol::ExecutionMessage::UniquePtr) {});

        auto message = std::make_unique<executor::NativeExecutionMessage>();
        message->setType(protocol::ExecutionMessage::MESSAGE);
        message->setContextID(9);
        message->setSeq(4);
        message->setDepth(0);
        message->setFrom(from);
        message->setOrigin(from);
        message->setNonce(nonce);
        message->setTo("");
        message->setGasAvailable(1000000);
        dmc->submit(std::move(message), false);

        dmc->prepare();
        dmc->go([](bcos::Error::UniquePtr, DmcExecutor::Status) {});
        return capturedTo;
    };

    ledger::Features gated;
    gated.set(ledger::Features::Flag::bugfix_dmc_create_address_rule);
    gated.set(ledger::Features::Flag::feature_evm_address);
    auto const gatedTo = run(gated);

    // The gate makes the DMC arm match the serial lane's derivation exactly.
    auto const serialTo = bcos::newLegacyEVMAddressString(
        bytesConstRef{boost::algorithm::unhex(from.substr(2))}, nonce);
    BOOST_CHECK_EQUAL(gatedTo, serialTo);

    // Flag off (the historical default): the formula address — different from the serial
    // rule, and non-empty (the derivation still ran).
    auto const legacyTo = run(ledger::Features{});
    BOOST_CHECK(!legacyTo.empty());
    BOOST_CHECK_NE(legacyTo, serialTo);

    // feature_evm_address on but the gate flag off: still the formula address.
    ledger::Features flagOnly;
    flagOnly.set(ledger::Features::Flag::feature_evm_address);
    auto const flagOnlyTo = run(flagOnly);
    BOOST_CHECK_EQUAL(flagOnlyTo, legacyTo);
}

// The sharding lane must derive from the features it was HANDED, not from a silent
// default (R4 blocking finding): ShardingDmcExecutor forwards its ctor's features into
// DmcExecutor::m_features, so a top-level deploy on a feature_sharding chain follows the
// same version-gated rule as every other DMC lane. The probe exposes the protected
// in-place stamping (the same call ShardingDmcExecutor::submit makes) so the applied
// formula is observable without driving the full shard pipeline.
BOOST_AUTO_TEST_CASE(shardingLaneCreateAddressFollowsForwardedFeaturesTest)
{
    struct ProbeShardingExecutor : ShardingDmcExecutor
    {
        using ShardingDmcExecutor::ShardingDmcExecutor;

        std::string stampedCreateTo(protocol::ExecutionMessage::UniquePtr message)
        {
            handleCreateMessage(message, 0);
            return std::string(message->to());
        }
    };

    auto const from = std::string("0x00000000000000000000000000000000000000aa");
    auto const nonce = std::string("7");

    auto run = [&](ledger::Features features) -> std::string {
        auto block = blockFactory->createBlock();
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(3);
        header->calculateHash(*cryptoSuite->hashImpl());
        block->setBlockHeader(header);

        ProbeShardingExecutor executor("ShardingDmcExecutor-create", "0xaabbccdd", block, executor1,
            keyLocks, std::make_shared<Keccak256>(), dmcRecorder,
            /*schedulerTermId=*/0, /*isCall=*/false, features);

        auto message = std::make_unique<executor::NativeExecutionMessage>();
        message->setType(protocol::ExecutionMessage::MESSAGE);
        message->setContextID(9);
        message->setSeq(4);
        message->setDepth(0);
        message->setFrom(from);
        message->setOrigin(from);
        message->setNonce(nonce);
        message->setTo("");
        message->setGasAvailable(1000000);
        return executor.stampedCreateTo(std::move(message));
    };

    ledger::Features gated;
    gated.set(ledger::Features::Flag::bugfix_dmc_create_address_rule);
    gated.set(ledger::Features::Flag::feature_evm_address);
    auto const gatedTo = run(gated);

    // Forwarded features make the sharding lane match the serial derivation exactly;
    // dropping the forward (features stuck at the all-zero default) lands here and
    // fails this equality with the historical formula address.
    auto const serialTo = bcos::newLegacyEVMAddressString(
        bytesConstRef{boost::algorithm::unhex(from.substr(2))}, nonce);
    BOOST_CHECK_EQUAL(gatedTo, serialTo);

    // Nothing scheduled: the historical formula — different from the serial rule and
    // non-empty (the derivation still ran).
    auto const legacyTo = run(ledger::Features{});
    BOOST_CHECK(!legacyTo.empty());
    BOOST_CHECK_NE(legacyTo, serialTo);

    // feature_evm_address on but the gate flag off: still the formula address.
    ledger::Features flagOnly;
    flagOnly.set(ledger::Features::Flag::feature_evm_address);
    auto const flagOnlyTo = run(flagOnly);
    BOOST_CHECK_EQUAL(flagOnlyTo, legacyTo);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
