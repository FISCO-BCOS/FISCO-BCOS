#include "TestBytecode.h"
#include "bcos-cpp-sdk/tarsRPC/RPCClient.h"
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-task/Wait.h"
#include <bcos-codec/abi/ContractABICodec.h>
#include <bcos-cpp-sdk/tarsRPC/CoRPCClient.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-task/Task.h>
#include <oneapi/tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/timer/progress_display.hpp>
#include <atomic>
#include <exception>
#include <latch>
#include <random>

long currentTime()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct Request
{
    bcos::sdk::Future<bcos::protocol::TransactionReceipt::Ptr> m_future;
    long m_sendTime = 0;
    long m_receiveTime = 0;
};

struct LatchCompletionQueue : public bcos::sdk::CompletionQueue
{
    std::latch& m_latch;
    std::vector<Request>& m_requests;

    LatchCompletionQueue(std::latch& latch, std::vector<Request>& requests)
      : m_latch(latch), m_requests(requests)
    {}
    void notify(std::any tag) override
    {
        auto index = std::any_cast<size_t>(tag);
        m_requests[index].m_receiveTime = currentTime();
        m_latch.count_down();
    }
};

int performance()
{
    try
    {
        auto cryptoSuite =
            std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
                std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
        auto keyPair = std::shared_ptr<bcos::crypto::KeyPairInterface>(
            cryptoSuite->signatureImpl()->generateKeyPair());
        size_t count = 10 * 10000;

        std::string_view connectionString =
            "fiscobcos.rpc.RPCObj@tcp -h 127.0.0.1 -p 20021 -t 60000";
        bcos::sdk::RPCClient rpcClient(std::string{connectionString});

        auto blockNumber = rpcClient.blockNumber().get();
        constexpr long blockLimit = 500;

        auto rand = std::mt19937(std::random_device{}());
        bcostars::protocol::TransactionFactoryImpl transactionFactory(cryptoSuite);
        bcos::bytes deployBin;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(deployBin));
        auto deployTransaction = transactionFactory.createTransaction(0, "", deployBin,
            boost::lexical_cast<std::string>(rand()), blockNumber + blockLimit, "chain0", "group0",
            0, keyPair);
        auto receipt = rpcClient.sendTransaction(*deployTransaction).get();

        if (receipt->status() != 0)
        {
            std::cout << "Deploy contract failed" << receipt->status() << std::endl;
            return 1;
        }

        auto const& contractAddress = receipt->contractAddress();
        std::latch latch(count);
        std::vector<Request> requests(count);
        LatchCompletionQueue completionQueue{latch, requests};

        long elapsed = currentTime();
        std::cout << "Sending transaction..." << std::endl;
        boost::timer::progress_display sendProgess(count);
        tbb::parallel_for(tbb::blocked_range(0LU, count), [&](const auto& range) {
            for (auto it = range.begin(); it != range.end(); ++it)
            {
                bcos::codec::abi::ContractABICodec abiCodec(cryptoSuite->hashImpl());
                auto input = abiCodec.abiIn("setInt(int256)", bcos::s256(it));
                auto setTransaction = transactionFactory.createTransaction(0,
                    std::string(contractAddress), input, boost::lexical_cast<std::string>(rand()),
                    blockNumber + blockLimit, "chain0", "group0", 0, keyPair);

                ++sendProgess;
                auto& request = requests[it];
                request.m_future = rpcClient.sendTransaction(*setTransaction, &completionQueue, it);
                request.m_sendTime = currentTime();
            }
        });
        long sendElapsed = currentTime() - elapsed;

        std::cout << std::endl << "All transaction sended, Waiting for response..." << std::endl;
        latch.wait();
        std::cout << "All response received" << std::endl;
        elapsed = currentTime() - elapsed;

        std::atomic_long allTimeCost = 0;
        std::atomic_int finished = 0;
        std::atomic_int failed = 0;
        tbb::parallel_for(tbb::blocked_range(0LU, count), [&](const auto& range) {
            for (auto it = range.begin(); it != range.end(); ++it)
            {
                try
                {
                    ++finished;
                    auto& request = requests[it];
                    auto receipt = request.m_future.get();
                    allTimeCost += (request.m_receiveTime - request.m_sendTime);
                    if (receipt->status() != 0)
                    {
                        ++failed;
                    }
                }
                catch (std::exception& e)
                {
                    ++failed;
                }
            }
        });

        std::cout << std::endl << "=======================================" << std::endl;
        std::cout << "Total received: " << finished << std::endl;
        std::cout << "Total failed: " << failed << std::endl;
        std::cout << "Receive elapsed: " << elapsed << "ms" << std::endl;
        std::cout << std::endl;
        std::cout << "Avg time cost: " << ((double)allTimeCost.load() / (double)count) << "ms"
                  << std::endl;
        std::cout << "Send TPS: " << ((double)count / (double)sendElapsed) * 1000.0 << std::endl;
        std::cout << "Receive TPS: " << ((double)count / (double)elapsed) * 1000.0 << std::endl;
    }
    catch (std::exception& e)
    {
        std::cout << boost::diagnostic_information(e);
    }


    return 0;
}

int main(int argc, char* argv[])
{
    return performance();
}