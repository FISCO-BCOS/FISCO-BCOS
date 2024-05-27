#include "Common.h"
#include "bcos-cpp-sdk/tarsRPC/RPCClient.h"
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-task/TBBWait.h"
#include "bcos-task/Wait.h"
#include <bcos-codec/abi/ContractABICodec.h>
#include <bcos-cpp-sdk/tarsRPC/CoRPCClient.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <oneapi/tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/thread/latch.hpp>
#include <atomic>
#include <exception>
#include <random>

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

        bcos::sdk::Config config = {
            .connectionString = "fiscobcos.rpc.RPCObj@tcp -h 127.0.0.1 -p 20021 -t 60000",
            .sendQueueSize = 100000,
            .timeoutMs = 600000,
        };
        bcos::sdk::RPCClient rpcClient(config);

        auto blockNumber = bcos::sdk::BlockNumber(rpcClient).send().get();
        constexpr static long blockLimit = 500;

        bcostars::protocol::TransactionFactoryImpl transactionFactory(cryptoSuite);
        bcos::bytes deployBin = bcos::sample::getContractBin();
        auto deployTransaction = transactionFactory.createTransaction(0, "", deployBin,
            boost::lexical_cast<std::string>(rand()), blockNumber + blockLimit, "chain0", "group0",
            0, *keyPair);
        auto receipt = bcos::sdk::SendTransaction(rpcClient).send(*deployTransaction).get();

        if (receipt->status() != 0)
        {
            std::cout << "Deploy contract failed" << receipt->status() << std::endl;
            return 1;
        }

        auto const& contractAddress = receipt->contractAddress();

        long elapsed = bcos::sample::currentTime();
        std::atomic_long allTimeCost = 0;
        std::atomic_int finished = 0;
        std::atomic_int failed = 0;
        std::cout << "Sending transaction..." << std::endl;

        boost::latch latch(count);
        tbb::parallel_for(tbb::blocked_range(0LU, count), [&](const auto& range) {
            auto rand = std::mt19937(std::random_device{}());
            for (auto it = range.begin(); it != range.end(); ++it)
            {
                bcos::codec::abi::ContractABICodec abiCodec(*cryptoSuite->hashImpl());
                auto input = abiCodec.abiIn("setInt(int256)", bcos::s256(it));
                auto setTransaction = transactionFactory.createTransaction(0,
                    std::string(contractAddress), input, boost::lexical_cast<std::string>(rand()),
                    blockNumber + blockLimit, "chain0", "group0", 0, *keyPair);

                ++finished;
                long startTime = bcos::sample::currentTime();
                try
                {
                    auto handle = bcos::task::tbb::syncWait(
                        bcos::sdk::async::sendTransaction(rpcClient, *setTransaction));
                    auto receipt = handle.get();
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
                allTimeCost += (bcos::sample::currentTime() - startTime);
            }
        });
        long sendElapsed = bcos::sample::currentTime() - elapsed;

        latch.wait();
        elapsed = bcos::sample::currentTime() - elapsed;

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