#pragma once
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
    MockTransactionExecutive(std::weak_ptr<BlockContext>, std::string, int64_t, int64_t,
        std::shared_ptr<wasm::GasInjector>&)
      : TransactionExecutive(nullptr, "aabbcc", 0, 0, nullptr)
    {}
    virtual ~MockTransactionExecutive() {}

    CallParameters::UniquePtr start(CallParameters::UniquePtr input) { return std::move(input); }
    CallParameters::UniquePtr resume()
    {
        auto callParameters = std::make_unique<CallParameters>(CallParameters::Type::MESSAGE);
        callParameters->staticCall = false;
        callParameters->codeAddress = "aabbccddee";
        callParameters->contextID = 1;
        callParameters->seq = 1;
        return std::move(callParameters);
    }

    void setExchangeMessage(CallParameters::UniquePtr callParameters)
    {
        m_exchangeMessage = std::move(callParameters);
    }
    void appendResumeKeyLocks(std::vector<std::string> keyLocks)
    {
        std::copy(keyLocks.begin(), keyLocks.end(), std::back_inserter({"key100"}));
    }

private:
    CallParameters::UniquePtr m_exchangeMessage = nullptr;
};
}  // namespace bcos::test