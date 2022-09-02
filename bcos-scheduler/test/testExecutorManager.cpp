#include "ExecutorManager.h"
#include "bcos-framework/executor/ParallelTransactionExecutorInterface.h"
#include "mock/MockExecutor.h"
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>
#include <memory>

namespace bcos::test
{
struct ExecutorManagerFixture
{
    ExecutorManagerFixture() { executorManager = std::make_shared<scheduler::ExecutorManager>(); }

    scheduler::ExecutorManager::Ptr executorManager;
};

BOOST_FIXTURE_TEST_SUITE(TestExecutorManager, ExecutorManagerFixture)

BOOST_AUTO_TEST_CASE(addExecutor)
{
    BOOST_CHECK_NO_THROW(
        executorManager->addExecutor("1", std::make_shared<MockParallelExecutor>("1")));
    BOOST_CHECK_NO_THROW(
        executorManager->addExecutor("2", std::make_shared<MockParallelExecutor>("2")));
    BOOST_CHECK_NO_THROW(
        executorManager->addExecutor("3", std::make_shared<MockParallelExecutor>("3")));
    BOOST_CHECK_NO_THROW(
        executorManager->addExecutor("3", std::make_shared<MockParallelExecutor>("3")));
}

BOOST_AUTO_TEST_CASE(dispatch)
{
    BOOST_CHECK_NO_THROW(
        executorManager->addExecutor("1", std::make_shared<MockParallelExecutor>("1")));
    BOOST_CHECK_NO_THROW(
        executorManager->addExecutor("2", std::make_shared<MockParallelExecutor>("2")));
    BOOST_CHECK_NO_THROW(
        executorManager->addExecutor("3", std::make_shared<MockParallelExecutor>("3")));
    BOOST_CHECK_NO_THROW(
        executorManager->addExecutor("4", std::make_shared<MockParallelExecutor>("4")));

    std::vector<std::string> contracts;
    for (int i = 0; i < 100; ++i)
    {
        contracts.push_back(boost::lexical_cast<std::string>(i));
    }

    std::vector<bcos::executor::ParallelTransactionExecutorInterface::Ptr> executors;
    for (auto& it : contracts)
    {
        executors.push_back(executorManager->dispatchExecutor(it));
    }
    BOOST_CHECK_EQUAL(executors.size(), 100);

    std::map<std::string, int> executor2count{
        std::pair("1", 0), std::pair("2", 0), std::pair("3", 0), std::pair("4", 0)};
    for (auto it = executors.begin(); it != executors.end(); ++it)
    {
        ++executor2count[std::dynamic_pointer_cast<MockParallelExecutor>(*it)->name()];
    }

    BOOST_CHECK_EQUAL(executor2count["1"], 25);
    BOOST_CHECK_EQUAL(executor2count["2"], 25);
    BOOST_CHECK_EQUAL(executor2count["3"], 25);
    BOOST_CHECK_EQUAL(executor2count["4"], 25);

    std::vector<bcos::executor::ParallelTransactionExecutorInterface::Ptr> executors2;
    for (auto& it : contracts)
    {
        executors2.push_back(executorManager->dispatchExecutor(it));
    }
    BOOST_CHECK_EQUAL_COLLECTIONS(
        executors.begin(), executors.end(), executors2.begin(), executors2.end());

    std::vector<std::string> contracts2;
    for (int i = 0; i < 40; ++i)
    {
        contracts2.push_back(boost::lexical_cast<std::string>(i + 1000));
    }

    contracts2.insert(contracts2.end(), contracts.begin(), contracts.end());

    std::vector<bcos::executor::ParallelTransactionExecutorInterface::Ptr> executors3;
    for (auto& it : contracts2)
    {
        executors3.push_back(executorManager->dispatchExecutor(it));
    }
    std::map<std::string, int> executor2count2{
        std::pair("1", 0), std::pair("2", 0), std::pair("3", 0), std::pair("4", 0)};
    for (auto it = executors3.begin(); it != executors3.end(); ++it)
    {
        ++executor2count2[std::dynamic_pointer_cast<MockParallelExecutor>(*it)->name()];
    }

    BOOST_CHECK_EQUAL(executor2count2["1"], 35);
    BOOST_CHECK_EQUAL(executor2count2["2"], 35);
    BOOST_CHECK_EQUAL(executor2count2["3"], 35);
    BOOST_CHECK_EQUAL(executor2count2["4"], 35);

    std::vector<bcos::executor::ParallelTransactionExecutorInterface::Ptr> executors4;
    for (auto& it : contracts2)
    {
        executors4.push_back(executorManager->dispatchExecutor(it));
    }

    std::map<std::string, executor::ParallelTransactionExecutorInterface::Ptr> contract2executor;
    for (size_t i = 0; i < contracts2.size(); ++i)
    {
        contract2executor.insert({contracts2[i], executors4[i]});
    }

    BOOST_CHECK_EQUAL_COLLECTIONS(
        executors3.begin(), executors3.end(), executors4.begin(), executors4.end());

    // record executor3's contract
    std::set<std::string> contractsInExecutor3;
    for (size_t i = 0; i < executors4.size(); ++i)
    {
        auto executor = executors4[i];
        if (std::dynamic_pointer_cast<MockParallelExecutor>(executor)->name() == "3")
        {
            contractsInExecutor3.insert(contracts2[i]);
        }
    }

    BOOST_CHECK_EQUAL(contractsInExecutor3.size(), 35);

    BOOST_CHECK_NO_THROW(executorManager->removeExecutor("3"));

    std::vector<std::string> contracts3;
    for (int i = 0; i < 10; ++i)
    {
        contracts2.push_back(boost::lexical_cast<std::string>(i + 2000));
    }

    contracts2.insert(contracts2.end(), contracts3.begin(), contracts3.end());

    std::vector<bcos::executor::ParallelTransactionExecutorInterface::Ptr> executors5;
    for (auto& it : contracts2)
    {
        executors5.push_back(executorManager->dispatchExecutor(it));
    }
    BOOST_CHECK_EQUAL(executors5.size(), 150);

    size_t oldContract = 0;
    for (size_t i = 0; i < contracts2.size(); ++i)
    {
        auto contract = contracts2[i];
        if (boost::lexical_cast<int>(contract) < 2000 &&
            contractsInExecutor3.find(contract) == contractsInExecutor3.end())
        {
            ++oldContract;

            BOOST_CHECK_EQUAL(executors5[i], contract2executor[contract]);
        }
    }

    BOOST_CHECK_EQUAL(oldContract, 150 - 35 - 10);  // exclude new contract and executor3's contract
}

BOOST_AUTO_TEST_CASE(remove)
{
    BOOST_CHECK_NO_THROW(
        executorManager->addExecutor("1", std::make_shared<MockParallelExecutor>("1")));
    BOOST_CHECK_NO_THROW(
        executorManager->addExecutor("2", std::make_shared<MockParallelExecutor>("2")));
    BOOST_CHECK_NO_THROW(
        executorManager->addExecutor("3", std::make_shared<MockParallelExecutor>("3")));
    BOOST_CHECK_NO_THROW(
        executorManager->addExecutor("3", std::make_shared<MockParallelExecutor>("3")));

    BOOST_CHECK_NO_THROW(executorManager->removeExecutor("2"));
    BOOST_CHECK_THROW(executorManager->removeExecutor("10"), bcos::Exception);
    BOOST_CHECK_THROW(executorManager->removeExecutor("2"), bcos::Exception);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test