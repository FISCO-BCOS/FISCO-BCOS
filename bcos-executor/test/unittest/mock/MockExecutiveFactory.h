#pragma once
#include "../../src/Common.h"
#include "../../src/executive/ExecutiveFactory.h"
#include "../../src/executive/TransactionExecutive.h"
#include "MockTransactionExecutive.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::executor;
namespace bcos::test
{
class MockExecutiveFactory : public bcos::executor::ExecutiveFactory
{
public:
    using Ptr = std::shared_ptr<MockExecutiveFactory>;
    MockExecutiveFactory(std::shared_ptr<BlockContext> blockContext,
        std::shared_ptr<std::map<std::string, std::shared_ptr<PrecompiledContract>>>
            precompiledContract,
        std::shared_ptr<std::map<std::string, std::shared_ptr<precompiled::Precompiled>>>
            constantPrecompiled,
        std::shared_ptr<const std::set<std::string>> builtInPrecompiled,
        std::shared_ptr<wasm::GasInjector> gasInjector)
      : ExecutiveFactory(
            blockContext, precompiledContract, constantPrecompiled, builtInPrecompiled, gasInjector)
    {}
    virtual ~MockExecutiveFactory() {}

    std::shared_ptr<TransactionExecutive> TransactionExecutive::build(
        const std::string&, int64_t, int64_t) override
    {
        auto executive = std::make_shared<TransactionExecutive>(nullptr, "0x00", 0, 0, nullptr);
        return executive;
    }
};
}  // namespace bcos::test
