#include "bcos-cpp-sdk/tarsRPC/RPCClient.h"

int main(int argc, char* argv[])
{
    std::string_view connectionString;
    bcos::sdk::RPCClient rpcClient(std::string{connectionString});

    // Deploy contract

    // rpcClient.sendTransaction(const bcos::protocol::Transaction &transaction)

    return 0;
}