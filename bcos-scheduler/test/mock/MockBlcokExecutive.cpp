#pragma once
#include "bcos-scheduler/src/BlockExecutive.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::scheduler;

namespace bcos::test
{
class MockBlockExecutive : public bcos::scheduler::BlockExecutive
{
public:
    using Ptr = std::shared_ptr<MockBlockExecutive>;
    BlockExecutive(bcos::protocol::Block::Ptr block, SchedulerImpl* scheduler,
        size_t startContextID,
        bcos::protocol::TransactionSubmitResultFactory::Ptr transactionSubmitResultFactory,
        bool staticCall, bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::txpool::TxPoolInterface::Ptr _txPool, uint64_t _gasLimit, bool _syncBlock)
      : BlockExecutive(block, scheduler, startContextID, transactionSubmitResultFactory, staticCall,
            _blockFactory, _txPool)
    {
        m_syncBlock = _syncBlock;
        m_gasLimit = _gasLimit;
    }
    virtual ~BlockExecutive{};
};
}  // namespace bcos::test