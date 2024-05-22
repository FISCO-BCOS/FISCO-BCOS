#include "Common.h"
#include "bcos-cpp-sdk/tarsRPC/RPCClient.h"
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-utilities/FixedBytes.h"
#include "bcos-utilities/ratelimiter/TimeWindowRateLimiter.h"
#include "bcos-utilities/ratelimiter/TokenBucketRateLimiter.h"
#include <bcos-codec/abi/ContractABICodec.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
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
const static std::string DAG_TRANSFER_ADDRESS = "000000000000000000000000000000000000100c";

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

std::vector<std::atomic_long> query(bcos::sdk::RPCClient& rpcClient,
    std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite, std::string contractAddress,
    int userCount)
{
    bcostars::protocol::TransactionFactoryImpl transactionFactory(cryptoSuite);
    boost::latch latch(userCount);
    std::vector<std::optional<bcos::sdk::Call>> handles(userCount);

    bcos::sample::Collector collector(userCount, "Query");
    tbb::parallel_for(tbb::blocked_range(0LU, (size_t)userCount), [&](const auto& range) {
        for (auto it = range.begin(); it != range.end(); ++it)
        {
            bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
            bcos::bytes input;
            if (contractAddress == DAG_TRANSFER_ADDRESS)
            {
                input = abiCodec.abiIn("userBalance(string)", std::to_string(it));
            }
            else
            {
                input = abiCodec.abiIn("balance(address)", bcos::Address(it));
            }
            auto transaction = transactionFactory.createTransaction(0, contractAddress, input,
                rpcClient.generateNonce(), blockNumber + blockLimit, "chain0", "group0", 0);

            handles[it].emplace(rpcClient);
            auto& call = *(handles[it]);
            call.setCallback(std::make_shared<PerformanceCallback>(latch, collector));
            call.send(*transaction);

            collector.send(true, 0);
        }
    });
    collector.finishSend();
    latch.wait();
    collector.report();

    std::vector<std::atomic_long> balances(userCount);
    // Check result
    tbb::parallel_for(tbb::blocked_range(0LU, (size_t)userCount), [&](const auto& range) {
        for (auto it = range.begin(); it != range.end(); ++it)
        {
            auto receipt = handles[it]->get();
            if (receipt->status() != 0)
            {
                std::cout << "Query with error! " << receipt->status() << std::endl;
                BOOST_THROW_EXCEPTION(std::runtime_error("Query with error!"));
            }

            auto output = receipt->output();
            bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
            bcos::s256 balance;
            abiCodec.abiOut(output, balance);

            balances[it] = balance.convert_to<long>();
        }
    });

    return balances;
}

int issue(bcos::sdk::RPCClient& rpcClient, std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite,
    std::shared_ptr<bcos::crypto::KeyPairInterface> keyPair, std::string contractAddress,
    int userCount, [[maybe_unused]] int qps)
{
    bcostars::protocol::TransactionFactoryImpl transactionFactory(cryptoSuite);
    boost::latch latch(userCount);
    std::vector<std::optional<bcos::sdk::SendTransaction>> handles(userCount);

    bcos::sample::Collector collector(userCount, "Issue");
    tbb::parallel_for(tbb::blocked_range(0LU, (size_t)userCount), [&](const auto& range) {
        for (auto it = range.begin(); it != range.end(); ++it)
        {
            bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
            bcos::bytes input;
            if (contractAddress == DAG_TRANSFER_ADDRESS)
            {
                input = abiCodec.abiIn(
                    "userAdd(string,uint256)", std::to_string(it), bcos::u256(initialValue));
            }
            else
            {
                input = abiCodec.abiIn(
                    "issue(address,int256)", bcos::Address(it), bcos::s256(initialValue));
            }
            auto transaction = transactionFactory.createTransaction(0, contractAddress, input,
                rpcClient.generateNonce(), blockNumber + blockLimit, "chain0", "group0", 0,
                *keyPair);
            transaction->setAttribute(bcos::protocol::Transaction::Attribute::EVM_ABI_CODEC |
                                      bcos::protocol::Transaction::Attribute::DAG);

            handles[it].emplace(rpcClient);
            auto& sendTransaction = *(handles[it]);
            sendTransaction.setCallback(std::make_shared<PerformanceCallback>(latch, collector));
            sendTransaction.send(*transaction);

            collector.send(true, 0);
        }
    });
    collector.finishSend();
    latch.wait();
    collector.report();

    // Check result
    tbb::parallel_for(tbb::blocked_range(0LU, (size_t)userCount), [&](const auto& range) {
        for (auto it = range.begin(); it != range.end(); ++it)
        {
            auto receipt = handles[it]->get();
            if (receipt->status() != 0)
            {
                std::cout << "Issue with error! " << receipt->status() << std::endl;
                BOOST_THROW_EXCEPTION(std::runtime_error("Issue with error!"));
            }
        }
    });

    return 0;
}

