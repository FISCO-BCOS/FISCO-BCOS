#include "Common.h"
#include "Transfer20ByteCode.h"
#include "TransparentUpgradeableProxyByteCode.h"
#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-cpp-sdk/tarsRPC/CoRPCClient.h"
#include "bcos-cpp-sdk/tarsRPC/RPCClient.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-task/TBBWait.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/FixedBytes.h"
#include "bcos-utilities/ratelimiter/TimeWindowRateLimiter.h"
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task_group.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/thread/latch.hpp>
#include <boost/throw_exception.hpp>
#include <atomic>
#include <chrono>
#include <exception>
#include <random>
#include <string>
#include <thread>

std::atomic_long blockNumber = 0;
constexpr static long blockLimit = 900;
constexpr static int64_t initialValue = 1000000000;
struct User
{
    User() = default;
    User(User&&) = delete;
    User& operator=(User&&) = delete;
    User(const User& user) : keyPair(user.keyPair) {}
    User& operator=(const User& user)
    {
        if (std::addressof(user) == this)
        {
            return *this;
        }
        keyPair = user.keyPair;
        return *this;
    }
    ~User() noexcept = default;

    std::shared_ptr<bcos::crypto::KeyPairInterface> keyPair;
    std::atomic_long balance;
};

std::vector<User> initUsers(int userCount, bcos::crypto::CryptoSuite& cryptoSuite)
{
    std::vector<User> users(userCount);
    tbb::parallel_for(tbb::blocked_range(0, userCount), [&](const auto& range) {
        for (auto i = range.begin(); i != range.end(); ++i)
        {
            users[i].keyPair = std::shared_ptr<bcos::crypto::KeyPairInterface>(
                cryptoSuite.signatureImpl()->generateKeyPair());
            users[i].balance = 0;
        }
    });
    return users;
}

class PerformanceCallback : public bcos::sdk::Callback
{
private:
    boost::latch& m_latch;
    bcos::sample::Collector& m_collector;
    long m_startTime;

public:
    PerformanceCallback(boost::latch& latch, bcos::sample::Collector& collector)
      : m_latch(latch), m_collector(collector), m_startTime(bcos::sample::currentTime())
    {}

    void onMessage([[maybe_unused]] int seq) override
    {
        m_latch.count_down();
        m_collector.receive(true, bcos::sample::currentTime() - m_startTime);
    }
};

void query(bcos::sdk::RPCClient& rpcClient, std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite,
    std::string contractAddress, std::vector<User>& users, int qps)
{
    bcostars::protocol::TransactionFactoryImpl transactionFactory(cryptoSuite);
    bcos::ratelimiter::TimeWindowRateLimiter limiter(qps);
    bcos::sample::Collector collector(users.size(), "Query");
    std::vector<std::optional<bcos::sdk::Call>> handles(users.size());
    boost::latch latch(users.size());

    tbb::parallel_for(::tbb::blocked_range(0LU, users.size()), [&](const auto& range) {
        for (auto i = range.begin(); i != range.end(); ++i)
        {
            limiter.acquire(1);
            bcos::codec::abi::ContractABICodec abiCodec1(*cryptoSuite->hashImpl());
            auto input = abiCodec1.abiIn(
                "availableBalance(address)", users[i].keyPair->address(cryptoSuite->hashImpl()));
            auto transaction = transactionFactory.createTransaction(0, contractAddress, input,
                rpcClient.generateNonce(), blockNumber + blockLimit, "chain0", "group0", 0);


            handles[i].emplace(rpcClient);
            handles[i]->setCallback(std::make_shared<PerformanceCallback>(latch, collector));
            handles[i]->send(*transaction);
            collector.send(true, 0);
        }
    });
    collector.finishSend();
    latch.wait();
    collector.report();

    tbb::parallel_for(::tbb::blocked_range(0LU, users.size()), [&](const auto& range) {
        for (auto i = range.begin(); i != range.end(); ++i)
        {
            auto receipt = handles[i]->get();
            if (receipt->status() != 0)
            {
                std::cout << "Query with error! " << receipt->status() << std::endl;
                BOOST_THROW_EXCEPTION(std::runtime_error("Query with error!"));
            }

            auto output = receipt->output();
            bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
            bcos::s256 balance;
            abiCodec.abiOut(output, balance);
            users[i].balance = balance.convert_to<long>();
        }
    });
}

