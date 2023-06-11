#include "TestBytecode.h"
#include <bcos-codec/abi/ContractABICodec.h>
#include <bcos-cpp-sdk/tarsRPC/RPCClient.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>

int main(int argc, char* argv[])
{
    auto hash = std::make_shared<bcos::crypto::Keccak256>();
    size_t count = 1000;

    std::string_view connectionString = "fiscobcos.rpc.RPCObj@tcp -h 127.0.0.1 -p 20021 -t 60000";
    bcos::sdk::RPCClient rpcClient(std::string{connectionString});

    auto blockNumber = rpcClient.blockNumber().get();
    constexpr long blockLimit = 500;
    long nonce = 0;

    // Deploy contract
    bcostars::protocol::TransactionImpl deployTransaction(
        [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
    auto& inner = deployTransaction.mutableInner();
    inner.data.blockLimit = blockNumber + blockLimit;
    inner.data.chainID = "chain0";
    inner.data.groupID = "group0";
    inner.data.nonce = boost::lexical_cast<std::string>(++nonce);
    deployTransaction.calculateHash(hash->hasher());
    boost::algorithm::unhex(
        helloworldBytecode, std::back_inserter(deployTransaction.mutableInner().data.input));
    auto receipt = rpcClient.sendTransaction(deployTransaction).get();

    if (receipt->status() != 0)
    {
        std::cout << "Deploy contract failed" << receipt->status() << std::endl;
        return 1;
    }
    // auto contractAddress = receipt->contractAddress();

    // Send transaction
    // bcostars::protocol::TransactionImpl transaction(
    //     [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
    // transaction.mutableInner().data.to = contractAddress;

    // bcos::codec::abi::ContractABICodec abiCodec(hash);

    // int contextID = 0;
    // for (auto const& it : RANGES::views::iota(0LU, count))
    // {
    //     auto input = abiCodec.abiIn("setInt(int256)", bcos::s256(contextID));
    //     transaction.mutableInner().data.input.assign(input.begin(), input.end());

    //     ++contextID;
    //     [[maybe_unused]] auto receipt =
    //         co_await fixture.executor.execute(fixture.blockHeader, transaction, contextID);
    // }

    return 0;
}