void perfQuery(bcos::sdk::RPCClient& rpcClient,
    std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite, std::string contractAddress,
    int userCount, int total, int qps)
{
    bcostars::protocol::TransactionFactoryImpl transactionFactory(cryptoSuite);
    boost::latch latch(total);
    std::vector<std::optional<bcos::sdk::Call>> handles(total);

    bcos::ratelimiter::TimeWindowRateLimiter limiter(qps);
    bcos::sample::Collector collector(total, "Query");
    tbb::parallel_for(tbb::blocked_range(0LU, (size_t)total), [&](const auto& range) {
        for (auto it = range.begin(); it != range.end(); ++it)
        {
            limiter.acquire(1);
            auto userIndex = it % userCount;
            bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
            bcos::bytes input;
            if (contractAddress == DAG_TRANSFER_ADDRESS)
            {
                input = abiCodec.abiIn("userBalance(string)", std::to_string(userIndex));
            }
            else
            {
                input = abiCodec.abiIn("balance(address)", bcos::Address(userIndex));
            }
            auto transaction = transactionFactory.createTransaction(0, contractAddress, input,
                rpcClient.generateNonce(), blockNumber + blockLimit, "chain0", "group0", 0);

            handles[it].emplace(rpcClient);
            auto& call = *(handles[it]);
            call.setCallback(std::make_shared<PerformanceCallback>(latch, collector));
            call.send(*transaction);

            collector.send(true, 0);
        }
    });
    collector.finishSend();
    latch.wait();
    collector.report();

    std::vector<std::atomic_long> balances(userCount);
    // Check result
    tbb::parallel_for(tbb::blocked_range(0LU, (size_t)userCount), [&](const auto& range) {
        for (auto it = range.begin(); it != range.end(); ++it)
        {
            auto receipt = handles[it]->get();
            if (receipt->status() != 0)
            {
                std::cout << "Query with error! " << receipt->status() << std::endl;
                BOOST_THROW_EXCEPTION(std::runtime_error("Query with error!"));
            }

            auto output = receipt->output();
            bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
            bcos::s256 balance;
            abiCodec.abiOut(output, balance);

            balances[it] = balance.convert_to<long>();
        }
    });
}


#if __cpp_lib_jthread
void loopFetchBlockNumber(std::stop_token& token, bcos::sdk::RPCClient& rpcClient)
{
    while (!token.stop_requested())
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
#else
void loopFetchBlockNumber(bcos::sdk::RPCClient& rpcClient)
{
    while (true)
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
#endif

int main(int argc, char* argv[])
{
    if (argc < 6)
    {
        std::cout
            << "Usage: " << argv[0]
            << " <connectionString> <solidity/precompiled> <userCount> <transactionCount> <qps>"
            << std::endl
            << "Example: " << argv[0]
            << " \"fiscobcos.rpc.RPCObj@tcp -h 127.0.0.1 -p 20021\" 100 1000 0 " << std::endl;

        return 1;
    }
    std::string connectionString = argv[1];
    std::string type = argv[2];
    int userCount = boost::lexical_cast<int>(argv[3]);
    int transactionCount = boost::lexical_cast<int>(argv[4]);
    int qps = boost::lexical_cast<int>(argv[5]);

    bcos::sdk::Config config = {
        .connectionString = connectionString,
        .sendQueueSize = std::max(userCount, transactionCount),
        .timeoutMs = 600000,
    };
    bcos::sdk::RPCClient rpcClient(config);

    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
            std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
    auto keyPair = std::shared_ptr<bcos::crypto::KeyPairInterface>(
        cryptoSuite->signatureImpl()->generateKeyPair());

    bcostars::protocol::TransactionFactoryImpl transactionFactory(cryptoSuite);
    std::string contractAddress = DAG_TRANSFER_ADDRESS;
    if (type == "solidity")
    {
        bcos::bytes deployBin = bcos::sample::getContractBin();
        auto deployTransaction = transactionFactory.createTransaction(0, "", deployBin,
            rpcClient.generateNonce(), blockNumber + blockLimit, "chain0", "group0", 0, *keyPair,
            std::string{bcos::sample::getContractABI()});
        auto receipt = bcos::sdk::SendTransaction(rpcClient).send(*deployTransaction).get();

        if (receipt->status() != 0)
        {
            std::cout << "Deploy contract failed" << receipt->status() << std::endl;
            return 1;
        }
        contractAddress = receipt->contractAddress();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "Contract address is:" << contractAddress << std::endl;
    issue(rpcClient, cryptoSuite, keyPair, std::string(contractAddress), userCount, qps);
    perfQuery(
        rpcClient, cryptoSuite, std::string(contractAddress), userCount, transactionCount, qps);

    return 0;
}