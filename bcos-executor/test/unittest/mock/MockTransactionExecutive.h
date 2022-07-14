#pragma once
#include "../../../src/executive/BlockContext.h"
#include "../../src/Common.h"
#include "../../src/executive/TransactionExecutive.h"
#include "bcos-executor/src/executive/BlockContext.h"
#include "bcos-executor/src/executor/TransactionExecutor.h"
#include "bcos-framework/interfaces/protocol/BlockHeader.h"
#include <boost/test/unit_test.hpp>


using namespace bcos;
using namespace bcos::executor;
namespace bcos::test
{
class MockTransactionExecutive : public bcos::executor::TransactionExecutive
{
public:
    using Ptr = std::shared_ptr<MockTransactionExecutive>;
    MockTransactionExecutive(std::weak_ptr<bcos::executor::BlockContext> blockContext,
        std::string contractAddress, int64_t contextID, int64_t seq,
        std::shared_ptr<wasm::GasInjector>& gasInjector)
      : TransactionExecutive(std::move(blockContext), contractAddress, contextID, seq, gasInjector)
    {}

    virtual ~MockTransactionExecutive() {}

    CallParameters::UniquePtr start(CallParameters::UniquePtr input) override
    {
        return std::move(input);
    }
    CallParameters::UniquePtr resume() override
    {
        auto callParameters = std::make_unique<CallParameters>(CallParameters::Type::MESSAGE);
        callParameters->staticCall = false;
        callParameters->codeAddress = "aabbccddee";
        callParameters->contextID = 1;
        callParameters->seq = 1;
        callParameters->keyLocks = m_exchangeMessage->keyLocks;

        return std::move(callParameters);
    }

    void setExchangeMessage(CallParameters::UniquePtr callParameters) override
    {
        m_exchangeMessage = std::move(callParameters);
    }
    void appendResumeKeyLocks(std::vector<std::string> keyLocks) override
    {
        std::copy(
            keyLocks.begin(), keyLocks.end(), std::back_inserter(m_exchangeMessage->keyLocks));
    }

private:
    CallParameters::UniquePtr m_exchangeMessage = nullptr;
};
}  // namespace bcos::test