void issue(bcos::sdk::RPCClient& rpcClient, std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite,
    std::string contractAddress, std::shared_ptr<bcos::crypto::KeyPairInterface> adminKeyPair,
    std::vector<User>& users, int qps)
{
    bcostars::protocol::TransactionFactoryImpl transactionFactory(cryptoSuite);
    bcos::ratelimiter::TimeWindowRateLimiter limiter(qps);
    bcos::sample::Collector collector(users.size(), "Issue");
    std::vector<std::optional<bcos::sdk::SendTransaction>> handles(users.size());
    boost::latch latch(users.size());

    tbb::parallel_for(::tbb::blocked_range(0LU, users.size()), [&](const auto& range) {
        for (auto i = range.begin(); i != range.end(); ++i)
        {
            limiter.acquire(1);
            bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
            auto input = abiCodec.abiIn("mint(address,uint256)",
                users[i].keyPair->address(cryptoSuite->hashImpl()), bcos::u256(initialValue));
            auto transaction = transactionFactory.createTransaction(0, contractAddress, input,
                rpcClient.generateNonce(), blockNumber + blockLimit, "chain0", "group0", 0,
                *adminKeyPair);

            handles[i].emplace(rpcClient);
            handles[i]->setCallback(std::make_shared<PerformanceCallback>(latch, collector));
            handles[i]->send(*transaction);

            collector.send(true, 0);
            users[i].balance += initialValue;
        }
    });
    collector.finishSend();
    latch.wait();
    collector.report();

    // Check result
    tbb::parallel_for(tbb::blocked_range(0LU, users.size()), [&](const auto& range) {
        for (auto i = range.begin(); i != range.end(); ++i)
        {
            auto receipt = handles[i]->get();
            if (receipt->status() != 0)
            {
                std::cout << "Issue with error! " << receipt->status() << std::endl;
                BOOST_THROW_EXCEPTION(std::runtime_error("Issue with error!"));
            }
        }
    });
}

void transfer(bcos::sdk::RPCClient& rpcClient,
    std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite, std::string contractAddress,
    std::shared_ptr<bcos::crypto::KeyPairInterface> keyPair, std::vector<User>& users,
    int transactionCount, int qps)
{
    bcostars::protocol::TransactionFactoryImpl transactionFactory(cryptoSuite);
    bcos::ratelimiter::TimeWindowRateLimiter limiter(qps);
    bcos::sample::Collector collector(transactionCount, "Transfer");
    std::vector<std::optional<bcos::sdk::SendTransaction>> handles(transactionCount);
    boost::latch latch(transactionCount);

    tbb::parallel_for(::tbb::blocked_range(0, transactionCount), [&](const auto& range) {
        for (auto i = range.begin(); i != range.end(); ++i)
        {
            limiter.acquire(1);
            auto fromAddress = i % users.size();
            auto toAddress = ((i + (users.size() / 2)) % users.size());

            bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
            auto input = abiCodec.abiIn("transfer(address,uint256)",
                users[toAddress].keyPair->address(cryptoSuite->hashImpl()), bcos::u256(1));
            auto transaction = transactionFactory.createTransaction(0, contractAddress, input,
                rpcClient.generateNonce(), blockNumber + blockLimit, "chain0", "group0", 0,
                *users[fromAddress].keyPair);
            handles[i].emplace(rpcClient);
            handles[i]->setCallback(std::make_shared<PerformanceCallback>(latch, collector));
            handles[i]->send(*transaction);

            collector.send(true, 0);
            --users[fromAddress].balance;
            ++users[toAddress].balance;
        }
    });
    collector.finishSend();
    latch.wait();
    collector.report();

    // Check result
    tbb::parallel_for(tbb::blocked_range(0LU, (size_t)transactionCount), [&](const auto& range) {
        for (auto i = range.begin(); i != range.end(); ++i)
        {
            auto receipt = handles[i]->get();
            if (receipt->status() != 0)
            {
                std::cout << "Transfer with error! " << receipt->status() << std::endl;
                BOOST_THROW_EXCEPTION(std::runtime_error("Transfer with error!"));
            }
        }
    });
}

