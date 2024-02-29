#include "Common.h"
#include "Transfer20ByteCode.h"
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
#include <oneapi/tbb/parallel_for.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
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

void query(bcos::sdk::RPCClient& rpcClient, std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite,
    std::string contractAddress, std::vector<User>& users, int qps)
{
    bcostars::protocol::TransactionFactoryImpl transactionFactory(cryptoSuite);
    bcos::ratelimiter::TimeWindowRateLimiter limiter(qps);
    bcos::sample::Collector collector(users.size(), "Query");

    tbb::parallel_for(tbb::blocked_range(0LU, users.size()), [&](const auto& range) {
        for (auto i = range.begin(); i != range.end(); ++i)
        {
            limiter.acquire(1);
            bcos::codec::abi::ContractABICodec abiCodec1(cryptoSuite->hashImpl());
            auto input = abiCodec1.abiIn(
                "availableBalance(address)", users[i].keyPair->address(cryptoSuite->hashImpl()));
            auto transaction = transactionFactory.createTransaction(0, contractAddress, input,
                rpcClient.generateNonce(), blockNumber + blockLimit, "chain0", "group0", 0);

            collector.send(true, 0);
            auto now = bcos::sample::currentTime();

            auto receipt =
                bcos::task::tbb::syncWait(bcos::sdk::async::call(rpcClient, *transaction)).get();

            auto elapsed = bcos::sample::currentTime() - now;
            if (receipt->status() != 0)
            {
                std::cout << "Query with error! " << receipt->status() << std::endl;
                BOOST_THROW_EXCEPTION(std::runtime_error("Query with error!"));
                collector.receive(false, elapsed);
            }
            else
            {
                auto output = receipt->output();
                bcos::codec::abi::ContractABICodec abiCodec2(cryptoSuite->hashImpl());
                bcos::s256 balance;
                abiCodec2.abiOut(output, balance);
                users[i].balance = balance.convert_to<long>();
                collector.receive(true, elapsed);
            }
        }
    });
    collector.finishSend();
    collector.report();
}

void issue(bcos::sdk::RPCClient& rpcClient, std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite,
    std::string contractAddress, std::shared_ptr<bcos::crypto::KeyPairInterface> adminKeyPair,
    std::vector<User>& users, int qps)
{
    bcostars::protocol::TransactionFactoryImpl transactionFactory(cryptoSuite);
    bcos::ratelimiter::TimeWindowRateLimiter limiter(qps);
    bcos::sample::Collector collector(users.size(), "Issue");

    tbb::task_group group;
    for (auto& user : users)
    {
        group.run([&]() {
            limiter.acquire(1);
            bcos::codec::abi::ContractABICodec abiCodec1(cryptoSuite->hashImpl());
            auto input = abiCodec1.abiIn("mint(address,uint256)",
                user.keyPair->address(cryptoSuite->hashImpl()), bcos::u256(initialValue));
            auto transaction = transactionFactory.createTransaction(0, contractAddress, input,
                rpcClient.generateNonce(), blockNumber + blockLimit, "chain0", "group0", 0,
                *adminKeyPair);

            collector.send(true, 0);
            auto now = bcos::sample::currentTime();

            auto receipt = bcos::task::tbb::syncWait(
                bcos::sdk::async::sendTransaction(rpcClient, *transaction))
                               .get();

            auto elapsed = bcos::sample::currentTime() - now;
            if (receipt->status() != 0)
            {
                std::cout << "Issue with error! " << receipt->status() << " | "
                          << bcos::sample::parseRevertMessage(
                                 receipt->output(), cryptoSuite->hashImpl())
                          << std::endl;
                collector.receive(false, elapsed);
            }
            else
            {
                user.balance += initialValue;
                collector.receive(true, elapsed);
            }
        });
    }
    group.wait();
    collector.finishSend();
    collector.report();
}

void transfer(bcos::sdk::RPCClient& rpcClient,
    std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite, std::string contractAddress,
    std::shared_ptr<bcos::crypto::KeyPairInterface> keyPair, std::vector<User>& users,
    int transactionCount, int qps)
{
    bcostars::protocol::TransactionFactoryImpl transactionFactory(cryptoSuite);
    bcos::ratelimiter::TimeWindowRateLimiter limiter(qps);
    bcos::sample::Collector collector(transactionCount, "Transfer");

    tbb::task_group group;
    for (auto it : RANGES::views::iota(0, transactionCount))
    {
        group.run([&, i = it]() {
            limiter.acquire(1);
            auto fromAddress = i % users.size();
            auto toAddress = ((i + (users.size() / 2)) % users.size());

            bcos::codec::abi::ContractABICodec abiCodec(cryptoSuite->hashImpl());
            auto input = abiCodec.abiIn("transfer(address,uint256)",
                users[toAddress].keyPair->address(cryptoSuite->hashImpl()), bcos::u256(1));
            auto transaction = transactionFactory.createTransaction(0, contractAddress, input,
                rpcClient.generateNonce(), blockNumber + blockLimit, "chain0", "group0", 0,
                *users[fromAddress].keyPair);
            collector.send(true, 0);

            auto now = bcos::sample::currentTime();
            auto receipt = bcos::task::tbb::syncWait(
                bcos::sdk::async::sendTransaction(rpcClient, *transaction))
                               .get();
            auto elapsed = bcos::sample::currentTime() - now;
            if (receipt->status() != 0)
            {
                std::cout << "Transfer with error! " << receipt->status() << " | "
                          << bcos::sample::parseRevertMessage(
                                 receipt->output(), cryptoSuite->hashImpl())
                          << std::endl;
                collector.receive(false, elapsed);
            }
            else
            {
                collector.receive(true, elapsed);
                --users[fromAddress].balance;
                ++users[toAddress].balance;
            }
        });
    }
    group.wait();
    collector.finishSend();
    collector.report();
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
    if (argc < 5)
    {
        std::cout << "Usage: " << argv[0]
                  << " <connectionString> <userCount> <transactionCount> <qps>" << std::endl
                  << "Example: " << argv[0]
                  << " \"fiscobcos.rpc.RPCObj@tcp -h 127.0.0.1 -p 20021\" 100 1000 100"
                  << std::endl;

        return 1;
    }

    tbb::task_group group;
    std::string connectionString = argv[1];
    int userCount = boost::lexical_cast<int>(argv[2]);
    int transactionCount = boost::lexical_cast<int>(argv[3]);
    int qps = boost::lexical_cast<int>(argv[4]);

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

    bcos::bytes deployBin;
    boost::algorithm::unhex(TRANSFER20_BYTECODE, std::back_inserter(deployBin));
    bcos::codec::abi::ContractABICodec abiCodec(cryptoSuite->hashImpl());
    auto deployParam = abiCodec.abiIn("", std::string("test_token"), std::string("tt"), false);
    deployBin.insert(deployBin.end(), deployParam.begin(), deployParam.end());

    auto deployTransaction = transactionFactory.createTransaction(0, "", deployBin,
        rpcClient.generateNonce(), blockNumber + blockLimit, "chain0", "group0", 0, *adminKeyPair);
    auto receipt =
        bcos::task::syncWait(bcos::sdk::async::sendTransaction(rpcClient, *deployTransaction))
            .get();

    if (receipt->status() != 0)
    {
        std::cout << "Deploy contract failed" << receipt->status() << std::endl;
        return 1;
    }
    auto contractAddress = receipt->contractAddress();
    auto users = initUsers(userCount, *cryptoSuite);

    std::cout << "Contract address is:" << contractAddress << std::endl;
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
