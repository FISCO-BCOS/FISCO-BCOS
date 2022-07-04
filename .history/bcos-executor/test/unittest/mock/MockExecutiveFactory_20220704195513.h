#pragma once
#include "../../../src/Common.h"
#include "../../../src/vm/gas_meter/GasInjector.h"
#include "../../../src/executive/ExecutiveFactory.h"
#include "../../../src/executive/TransactionExecutive.h"
#include "../../../src/executive/BlockContext.h"
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
           std::move(blockContext), precompiledContract, constantPrecompiled, builtInPrecompiled, gasInjector)
    {}
    virtual ~MockExecutiveFactory() {}


    std::shared_ptr<TransactionExecutive> build(const std::string&, int64_t, int64_t) override
    {
	std::shared_ptr<BlockContext> blockContext = std::make_shared<BlockContext>(nullptr, nullptr, 0, h256(), 0, 0, FiscoBcosScheduleV4, false, false);
        auto executive = std::make_shared<MockTransactionExecutive>(blockContext, "0x00", 0, 0, instruction);
        return executive;
    }
    std::shared_ptr<wasm::GasInjector> instruction = std::make_shared<wasm::GasInjector>(wasm::GetInstructionTable());
};
}  // namespace bcos::test
