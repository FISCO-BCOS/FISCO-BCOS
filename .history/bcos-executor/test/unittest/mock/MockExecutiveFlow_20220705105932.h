#pragma once

#include "../../../src/CallParameters.h"
#include <bcos-utilities/ThreadPool.h>
using namespace bcos;
using namespace std;
using namespace bcos::executor;

namespace bcos::test
{
class MockExecutiveFlow : public bcos::executor::ExecutiveFlowInterface
{
public:
    using Ptr = std::shared_ptr<MockExecutiveFlow>;
    MockExecutiveFlow(std::string& name) : m_name(name){};


    void submit(CallParameters::UniquePtr txInput) override {}
    void submit(std::shared_ptr<std::vector<CallParameters::UniquePtr>> txInputs) override {}
    void asyncRun(
        // onTxReturn(output)
        std::function<void(CallParameters::UniquePtr)> onTxReturn,

        // onFinished(success, errorMessage)
        std::function<void(bcos::Error::UniquePtr)> onFinished) override;
    string name() { return m_name; }

private:
    std::string m_name;
};

}  // namespace bcos::test