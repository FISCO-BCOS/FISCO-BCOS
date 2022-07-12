#include "../src/Executive.h"
#include "bcos-executor/src/CallParameters.h"
#include "bcos-executor/src/executive/BlockContext.h"
#include "bcos-executor/src/executive/ExecutiveState.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/executor/NativeExecutionMessage.h"
#include "bcos-protocol/protobuf/PBBlock.h"
#include "bcos-protocol/protobuf/PBBlockFactory.h"
#include "bcos-protocol/testutils/protocol/FakeBlock.h"
#include "bcos-protocol/testutils/protocol/FakeBlockHeader.h"
#include "bcos-scheduler/src/DmcExecutor.h"
#include "bcos-scheduler/src/DmcStepRecorder.h"
#include "bcos-scheduler/src/GraphKeyLocks.h"
#include "mock/MockDmcExecutor.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/interfaces/protocol/Block.h>
#include <bcos-framework/interfaces/protocol/TransactionFactory.h>
#include <bcos-framework/interfaces/protocol/TransactionReceiptFactory.h>
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
    return std::move(message);
}

BOOST_AUTO_TEST_CASE(stateSwitchTest)
{
    DmcFlagStruct dmcFlagStruct;

    auto hashImpl = std::make_shared<Keccak256>();
    auto block = blockFactory->createBlock();
    auto blockHeader = blockFactory->blockHeaderFactory()->createBlockHeader();
    blockHeader->setNumber(1);
    block->setBlockHeader(blockHeader);
    // block = fakeBlock(cryptoSuite, blockFactory, 1, 1, 1);
    auto dmcExecutor = std::make_shared<DmcExecutor>(
        "DmcExecutor1", "0xaabbccdd", block, executor1, keyLocks, hashImpl, dmcRecorder);

    dmcExecutor->setSchedulerOutHandler(
        [this, &dmcFlagStruct](bcos::scheduler::ExecutiveState::Ptr executiveState) {
            dmcFlagStruct.schedulerOutFlag = true;
            auto to = std::string(executiveState->message->to());
            auto hashImpl = std::make_shared<Keccak256>();
            auto block = blockFactory->createBlock();
            auto blockHeader = blockFactory->blockHeaderFactory()->createBlockHeader();
            blockHeader->setNumber(2);
            block->setBlockHeader(blockHeader);
            auto dmcExecutor2 = std::make_shared<DmcExecutor>(
                "DmcExecutor2", to, block, executor1, keyLocks, hashImpl, dmcRecorder);
            dmcExecutor2->scheduleIn(executiveState);
        });

    dmcExecutor->setOnTxFinishedHandler(
        [this, &dmcFlagStruct](bcos::protocol::ExecutionMessage::UniquePtr output) {
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

    dmcExecutor->setOnNeedSwitchEventHandler([this, &dmcFlagStruct]() {
        SCHEDULER_LOG(DEBUG) << "Transaction Perform Error , Need Switch.";
        dmcFlagStruct.switchFlag = true;
    });


    auto executorCallback = [this, &dmcFlagStruct](
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
    bcos::u256 salt(787667543453);
    message3->setCreateSalt(salt);
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
    block->setBlockHeader(blockHeader);
    // block = fakeBlock(cryptoSuite, blockFactory, 1, 1, 1);
    auto dmcExecutor = std::make_shared<DmcExecutor>(
        "DmcExecutor1", "0xaabbccdd", block, executor1, keyLocks, hashImpl, dmcRecorder);

    dmcExecutor->setSchedulerOutHandler(
        [this, &dmcFlagStruct](bcos::scheduler::ExecutiveState::Ptr executiveState) {
            dmcFlagStruct.schedulerOutFlag = true;
            auto to = std::string(executiveState->message->to());
            auto hashImpl = std::make_shared<Keccak256>();
            auto block = blockFactory->createBlock();
            auto blockHeader = blockFactory->blockHeaderFactory()->createBlockHeader();
            blockHeader->setNumber(2);
            block->setBlockHeader(blockHeader);
            auto dmcExecutor2 = std::make_shared<DmcExecutor>(
                "DmcExecutor2", to, block, executor1, keyLocks, hashImpl, dmcRecorder);
            dmcExecutor2->scheduleIn(executiveState);
        });

    dmcExecutor->setOnTxFinishedHandler(
        [this, &dmcFlagStruct](bcos::protocol::ExecutionMessage::UniquePtr output) {
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

    dmcExecutor->setOnNeedSwitchEventHandler([this, &dmcFlagStruct]() {
        SCHEDULER_LOG(DEBUG) << "Transaction Perform Error , Need Switch.";
        dmcFlagStruct.switchFlag = true;
    });

    auto executorCallback = [this, &dmcFlagStruct](
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
    dmcExecutor->unlockPrepare();
    dmcExecutor->detectLockAndRevert();
    dmcExecutor->releaseOutdatedLock();
    dmcExecutor->go(executorCallback);

    SCHEDULER_LOG(DEBUG) << LOG_BADGE("DmcExecutor") << LOG_KV("round is ", dmcFlagStruct.round)
                         << LOG_KV("finished is ", dmcFlagStruct.finished)
                         << LOG_KV("paused is ", dmcFlagStruct.paused)
                         << LOG_KV("error is ", dmcFlagStruct.error);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
