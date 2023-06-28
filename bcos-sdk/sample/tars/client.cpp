#include "TestBytecode.h"
#include "bcos-cpp-sdk/tarsRPC/RPCClient.h"
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-task/Wait.h"
#include <bcos-codec/abi/ContractABICodec.h>
#include <bcos-cpp-sdk/tarsRPC/CoRPCClient.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-task/Coroutine.h>
#include <bcos-task/TBBScheduler.h>
#include <bcos-task/TBBWait.h>
#include <bcos-task/Task.h>
#include <oneapi/tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/timer/progress_display.hpp>
#include <atomic>
#include <exception>
#include <latch>
#include <random>

struct LatchCompletionQueue : public bcos::sdk::CompletionQueue
{
    std::latch& m_latch;

    LatchCompletionQueue(std::latch& latch) : m_latch(latch) {}
    void notify(std::any tag) override { m_latch.count_down(); }
};

int performance()
{
    try
    {
        oneapi::tbb::task_group taskGroup;
        bcos::task::tbb::TBBScheduler scheduler(taskGroup);

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
        boost::timer::progress_display sendProgess(count);

        std::latch latch(count);
        std::vector<bcos::sdk::Future<bcos::protocol::TransactionReceipt::Ptr>> futures(count);
        LatchCompletionQueue completionQueue{latch};
        std::atomic_int finished = 0;
        std::atomic_int failed = 0;
        tbb::parallel_for(tbb::blocked_range(0LU, count), [&](const auto& range) {
            for (auto it = range.begin(); it != range.end(); ++it)
            {
                try
                {
                    bcos::codec::abi::ContractABICodec abiCodec(cryptoSuite->hashImpl());
                    auto input = abiCodec.abiIn("setInt(int256)", bcos::s256(it));
                    auto setTransaction =
                        transactionFactory.createTransaction(0, std::string(contractAddress), input,
                            boost::lexical_cast<std::string>(rand()), blockNumber + blockLimit,
                            "chain0", "group0", 0, keyPair);

                    ++sendProgess;
                    futures[it] = rpcClient.sendTransaction(*setTransaction, &completionQueue);
                }
                catch (std::exception& e)
                {
                    ++failed;
                }
            }
        });

        std::cout << "Waiting for response..." << std::endl;
        latch.wait();
        std::cout << "All response received" << std::endl;
        for (auto& future : futures)
        {
            try
            {
                ++finished;
                auto receipt = future.get();
                if (receipt->status() != 0)
                {
                    std::cout << "Receipt error! " << receipt->status() << std::endl;
                    ++failed;
                }
            }
            catch (std::exception& e)
            {
                ++failed;
            }
        }
        std::cout << "Total received: " << finished << ", failed: " << failed << std::endl;
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