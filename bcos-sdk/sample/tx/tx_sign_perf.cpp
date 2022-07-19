/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file tx_sign_perf.cpp
 * @author: octopus
 * @date 2022-01-17
 */

#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

// HelloWorld Source Code:

// HelloWorld Source Code:
/**
pragma solidity>=0.4.24 <0.6.11;

contract HelloWorld {
    string name;

    constructor() public {
        name = "Hello, World!";
    }

    function get() public view returns (string memory) {
        return name;
    }

    function set(string memory n) public {
        name = n;
    }
}
*/
const char* hwBIN =
    "608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c20576f72"
    "6c6421000000000000000000000000000000000000008152506000908051906020019061005c929190610062565b50"
    "610107565b828054600181600116156101000203166002900490600052602060002090601f01602090048101928260"
    "1f106100a357805160ff19168380011785556100d1565b828001600101855582156100d1579182015b828111156100"
    "d05782518255916020019190600101906100b5565b5b5090506100de91906100e2565b5090565b61010491905b8082"
    "11156101005760008160009055506001016100e8565b5090565b90565b610310806101166000396000f3fe60806040"
    "5234801561001057600080fd5b50600436106100365760003560e01c80634ed3885e1461003b5780636d4ce63c1461"
    "00f6575b600080fd5b6100f46004803603602081101561005157600080fd5b81019080803590602001906401000000"
    "0081111561006e57600080fd5b82018360208201111561008057600080fd5b80359060200191846001830284011164"
    "0100000000831117156100a257600080fd5b91908080601f0160208091040260200160405190810160405280939291"
    "90818152602001838380828437600081840152601f19601f8201169050808301925050505050505091929192905050"
    "50610179565b005b6100fe610193565b60405180806020018281038252838181518152602001915080519060200190"
    "80838360005b8381101561013e578082015181840152602081019050610123565b50505050905090810190601f1680"
    "1561016b5780820380516001836020036101000a031916815260200191505b509250505060405180910390f35b8060"
    "00908051906020019061018f929190610235565b5050565b6060600080546001816001161561010002031660029004"
    "80601f0160208091040260200160405190810160405280929190818152602001828054600181600116156101000203"
    "1660029004801561022b5780601f106102005761010080835404028352916020019161022b565b8201919060005260"
    "20600020905b81548152906001019060200180831161020e57829003601f168201915b5050505050905090565b8280"
    "54600181600116156101000203166002900490600052602060002090601f016020900481019282601f106102765780"
    "5160ff19168380011785556102a4565b828001600101855582156102a4579182015b828111156102a3578251825591"
    "602001919060010190610288565b5b5090506102b191906102b5565b5090565b6102d791905b808211156102d35760"
    "008160009055506001016102bb565b5090565b9056fea2646970667358221220b5943f43c48cc93c6d71cdcf27aee5"
    "072566c88755ce9186e32ce83b24e8dc6c64736f6c634300060a0033";

const char* hwSmBIN =
    "608060405234801561001057600080fd5b506040518060400160405280600d81526020017f48656c6c6f2c20576f72"
    "6c6421000000000000000000000000000000000000008152506000908051906020019061005c929190610062565b50"
    "610107565b828054600181600116156101000203166002900490600052602060002090601f01602090048101928260"
    "1f106100a357805160ff19168380011785556100d1565b828001600101855582156100d1579182015b828111156100"
    "d05782518255916020019190600101906100b5565b5b5090506100de91906100e2565b5090565b61010491905b8082"
    "11156101005760008160009055506001016100e8565b5090565b90565b610310806101166000396000f3fe60806040"
    "5234801561001057600080fd5b50600436106100365760003560e01c8063299f7f9d1461003b5780633590b49f1461"
    "00be575b600080fd5b610043610179565b604051808060200182810382528381815181526020019150805190602001"
    "9080838360005b83811015610083578082015181840152602081019050610068565b50505050905090810190601f16"
    "80156100b05780820380516001836020036101000a031916815260200191505b509250505060405180910390f35b61"
    "0177600480360360208110156100d457600080fd5b81019080803590602001906401000000008111156100f1576000"
    "80fd5b82018360208201111561010357600080fd5b8035906020019184600183028401116401000000008311171561"
    "012557600080fd5b91908080601f016020809104026020016040519081016040528093929190818152602001838380"
    "828437600081840152601f19601f82011690508083019250505050505050919291929050505061021b565b005b6060"
    "60008054600181600116156101000203166002900480601f0160208091040260200160405190810160405280929190"
    "818152602001828054600181600116156101000203166002900480156102115780601f106101e65761010080835404"
    "0283529160200191610211565b820191906000526020600020905b8154815290600101906020018083116101f45782"
    "9003601f168201915b5050505050905090565b8060009080519060200190610231929190610235565b5050565b8280"
    "54600181600116156101000203166002900490600052602060002090601f016020900481019282601f106102765780"
    "5160ff19168380011785556102a4565b828001600101855582156102a4579182015b828111156102a3578251825591"
    "602001919060010190610288565b5b5090506102b191906102b5565b5090565b6102d791905b808211156102d35760"
    "008160009055506001016102bb565b5090565b9056fea26469706673582212209871cb2bcf390d53645807cbaedfe0"
    "52d739ef9cff9d84787f74c4f379e1854664736f6c634300060a0033";

