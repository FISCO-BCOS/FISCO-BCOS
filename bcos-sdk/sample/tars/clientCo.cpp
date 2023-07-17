#include "TestBytecode.h"
#include "bcos-cpp-sdk/tarsRPC/RPCClient.h"
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-task/Wait.h"
#include <bcos-codec/abi/ContractABICodec.h>
#include <bcos-cpp-sdk/tarsRPC/CoRPCClient.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-task/TBBScheduler.h>
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

int performance()
{
    try
    {
        auto cryptoSuite =
            std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
                std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
        auto keyPair = std::shared_ptr<bcos::crypto::KeyPairInterface>(
            cryptoSuite->signatureImpl()->generateKeyPair());
        constexpr static size_t count = 10UL * 10000;

        constexpr static std::string_view connectionString =
            "fiscobcos.rpc.RPCObj@tcp -h 127.0.0.1 -p 20021 -t 60000";
        bcos::sdk::RPCClient rpcClient(std::string{connectionString});

        auto blockNumber = rpcClient.blockNumber().get();
        constexpr static long blockLimit = 500;

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

        long elapsed = currentTime();
        std::atomic_long allTimeCost = 0;
        std::atomic_int finished = 0;
        std::atomic_int failed = 0;
        std::cout << "Sending transaction..." << std::endl;
        boost::timer::progress_display sendProgess(count);

        bcos::sdk::CoRPCClient coRPCClient(rpcClient);
        tbb::task_group taskGroup;
        bcos::task::tbb::TBBScheduler tbbScheduler(taskGroup);

        std::latch latch(count);
        tbb::parallel_for(tbb::blocked_range(0LU, count), [&](const auto& range) {
            auto rand = std::mt19937(std::random_device{}());
            for (auto it = range.begin(); it != range.end(); ++it)
            {
                bcos::codec::abi::ContractABICodec abiCodec(cryptoSuite->hashImpl());
                auto input = abiCodec.abiIn("setInt(int256)", bcos::s256(it));
                auto setTransaction = transactionFactory.createTransaction(0,
                    std::string(contractAddress), input, boost::lexical_cast<std::string>(rand()),
                    blockNumber + blockLimit, "chain0", "group0", 0, keyPair);

                ++sendProgess;
                ++finished;
                bcos::task::wait(
                    [](decltype(setTransaction) transaction, decltype(coRPCClient)& coRPCClient,
                        decltype(failed)& failed, decltype(allTimeCost)& allTimeCost,
                        decltype(tbbScheduler)& tbbScheduler,
                        decltype(latch)& latch) -> bcos::task::Task<void> {
                        long startTime = currentTime();
                        try
                        {
                            auto future = co_await coRPCClient.sendTransaction(*transaction);

                            co_await tbbScheduler;
                            auto receipt = future.get();
                            if (receipt->status() != 0)
                            {
                                ++failed;
                            }
                        }
                        catch (std::exception& e)
                        {
                            ++failed;
                        }
                        latch.count_down();
                        allTimeCost += (currentTime() - startTime);
                    }(std::move(setTransaction), coRPCClient, failed, allTimeCost, tbbScheduler,
                                                    latch));
            }
        });
        long sendElapsed = currentTime() - elapsed;

        latch.wait();
        taskGroup.wait();
        elapsed = currentTime() - elapsed;

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