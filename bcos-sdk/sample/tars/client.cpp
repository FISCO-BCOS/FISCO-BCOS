#include "TestBytecode.h"
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include <bcos-codec/abi/ContractABICodec.h>
#include <bcos-cpp-sdk/tarsRPC/RPCClient.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-task/Coroutine.h>
#include <oneapi/tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/task_group.h>
#include <boost/exception/diagnostic_information.hpp>
#include <atomic>
#include <exception>
#include <latch>
#include <random>

class TBBCompletionQueue : public bcos::sdk::CompletionQueue
{
private:
    tbb::task_group m_taskGroup;

public:
    ~TBBCompletionQueue() noexcept override = default;
    void notify(std::any tag) override
    {
        m_taskGroup.run([m_tag = std::move(tag)]() {
            try
            {
                auto handle = std::any_cast<CO_STD::coroutine_handle<>>(m_tag);
                handle.resume();
            }
            catch (std::exception& e)
            {
                std::cout << boost::diagnostic_information(e);
            }
        });
    }
};

int main(int argc, char* argv[])
{
    try
    {
        auto hash = std::make_shared<bcos::crypto::Keccak256>();
        auto ecc = std::make_shared<bcos::crypto::Secp256k1Crypto>();
        auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(hash, ecc, nullptr);
        auto keyPair = std::shared_ptr<bcos::crypto::KeyPairInterface>(ecc->generateKeyPair());
        size_t count = 10 * 10000;

        std::string_view connectionString =
            "fiscobcos.rpc.RPCObj@tcp -h 127.0.0.1 -p 20021 -t 60000";
        bcos::sdk::RPCClient rpcClient(std::string{connectionString});

        auto blockNumber = rpcClient.blockNumber().get();
        constexpr long blockLimit = 500;

        auto rand = std::mt19937(std::random_device()());
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

        std::latch latch(count);
        TBBCompletionQueue queue;
        auto const& contractAddress = receipt->contractAddress();

        std::atomic_int sended = 0;
        tbb::parallel_for(tbb::blocked_range(0LU, count), [&](const auto& range) {
            for (auto it = range.begin(); it != range.end(); ++it)
            {
                bcos::codec::abi::ContractABICodec abiCodec(hash);
                auto input = abiCodec.abiIn("setInt(int256)", bcos::s256(it));
                auto setTransaction = transactionFactory.createTransaction(0,
                    std::string(contractAddress), input, boost::lexical_cast<std::string>(rand()),
                    blockNumber + blockLimit, "chain0", "group0", 0, keyPair);
                futures[it] = std::move(
                    rpcClient.sendTransaction(*setTransaction, std::addressof(queue), it));

                ++sended;
            }
        });

        std::cout << "Sended: " << sended << std::endl;
        latch.wait();
    }
    catch (std::exception& e)
    {
        std::cout << boost::diagnostic_information(e);
    }

    return 0;
}