/*
{
    "6d4ce63c": "get()",
    "4ed3885e": "set(string)"
}

{
    "299f7f9d": "get()",
    "3590b49f": "set(string)"
}
*/

const char* getBinary(int _sm)
{
    return _sm ? hwSmBIN : hwBIN;
}

void usage()
{
    printf("Desc: create signed transaction[HelloWorld set] perf test\n");
    printf("Usage: tx_sign_perf isSM txCount\n");
    printf("Example:\n");
    printf("    ./tx_sign_perf true 30000\n");
    printf("    ./tx_sign_perf false 30000\n");
    exit(0);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        usage();
    }

    bool smCrypto = (std::string(argv[1]) == "true");
    uint32_t txCount = std::stoul(argv[2]);

    printf("[Create Signed Tx Perf Test] ===>>>> smCrypto: %d, txCount: %u\n", smCrypto, txCount);

    // auto keyPairBuilder = std::make_shared<bcos::cppsdk::utilities::KeyPairBuilder>();
    // auto keyPair =
    //     keyPairBuilder->genKeyPair(smCrypto ? bcos::cppsdk::utilities::CryptoType::SM2 :
    //                                           bcos::cppsdk::utilities::CryptoType::Secp256K1);

    // auto transactionBuilder = std::make_shared<bcos::cppsdk::utilities::TransactionBuilder>();
    // auto code = *bcos::fromHexString(getBinary(smCrypto ? 1 : 0));

    int64_t block_limit = 111111;
    const char* group_id = "group0";
    const char* chain_id = "chain0";

    std::string txHash = "";
    uint32_t i = 0;
    uint32_t _10Per = txCount / 10;
    auto keyPairFactory = std::make_shared<bcos::crypto::Secp256k1Crypto>();

    auto startPoint = std::chrono::high_resolution_clock::now();
    while (i++ < txCount)
    {
        if (i % _10Per == 0)
        {
            std::cerr << " ..process : " << ((double)i / txCount) * 100 << "%" << std::endl;
        }

        auto keyPair = keyPairFactory->generateKeyPair();
        auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
        bcos::crypto::CryptoSuite::Ptr cryptoSuite =
            std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, keyPairFactory, nullptr);

        auto transactionFactory =
            std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
        bcos::bytes inputData;
        auto tx =
            transactionFactory->createTransaction(0, "to", inputData, bcos::u256(100), 200, "chain",
                "group", 1112, std::shared_ptr<bcos::crypto::KeyPairInterface>(std::move(keyPair)));
    }

    auto endPoint = std::chrono::high_resolution_clock::now();
    auto elapsedMS =
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(endPoint - startPoint)
            .count();
    auto elapsedUS =
        (long long)std::chrono::duration_cast<std::chrono::microseconds>(endPoint - startPoint)
            .count();

    printf(
        " [Create Signed Tx Perf Test] total txs: %u, total elapsed(ms): %lld, avg(us): %lld, "
        "txs/s: %lld \n",
        txCount, elapsedMS, elapsedUS / txCount, 1000 * txCount / elapsedMS);

    return 0;
}
