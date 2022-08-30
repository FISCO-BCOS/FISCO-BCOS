#pragma once
#include "../../../src/executive/BlockContext.h"
#include "../../src/Common.h"
#include "../../src/executive/TransactionExecutive.h"
#include "bcos-executor/src/CallParameters.h"
#include "bcos-executor/src/executive/BlockContext.h"
#include "bcos-executor/src/executor/TransactionExecutor.h"
#include <boost/test/unit_test.hpp>
#include <string>

using namespace std;
using namespace bcos;
using namespace bcos::executor;
namespace bcos::test
{
class MockTransactionExecutive : public bcos::executor::CoroutineTransactionExecutive
{
public:
    using Ptr = std::shared_ptr<MockTransactionExecutive>;
    MockTransactionExecutive(std::weak_ptr<bcos::executor::BlockContext> blockContext,
        std::string contractAddress, int64_t contextID, int64_t seq,
        std::shared_ptr<wasm::GasInjector>& gasInjector)
      : CoroutineTransactionExecutive(
            std::move(blockContext), contractAddress, contextID, seq, gasInjector)
    {}

    virtual ~MockTransactionExecutive() {}

    CallParameters::UniquePtr start(CallParameters::UniquePtr input) override
    {
        if (input->type == CallParameters::MESSAGE || input->type == CallParameters::KEY_LOCK)
        {
            EXECUTOR_LOG(DEBUG) << LOG_KV("contextID", input->contextID)
                                << LOG_KV("keylocks_size", input->keyLocks.size());
            if (input->contextID == 2)
            {
                input->keyLocks = {"key0"};
                input->type = CallParameters::MESSAGE;
                return input;
            }
            else if (input->contextID == 1 && input->keyLocks.size() != 0)
            {
                m_lastKeyLocks = input->keyLocks;
                uint16_t count1 = 0;
                for (size_t i = 0; i < m_lastKeyLocks.size(); ++i)
                {
                    auto value = m_lastKeyLocks[i].compare("key" + std::to_string(i));
                    BOOST_CHECK_EQUAL(value, 0);
                    count1++;
                }
                BOOST_CHECK_EQUAL(count1, 1);
                std::vector<std::string> appendLocks = {"key1"};
                std::copy(
                    appendLocks.begin(), appendLocks.end(), std::back_inserter(m_lastKeyLocks));
                input->keyLocks = m_lastKeyLocks;
                input->type = CallParameters::KEY_LOCK;
                EXECUTOR_LOG(DEBUG) << LOG_KV("contextID", input->contextID)
                                    << LOG_KV("keylocks_size", input->keyLocks.size());
                return input;
            }
            else if (input->contextID == 0 && input->keyLocks.size() != 0)
            {
                EXECUTOR_LOG(DEBUG)
                    << LOG_KV("contextID", input->contextID)
                    << LOG_KV("keylocks_size", input->keyLocks.size()) << input->keyLocks[0]
                    << input->keyLocks[1] << input->keyLocks[2];
                m_lastKeyLocks = input->keyLocks;
                uint16_t count = 0;
                for (size_t i = 0; i < m_lastKeyLocks.size(); ++i)
                {
                    if (i >= 1)
                    {
                        auto value = m_lastKeyLocks[i].compare("key" + std::to_string(i - 1));
                        BOOST_CHECK_EQUAL(value, 0);
                        count++;
                    }
                    else
                    {
                        auto value = m_lastKeyLocks[i].compare("key" + std::to_string(i));
                        BOOST_CHECK_EQUAL(value, 0);
                        count++;
                    }
                }
                BOOST_CHECK_EQUAL(count, 3);
                std::vector<std::string> appendLocks = {"key2"};
                std::copy(
                    appendLocks.begin(), appendLocks.end(), std::back_inserter(m_lastKeyLocks));
                input->keyLocks = m_lastKeyLocks;
                input->type = CallParameters::KEY_LOCK;
                EXECUTOR_LOG(DEBUG) << LOG_KV("contextID", input->contextID)
                                    << LOG_KV("keylocks_size", input->keyLocks.size());
                return input;
            }
            else
            {
                return input;
            }
        }
        else
        {
            return input;
        }
    }
    CallParameters::UniquePtr resume() override
    {
        auto callParameters = std::make_unique<CallParameters>(CallParameters::Type::MESSAGE);
        callParameters->staticCall = false;
        callParameters->codeAddress = "aabbccddee";
        callParameters->contextID = 1;
        callParameters->seq = 1;
        return callParameters;
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
    std::vector<std::string> m_lastKeyLocks;
};
}  // namespace bcos::test
