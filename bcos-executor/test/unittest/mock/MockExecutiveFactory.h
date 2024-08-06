#pragma once
#include "../../../src/Common.h"
#include "../../../src/executive/BlockContext.h"
#include "../../../src/executive/ExecutiveFactory.h"
#include "../../../src/executive/TransactionExecutive.h"
#include "../../../src/vm/gas_meter/GasInjector.h"
#include "MockLedger.h"
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
    MockExecutiveFactory(const BlockContext& blockContext,
        std::shared_ptr<std::map<std::string, std::shared_ptr<PrecompiledContract>>> evmPrecompiled,
        std::shared_ptr<PrecompiledMap> precompiled,
        std::shared_ptr<const std::set<std::string>> staticPrecompiled,
        std::shared_ptr<wasm::GasInjector> gasInjector)
      : ExecutiveFactory(blockContext, std::move(evmPrecompiled), std::move(precompiled), std::move(staticPrecompiled),
            *gasInjector)
    {}
    virtual ~MockExecutiveFactory() {}


    std::shared_ptr<TransactionExecutive> build(const std::string&, int64_t, int64_t, ExecutiveType) override
    {
        auto ledgerCache = std::make_shared<LedgerCache>(std::make_shared<MockLedger>());
        blockContext = std::make_shared<BlockContext>(
            nullptr, ledgerCache, nullptr, 0, h256(), 0, 0, false, false);
        auto executive =
            std::make_shared<MockTransactionExecutive>(*blockContext, "0x00", 0, 0, instruction);
        return executive;
    }
    std::shared_ptr<BlockContext> blockContext;
#ifdef WITH_WASM
    std::shared_ptr<wasm::GasInjector> instruction =
        std::make_shared<wasm::GasInjector>(wasm::GetInstructionTable());
#else
    std::shared_ptr<wasm::GasInjector> instruction;
#endif
};
}  // namespace bcos::test
