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
 * @file deploy_hello.cpp
 * @author: octopus
 * @date 2022-01-16
 */

#include <bcos-tars-protocol/impl/TarsSerializable.h>

#include "bcos-concepts/Serialize.h"
#include "bcos-crypto/interfaces/crypto/KeyPairFactory.h"
#include "bcos-crypto/interfaces/crypto/KeyPairInterface.h"
#include "bcos-crypto/interfaces/crypto/Signature.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h"
#include "bcos-crypto/signature/sm2/SM2Crypto.h"
#include "bcos-crypto/signature/sm2/SM2KeyPairFactory.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/tars/Transaction.h"
#include <bcos-cpp-sdk/SdkFactory.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-utilities/Common.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string/compare.hpp>
#include <cstdlib>
#include <iterator>
#include <memory>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::boostssl;
//----------------------------------------------------------------------------------------------------
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
constexpr static std::string_view hwBIN =
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

constexpr static std::string_view hwSmBIN =
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

//-------------------------------------------------------------------------------------------------

std::string_view getBinary(bool _sm)
{
    return _sm ? hwSmBIN : hwBIN;
}

void usage()
{
    std::cerr << "Desc: deploy HelloWorld contract\n";
    std::cerr << "Usage: deploy_hello <config> <groupID>\n"
              << "Example:\n"
              << "    ./deploy_hello ./config_sample.ini group0\n"
                 "\n";
    std::exit(0);
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        usage();
    }

    std::string config = argv[1];
    std::string group = argv[2];

    std::cout << LOG_DESC(" [DeployHello] params ===>>>> ") << LOG_KV("\n\t # config", config)
              << LOG_KV("\n\t # groupID", group) << std::endl;

    auto factory = std::make_shared<SdkFactory>();
    // construct cpp-sdk object
    auto sdk = factory->buildSdk(config);
    // start sdk
    sdk->start();

    std::cout << LOG_DESC(" [DeployHello] start sdk ... ") << std::endl;

    // get group info
    bcos::group::GroupInfo::Ptr groupInfo = sdk->service()->getGroupInfo(group);
    if (!groupInfo)
    {
        std::cout << LOG_DESC(" [DeployHello] group not exist") << LOG_KV("group", group)
                  << std::endl;
        exit(-1);
    }

    crypto::SignatureCrypto::Ptr keyPairFactory;
    crypto::KeyPairInterface::UniquePtr keyPair;
    if (groupInfo->smCryptoType())
    {
        keyPairFactory = std::make_shared<bcos::crypto::SM2Crypto>();
        keyPair = keyPairFactory->generateKeyPair();
    }
    else
    {
        keyPairFactory = std::make_shared<bcos::crypto::Secp256k1Crypto>();
        keyPair = keyPairFactory->generateKeyPair();
    }

    std::cout << LOG_DESC(" [DeployHello] sm_crypto_type ") << groupInfo->smCryptoType()
              << std::endl;

    std::cout << LOG_DESC(" [DeployHello] new account ")
              << LOG_KV("address", keyPair->publicKey()->hex()) << std::endl;

    int64_t blockLimit = -1;
    sdk->service()->getBlockLimit(group, blockLimit);

    std::cout << LOG_DESC(" [DeployHello] block limit ") << LOG_KV("blockLimit", blockLimit)
              << std::endl;

    auto hexBin = getBinary(groupInfo->smCryptoType());

    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    bcos::crypto::CryptoSuite::Ptr cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, keyPairFactory, nullptr);
    auto transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    bcos::bytes inputData;
    boost::algorithm::unhex(hexBin.begin(), hexBin.end(), std::back_inserter(inputData));
    auto tx = transactionFactory->createTransaction(0, "to", inputData, bcos::u256(100), 200,
        "chain0", group, 1112, std::shared_ptr<bcos::crypto::KeyPairInterface>(std::move(keyPair)));
    // auto r =
    //     transactionBuilderService->createSignedTransaction(*keyPair, "", *binBytes.get(), "", 0);

    std::cout << LOG_DESC(" [DeployHello] create signed transaction success")
              << LOG_KV("tx hash", tx->hash(false)) << std::endl;

    std::promise<bool> p;
    auto f = p.get_future();
    std::string buffer;
    bcos::concepts::serialize::encode(
        std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx)->inner(), buffer);

    auto hexBuffer = boost::algorithm::hex_lower(buffer);

    sdk->jsonRpc()->sendTransaction(group, "", hexBuffer, false,
        [&p](bcos::Error::Ptr _error, std::shared_ptr<bcos::bytes> _resp) {
            if (_error && _error->errorCode() != 0)
            {
                std::cout << LOG_DESC(" [DeployHello] send transaction response error")
                          << LOG_KV("errorCode", _error->errorCode())
                          << LOG_KV("errorMessage", _error->errorMessage()) << std::endl;
            }
            else
            {
                std::string receipt = std::string(_resp->begin(), _resp->end());
                std::cout << LOG_DESC(" [DeployHello] recv response success ")
                          << LOG_KV("transaction receipt", receipt) << std::endl;

                Json::Value root;
                Json::Reader jsonReader;

                try
                {
                    if (!jsonReader.parse(receipt, root))
                    {
                        std::cout << LOG_DESC(" [DeployHello] [ERROR] recv invalid json object")
                                  << LOG_KV("resp", receipt) << std::endl;
                        return;
                    }

                    std::cout << LOG_DESC(" [DeployHello] contract address ==> " +
                                          root["result"]["contractAddress"].asString())
                              << std::endl;
                }
                catch (const std::exception& _e)
                {
                    std::cout << LOG_DESC(" [DeployHello] [ERROR] recv invalid json object")
                              << LOG_KV("resp", receipt) << std::endl;
                }
            }
            p.set_value(true);
        });
    f.get();

    return 0;
}