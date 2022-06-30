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
    MockExecutiveFactory() : ExecutiveFactory(nullptr, nullptr, nullptr, nullptr, nullptr) {}
    virtual ~MockExecutiveFactory() {}

    std::shared_ptr<MockTransactionExecutive> MockExecutiveFactory::build(
        const std::string&, int64_t, int64_t)
    {
        auto executive = std::make_shared<MockTransactionExecutive>(nullptr, "0x00", 0, 0, nullptr);
        executive->setConstantPrecompiled(nullptr);
        executive->setEVMPrecompiled(nullptr);
        executive->setBuiltInPrecompiled(nullptr);
        return executive;
    }
};
}  // namespace bcos::test
