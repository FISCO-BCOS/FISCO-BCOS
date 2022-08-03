#pragma once
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/executor/ParallelTransactionExecutorInterface.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/BlockHeaderFactory.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/protocol/TransactionMetaData.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-scheduler/src/BlockExecutive.h"
#include "bcos-scheduler/test/mock/MockLedger3.h"
#include "bcos-tars-protocol/bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "mock/MockLedger.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <bcos-utilities/Error.h>
#include <tbb/concurrent_unordered_map.h>
#include <boost/test/unit_test.hpp>


using namespace bcos;
using namespace bcostars::protocol;
using namespace bcos::crypto;
using namespace bcos::scheduler;

namespace bcos::test
{
class MockBlockExecutive : public bcos::scheduler::BlockExecutive
{
public:
    using Ptr = std::shared_ptr<MockBlockExecutive>;
    MockBlockExecutive(bcos::protocol::Block::Ptr block, SchedulerImpl* scheduler,
        size_t startContextID,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::txpool::TxPoolInterface::Ptr _txPool)
      : BlockExecutive(block, scheduler, startContextID, transactionSubmitResultFactory, staticCall,
            _blockFactory, _txPool)
    {}

    MockBlockExecutive(bcos::protocol::Block::Ptr block, SchedulerImpl* scheduler,
        size_t startContextID,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::txpool::TxPoolInterface::Ptr _txPool, uint64_t _gasLimit, bool _syncBlock)
      : BlockExecutive(block, scheduler, startContextID, transactionSubmitResultFactory, staticCall,
            _blockFactory, _txPool)
    {
        m_blockNumber = block->blockHeader()->number();
        m_syncBlock = _syncBlock;
        m_gasLimit = _gasLimit;
    }
    virtual ~MockBlockExecutive(){};

    void prepare() override
    {
        // do nothing
    }
    void asyncCall(
        std::function<void(Error::UniquePtr&&, protocol::TransactionReceipt::Ptr&&)> callback)
        override
    {
        asyncExecute([executive = shared_from_this(), callback](
                         Error::UniquePtr&& _error, protocol::BlockHeader::Ptr, bool) {
            if (!executive)
            {
                callback(BCOS_ERROR_UNIQUE_PTR(
                             SchedulerError::UnknownError, "get block executive failed"),
                    nullptr);
                return;
            }
            auto receipt = std::const_pointer_cast<protocol::TransactionReceipt>(
                executive->block()->receipt(0));
            callback(std::move(_error), std::move(receipt));
        });
    }
    void asyncNotify(
        std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
            std::function<void(Error::Ptr)>)>& notifier,
        std::function<void(Error::Ptr)> _callback)
    {
        // do nothing
        _callback(nullptr);
    }
    void asyncExecute(
        std::function<void(Error::UniquePtr, protocol::BlockHeader::Ptr, bool)> callback) override
    {
        if (m_result)
        {
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::InvalidStatus, "Invalid status"),
                nullptr, m_isSysBlock);
            SCHEDULER_LOG(DEBUG) << "i am asyncExecute error";
            return;
        }
        else
        {
            SCHEDULER_LOG(DEBUG) << "MockBlockExecutive asyncExecute begin";
            bcos::crypto::Hash::Ptr hashImpl = std::make_shared<Keccak256>();
            bcos::crypto::SignatureCrypto::Ptr signature = std::make_shared<Secp256k1Crypto>();
            bcos::crypto::CryptoSuite::Ptr suite =
                std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signature, nullptr);
            m_blockHeaderFactory =
                std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(suite);
            auto blockHeader = m_blockHeaderFactory->createBlockHeader();
            SCHEDULER_LOG(DEBUG) << LOG_KV("BlockNumber", m_blockNumber)
                                 << LOG_KV("blockHeader", blockHeader);
            blockHeader->setNumber(m_blockNumber);
            m_result = blockHeader;
            callback(nullptr, blockHeader, false);
            return;
        }
    }
    void asyncCommit(std::function<void(Error::UniquePtr)> callback) override
    {
        if (m_blockNumber <= 5)
        {
            // m_ledger->commitSuccess(false);
            callback(BCOS_ERROR_UNIQUE_PTR(SchedulerError::CommitError, "asyncCommit errors!"));
        }
        else
        {
            // m_ledger->commitSuccess(true);
            callback(nullptr);
        }
    }
    bcos::protocol::BlockNumber number() { return m_block->blockHeaderConst()->number(); }
    bcos::protocol::Block::Ptr block() { return m_block; }
    bool sysBlock() const { return m_isSysBlock; }

    void triggerSwitch()
    {
        if (f_onNeedSwitchEvent)
        {
            f_onNeedSwitchEvent();
        }
    }
    void setOnNeedSwitchEventHandler(std::function<void()> onNeedSwitchEvent)
    {
        f_onNeedSwitchEvent = std::move(onNeedSwitchEvent);
    }

private:
    bcos::protocol::Block::Ptr m_block;
    bcostars::protocol::BlockHeaderFactoryImpl::Ptr m_blockHeaderFactory;
    bcos::protocol::BlockNumber m_blockNumber;
    bcos::test::MockLedger3::Ptr m_ledger;
    size_t m_gasLimit = TRANSACTION_GAS;
    bool m_isSysBlock;
    bool m_running = false;
    bool m_syncBlock = false;

public:
    struct CommitStatus
    {
        std::atomic_size_t total;
        std::atomic_size_t success = 0;
        std::atomic_size_t failed = 0;
        std::function<void(const CommitStatus&)> checkAndCommit;
        mutable SharedMutex x_lock;
    };
};
}  // namespace bcos::test