void loopFetchBlockNumber(bcos::sdk::RPCClient& rpcClient, boost::atomic_flag const& stopFlag)
{
    while (!stopFlag.test())
    {
        try
        {
            blockNumber = bcos::sdk::BlockNumber(rpcClient).send().get();
        }
        catch (std::exception& e)
        {
            std::cout << boost::diagnostic_information(e);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int main(int argc, char* argv[])
{
    if (argc < 6)
    {
        std::cout << "Usage: " << argv[0]
                  << " <connectionString> <userCount> <transactionCount> <qps> <proxy>" << std::endl
                  << "Example: " << argv[0]
                  << " \"fiscobcos.rpc.RPCObj@tcp -h 127.0.0.1 -p 20021\" 100 1000 100 0"
                  << std::endl;

        return 1;
    }

    std::string connectionString = argv[1];
    int userCount = boost::lexical_cast<int>(argv[2]);
    int transactionCount = boost::lexical_cast<int>(argv[3]);
    int qps = boost::lexical_cast<int>(argv[4]);
    bool proxy = boost::lexical_cast<bool>(argv[5]);

    bcos::sdk::Config config = {
        .connectionString = connectionString,
        .sendQueueSize = std::max(userCount, transactionCount),
        .timeoutMs = 600000,
    };
    bcos::sdk::RPCClient rpcClient(config);
    boost::atomic_flag stopFlag{};
    std::thread getBlockNumber([&]() { loopFetchBlockNumber(rpcClient, stopFlag); });
    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
            std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
    auto adminKeyPair = std::shared_ptr<bcos::crypto::KeyPairInterface>(
        cryptoSuite->signatureImpl()->generateKeyPair());

    bcostars::protocol::TransactionFactoryImpl transactionFactory(cryptoSuite);

    bcos::bytes transfer20Bin;
    boost::algorithm::unhex(TRANSFER20_BYTECODE, std::back_inserter(transfer20Bin));
    bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
    auto deployParam = abiCodec.abiIn("", std::string("test_token"), std::string("tt"), false);
    transfer20Bin.insert(transfer20Bin.end(), deployParam.begin(), deployParam.end());

    auto deployTransfer20Transaction = transactionFactory.createTransaction(0, "", transfer20Bin,
        rpcClient.generateNonce(), blockNumber + blockLimit, "chain0", "group0", 0, *adminKeyPair);
    auto receipt = bcos::task::syncWait(
        bcos::sdk::async::sendTransaction(rpcClient, *deployTransfer20Transaction))
                       .get();
    if (receipt->status() != 0)
    {
        std::cout << "Deploy contract failed" << receipt->status() << std::endl;
        return 1;
    }
    auto transfer20ContractAddress = receipt->contractAddress();
    std::cout << "Transfer20 contract address is:" << transfer20ContractAddress << std::endl;

    bcos::bytes tupBin;
    boost::algorithm::unhex(TRANSPARENT_UPGRADEABLE_PROXY_BYTECODE, std::back_inserter(tupBin));
    bcos::codec::abi::ContractABICodec abiCodec2(*cryptoSuite->hashImpl());
    auto ownerKeyPair = std::shared_ptr<bcos::crypto::KeyPairInterface>(
        cryptoSuite->signatureImpl()->generateKeyPair());
    auto tupParam = abiCodec2.abiIn("", bcos::toAddress(std::string(transfer20ContractAddress)),
        ownerKeyPair->address(cryptoSuite->hashImpl()), bcos::bytes{});
    tupBin.insert(tupBin.end(), tupParam.begin(), tupParam.end());

    auto deployTupTransaction = transactionFactory.createTransaction(0, "", tupBin,
        rpcClient.generateNonce(), blockNumber + blockLimit, "chain0", "group0", 0, *adminKeyPair);
    auto receipt2 =
        bcos::task::syncWait(bcos::sdk::async::sendTransaction(rpcClient, *deployTupTransaction))
            .get();
    if (receipt2->status() != 0)
    {
        std::cout << "Deploy tup failed" << receipt2->status() << std::endl;
        return 1;
    }
    std::string contractAddress;
    if (proxy)
    {
        contractAddress = receipt2->contractAddress();
    }
    else
    {
        contractAddress = transfer20ContractAddress;
    }
    std::cout << "Target contract address is:" << contractAddress << std::endl;

    auto users = initUsers(userCount, *cryptoSuite);
    query(rpcClient, cryptoSuite, std::string(contractAddress), users, qps);
    issue(rpcClient, cryptoSuite, std::string(contractAddress), adminKeyPair, users, qps);
    transfer(rpcClient, cryptoSuite, std::string(contractAddress), adminKeyPair, users,
        transactionCount, qps);

    auto resultUsers = users;
    query(rpcClient, cryptoSuite, std::string(contractAddress), resultUsers, qps);

    // Compare the result
    for (int i = 0; i < userCount; ++i)
    {
        if (users[i].balance != resultUsers[i].balance)
        {
            std::cout << "Balance not match! " << users[i].balance << " " << resultUsers[i].balance
                      << std::endl;
            exit(1);
        }
    }
    stopFlag.test_and_set();
    getBlockNumber.join();

    return 0;
}
