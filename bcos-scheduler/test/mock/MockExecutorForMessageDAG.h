#pragma once
#include "Common.h"
#include "bcos-framework//executor/ExecutionMessage.h"
#include "bcos-framework//executor/ParallelTransactionExecutorInterface.h"
#include "bcos-framework//protocol/ProtocolTypeDef.h"
#include <boost/core/ignore_unused.hpp>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
class MockParallelExecutorByMessage : public bcos::executor::ParallelTransactionExecutorInterface
{
public:
    MockParallelExecutorByMessage(const std::string& name) : m_name(name) {}

    ~MockParallelExecutorByMessage() override {}

    const std::string& name() const { return m_name; }

    void nextBlockHeader(int64_t schedulerTermId,
        const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
        std::function<void(bcos::Error::UniquePtr)> callback) override
    {
        SCHEDULER_LOG(TRACE) << "Receiving nextBlock: " << blockHeader->number();
        m_blockNumber = blockHeader->number();
        callback(nullptr);  // always success
    }

    void executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {
        // Always success
        BOOST_CHECK(input);
        if (input->type() == bcos::protocol::ExecutionMessage::MESSAGE)
        {
            BOOST_CHECK_NE(input->transactionHash(), bcos::crypto::HashType());
        }

        input->setStatus(0);
        input->setMessage("");

        std::string data = "Hello world!";
        input->setData(bcos::bytes(data.begin(), data.end()));
        input->setType(bcos::protocol::ExecutionMessage::FINISHED);

        callback(nullptr, std::move(input));
    }

    void dagExecuteTransactions(gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        BOOST_CHECK_EQUAL(inputs.size(), 100);

        std::vector<bcos::protocol::ExecutionMessage::UniquePtr> messages(inputs.size());
        for (decltype(inputs)::index_type i = 0; i < inputs.size(); ++i)
        {
            BOOST_TEST(inputs[i].get());
            BOOST_CHECK_EQUAL(inputs[i]->type(), protocol::ExecutionMessage::MESSAGE);
            messages.at(i) = std::move(inputs[i]);
            messages[i]->setType(protocol::ExecutionMessage::FINISHED);

            std::string result = "OK!";
            messages[i]->setData(bcos::bytes(result.begin(), result.end()));
        }

        callback(nullptr, std::move(messages));
    }

    void dmcExecuteTransactions(std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,

        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        std::vector<bcos::protocol::ExecutionMessage::UniquePtr> results(inputs.size());
        for (auto i = 0; i < inputs.size(); i++)
        {
            executeTransaction(std::move(inputs[i]),
                [&](bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr result) {
                    results[i] = std::move(result);
                });
        }
        callback(nullptr, std::move(results));
    };

    void call(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override
    {}

    void getHash(bcos::protocol::BlockNumber number,
        std::function<void(bcos::Error::UniquePtr, crypto::HashType)> callback) override
    {
        callback(nullptr, h256(12345));
    }

    void prepare(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        callback(nullptr);
    }

    void commit(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        callback(nullptr);
    }

    void rollback(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        callback(nullptr);
    }

    void reset(std::function<void(bcos::Error::Ptr)> callback) override {}

    void getCode(std::string_view contract,
        std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) override
    {
        callback(nullptr, {});
    }
    void getABI(std::string_view contract,
        std::function<void(bcos::Error::Ptr, std::string)> callback) override
    {
        callback(nullptr, {});
    }
    void clear() { m_dagHashes.clear(); }

    std::string m_name;
    bcos::protocol::BlockNumber m_blockNumber = 0;
    std::set<bcos::crypto::HashType> m_dagHashes;
};
#pragma GCC diagnostic pop
}  // namespace bcos::test
