#include "TestBytecode.h"
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include <bcos-codec/abi/ContractABICodec.h>
#include <bcos-cpp-sdk/tarsRPC/RPCClient.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <boost/exception/diagnostic_information.hpp>
#include <random>

int main(int argc, char* argv[])
{
    try
    {
        auto hash = std::make_shared<bcos::crypto::Keccak256>();
        auto ecc = std::make_shared<bcos::crypto::Secp256k1Crypto>();
        auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(hash, ecc, nullptr);
        auto keyPair = std::shared_ptr<bcos::crypto::KeyPairInterface>(ecc->generateKeyPair());
        size_t count = 100;

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

        auto contractAddress = receipt->contractAddress();

        bcostars::protocol::TransactionImpl transaction(
            [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
        transaction.mutableInner().data.to = contractAddress;

        bcos::codec::abi::ContractABICodec abiCodec(hash);
        for (auto it : RANGES::views::iota(0LU, count))
        {
            auto input = abiCodec.abiIn("setInt(int256)", bcos::s256(it));
            auto transaction = transactionFactory.createTransaction(0, "", input,
                boost::lexical_cast<std::string>(rand()), blockNumber + blockLimit, "chain0",
                "group0", 0, keyPair);
            auto receipt = rpcClient.sendTransaction(*transaction).get();
        }
    }
    catch (std::exception& e)
    {
        std::cout << boost::diagnostic_information(e);
    }

    return 0;
}