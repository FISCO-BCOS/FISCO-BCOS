#pragma once
#include "../../../src/Common.h"
#include "../../../src/executive/ExecutiveFactory.h"
#include "../../../src/executive/TransactionExecutive.h"
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
    MockExecutiveFactory(std::shared_ptr<BlockContext>,
        std::shared_ptr<std::map<std::string, std::shared_ptr<PrecompiledContract>>>,
        std::shared_ptr<std::map<std::string, std::shared_ptr<precompiled::Precompiled>>>,
        std::shared_ptr<const std::set<std::string>>, std::shared_ptr<wasm::GasInjector>)
      : ExecutiveFactory(nullptr, nullptr, nullptr, nullptr, nullptr)
    {}
    virtual ~MockExecutiveFactory() {}

    std::shared_ptr<MockTransactionExecutive> MockExecutiveFactory::build(
        const std::string&, int64_t, int64_t) override
    {
        auto executive = std::make_shared<MockTransactionExecutive>(nullptr, null, 0, 0, nullptr);
        return executive;
    }
};
}  // namespace bcos::test
