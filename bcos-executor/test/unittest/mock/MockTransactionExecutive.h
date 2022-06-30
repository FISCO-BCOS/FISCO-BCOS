#pragma once
#include "../../../src/executive/TransactionExecutive.h"
#include "../../Common.h"
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
    MockTransactionExecutive(std::weak_ptr<BlockContext>, std::string, int64_t, int64_t,
        std::shared_ptr<wasm::GasInjector>&)
      : TransactionExecutive(nullptr, null, 0, 0, nullptr) override
    {}
    virtual ~MockTransactionExecutive() {}

    CallParameters::UniquePtr start(CallParameters::UniquePtr input) override
    {
        return std::move(input);
    }
    CallParameters::UniquePtr resume() override
    {
        auto callParameters = std::make_unique<CallParameters>();
        callParameters->staticCall = false;
        callParameters->codeAddress = "aabbccddee";
        callParameters->contextID = 1;
        callParameters->seq = 1;
        callParameters->type = 0;
        return std::move(callParameters);
    }

    void setExchangeMessage(CallParameters::UniquePtr) override { return nullptr; }
    void appendResumeKeyLocks(std::vector<std::string>) override { return nullptr; }
}
}  // namespace bcos::test