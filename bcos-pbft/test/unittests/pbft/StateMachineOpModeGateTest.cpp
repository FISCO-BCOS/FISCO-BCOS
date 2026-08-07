/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-pbft/core/Proposal.h>
#include <bcos-pbft/core/StateMachine.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/IOServicePool.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <thread>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::crypto;

namespace bcos::test
{
// Mock scheduler：记录 executeBlock/preExecuteBlock 是否被调用。
class RecordingScheduler : public bcos::scheduler::SchedulerInterface
{
public:
    bool executeBlockCalled = false;
    bool preExecuteBlockCalled = false;

    void executeBlock(bcos::protocol::Block::Ptr, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> callback)
        override
    {
        executeBlockCalled = true;
        callback(BCOS_ERROR_PTR(-1, "recording"), nullptr, false);
    }
    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(bcos::Error::Ptr)> callback) override
    {
        preExecuteBlockCalled = true;
        callback(BCOS_ERROR_PTR(-1, "recording"));
    }
    // 其余 SchedulerInterface 纯虚函数：空实现（参照 FIB112 的 MismatchedNumberScheduler）。
    void commitBlock(bcos::protocol::BlockHeader::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback) override
    {
        callback(BCOS_ERROR_PTR(-1, "recording"), nullptr);
    }
    void status(std::function<void(bcos::Error::Ptr, bcos::protocol::Session::ConstPtr)>) override
    {}
    void call(bcos::protocol::Transaction::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr)>) override
    {}
    void reset(std::function<void(bcos::Error::Ptr)>) override {}
    void getCode(std::string_view, std::function<void(bcos::Error::Ptr, bcos::bytes)>) override {}
    void getABI(std::string_view, std::function<void(bcos::Error::Ptr, std::string)>) override {}
    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, bcos::protocol::BlockNumber) override
    {
        co_return std::nullopt;
    }
};

struct OpModeGateFixture
{
    OpModeGateFixture()
      : hashImpl(std::make_shared<Keccak256>()),
        signatureImpl(std::make_shared<Secp256k1Crypto>()),
        cryptoSuite(std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr)),
        headerFactory(std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite)),
        txFactory(std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite)),
        receiptFactory(
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite)),
        blockFactory(std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, headerFactory, txFactory, receiptFactory)),
        ioPool(std::make_shared<IOServicePool>(1, "opgate")),
        scheduler(std::make_shared<RecordingScheduler>())
    {}
    Hash::Ptr hashImpl;
    std::shared_ptr<Secp256k1Crypto> signatureImpl;
    CryptoSuite::Ptr cryptoSuite;
    bcos::protocol::BlockHeaderFactory::Ptr headerFactory;
    bcos::protocol::TransactionFactory::Ptr txFactory;
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory;
    bcos::protocol::BlockFactory::Ptr blockFactory;
    IOServicePool::Ptr ioPool;
    std::shared_ptr<RecordingScheduler> scheduler;
};

// 构造带真实块数据的 proposal（仿 FIB112:123-134）：nonOpMode 用例需要有效块头才能走到
// scheduler->executeBlock（否则 apply 在 invalid-block 分支提前返回）。
std::shared_ptr<Proposal> makeProposalWithBlockData(OpModeGateFixture& f,
    bcos::protocol::BlockNumber number, ProposalInterface::ConstPtr lastApplied)
{
    auto proposalBlock = f.blockFactory->createBlock();
    auto proposalHeader = f.headerFactory->createBlockHeader(number);
    proposalHeader->setNumber(number);
    proposalHeader->calculateHash(*f.hashImpl);
    proposalBlock->setBlockHeader(proposalHeader);
    bcos::bytes blockData;
    proposalBlock->encode(blockData);

    auto proposal = std::make_shared<Proposal>();
    proposal->setIndex(number);
    proposal->setHash(proposalHeader->hash());
    proposal->setData(blockData);
    return proposal;
}

// opStackMode=true → asyncApply 短路：不调 scheduler->executeBlock，回调 -2。
// 等待方式：IOServicePool 构造时自启 worker 线程跑 ioService->run()，asyncApply 经 strand
// 投递、由 worker 驱动回调；测试用 FIB112:146-151 的 deadline 轮询等待（IOServicePool 无 run()）。
BOOST_AUTO_TEST_CASE(opModeGatesExecuteBlock)
{
    OpModeGateFixture f;
    auto stateMachine = std::make_shared<StateMachine>(f.scheduler, f.blockFactory, f.ioPool, true);
    auto lastApplied = std::make_shared<Proposal>();
    lastApplied->setIndex(0);
    lastApplied->setHash(HashType{});
    auto proposal = makeProposalWithBlockData(f, 1, lastApplied);
    auto executedProposal = std::make_shared<Proposal>();

    std::atomic<bool> callbackCalled{false};
    std::atomic<int64_t> callbackCode{0};
    stateMachine->asyncApply(0, lastApplied, proposal, executedProposal, [&](int64_t errorCode) {
        callbackCalled.store(true);
        callbackCode.store(errorCode);
    });
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!callbackCalled.load() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    BOOST_CHECK_EQUAL(callbackCode.load(), c_opModeExecutionDisabled);
    BOOST_CHECK_EQUAL(f.scheduler->executeBlockCalled, false);
}

// opStackMode=false（默认）→ 正常走 scheduler->executeBlock（回调收到 mock 的 -1）。
BOOST_AUTO_TEST_CASE(nonOpModeExecutesNormally)
{
    OpModeGateFixture f;
    auto stateMachine =
        std::make_shared<StateMachine>(f.scheduler, f.blockFactory, f.ioPool);  // 默认 false
    auto lastApplied = std::make_shared<Proposal>();
    lastApplied->setIndex(0);
    lastApplied->setHash(HashType{});
    auto proposal = makeProposalWithBlockData(f, 1, lastApplied);
    auto executedProposal = std::make_shared<Proposal>();

    std::atomic<bool> callbackCalled{false};
    std::atomic<int64_t> callbackCode{0};
    stateMachine->asyncApply(0, lastApplied, proposal, executedProposal, [&](int64_t errorCode) {
        callbackCalled.store(true);
        callbackCode.store(errorCode);
    });
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!callbackCalled.load() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    BOOST_CHECK_EQUAL(f.scheduler->executeBlockCalled, true);
    BOOST_CHECK_EQUAL(callbackCode.load(), -1);  // RecordingScheduler 的回调错误码
}

// opStackMode=true → asyncPreApply 短路：回调 false，不调 scheduler->preExecuteBlock。
// asyncPreApply 同步直调 preApply（StateMachine.cpp:50），无需等待。
BOOST_AUTO_TEST_CASE(opModeGatesPreExecuteBlock)
{
    OpModeGateFixture f;
    auto stateMachine = std::make_shared<StateMachine>(f.scheduler, f.blockFactory, f.ioPool, true);
    bool preResult = true;
    auto proposal = std::make_shared<Proposal>();
    stateMachine->asyncPreApply(proposal, [&](bool ok) { preResult = ok; });

    BOOST_CHECK_EQUAL(preResult, false);
    BOOST_CHECK_EQUAL(f.scheduler->preExecuteBlockCalled, false);
}
}  // namespace bcos::test
