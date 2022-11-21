/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file EVMStateContextTest.cpp
 * @author: kyonGuo
 * @date 2022/11/2
 */
#include "bcos-crypto/signature/codec/SignatureDataWithPub.h"
#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/CryptoPrecompiled.h"
#include <bcos-crypto/signature/codec/SignatureDataWithV.h>
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;
using namespace bcos::crypto;
using namespace bcos::codec;

namespace bcos::test
{
class EVMStateContextFixture : public PrecompiledFixture
{
public:
    EVMStateContextFixture()
    {
        codec = std::make_shared<CodecWrapper>(hashImpl, false);
        setIsWasm(false, false, true);
        testAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2").hex();
        origin = Address("0x1234567890123456789012345678901234567890").hex();
    }

    virtual ~EVMStateContextFixture() {}

    void deployTest()
    {
        bytes input;
        boost::algorithm::unhex(testBin, std::back_inserter(input));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));

        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params = std::make_unique<NativeExecutionMessage>();
        params->setContextID(99);
        params->setSeq(1000);
        params->setDepth(0);

        params->setOrigin(sender);
        params->setFrom(sender);

        // toChecksumAddress(addressString, hashImpl);
        params->setTo(testAddress);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        NativeExecutionMessage paramsBak = *params;
        nextBlock(1);
        // --------------------------------
        // Create contract
        // --------------------------------

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
        executor->dmcExecuteTransaction(
            std::move(params), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });

        auto result = executePromise.get_future().get();
        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result->contextID(), 99);
        BOOST_CHECK_EQUAL(result->seq(), 1000);
        BOOST_CHECK_EQUAL(result->create(), false);
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), testAddress);
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), testAddress);
        BOOST_CHECK(result->to() == sender);
        BOOST_CHECK_LT(result->gasAvailable(), gas);
        commitBlock(1);
    }

    ExecutionMessage::UniquePtr callTest(
        protocol::BlockNumber _number, bytesConstRef encodedData, std::string _origin = "")
    {
        nextBlock(_number);
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", encodedData.toBytes(), 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        if (!_origin.empty())
        {
            tx->forceSender(Address(_origin).asBytes());
        }
        auto txHash = tx->hash();
        txpool->hash2Transaction.emplace(txHash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(txHash);
        params2->setContextID(_number);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(testAddress);
        params2->setOrigin(_origin.empty() ? sender : _origin);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(encodedData.toBytes());
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        commitBlock(_number);
        return result2;
    }

    std::string sender;
    std::string origin;
    std::string testAddress;
    // clang-format off
    std::string testBin = "608060405234801561001057600080fd5b50611b70806100206000396000f3fe608060405234801561001057600080fd5b50600436106101215760003560e01c8063b2acd509116100ad578063cbbab99c11610071578063cbbab99c14610914578063da1ce32314610997578063ec8b466a146109b5578063ff0732ec14610a33578063fffd016714610b0257610121565b8063b2acd50914610596578063b815849414610665578063c13be6ea146106bb578063c3e6b0181461078a578063caa26032146107e057610121565b806329470338116100f457806329470338146103e25780634849f27914610424578063653d710c146104ac5780636a4dc7f3146104f657806370a3872f1461054057610121565b8063086ecbfa146101265780630c6daa6e146101f55780631d1f490d146103355780631ecf4f2114610353575b600080fd5b6101df6004803603602081101561013c57600080fd5b810190808035906020019064010000000081111561015957600080fd5b82018360208201111561016b57600080fd5b8035906020019184600183028401116401000000008311171561018d57600080fd5b91908080601f016020809104026020016040519081016040528093929190818152602001838380828437600081840152601f19601f820116905080830192505050505050509192919290505050610b20565b6040518082815260200191505060405180910390f35b6102f7600480360361014081101561020c57600080fd5b81019080803563ffffffff16906020019092919080604001906002806020026040519081016040528092919082600260200280828437600081840152601f19601f820116905080830192505050505050919291929080608001906004806020026040519081016040528092919082600460200280828437600081840152601f19601f820116905080830192505050505050919291929080604001906002806020026040519081016040528092919082600260200280828437600081840152601f19601f8201169050808301925050505050509192919290803515159060200190929190505050610c8a565b6040518082600260200280838360005b83811015610322578082015181840152602081019050610307565b5050505090500191505060405180910390f35b61033d610efa565b6040518082815260200191505060405180910390f35b6103a06004803603608081101561036957600080fd5b8101908080359060200190929190803560ff1690602001909291908035906020019092919080359060200190929190505050610f02565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b61040e600480360360208110156103f857600080fd5b8101908080359060200190929190505050611045565b6040518082815260200191505060405180910390f35b61046e6004803603608081101561043a57600080fd5b8101908080359060200190929190803590602001909291908035906020019092919080359060200190929190505050611050565b6040518082600260200280838360005b8381101561049957808201518184015260208101905061047e565b5050505090500191505060405180910390f35b6104b46111b6565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b6104fe6111be565b604051808273ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b6105806004803603606081101561055657600080fd5b810190808035906020019092919080359060200190929190803590602001909291905050506111c6565b6040518082815260200191505060405180910390f35b61064f600480360360208110156105ac57600080fd5b81019080803590602001906401000000008111156105c957600080fd5b8201836020820111156105db57600080fd5b803590602001918460018302840111640100000000831117156105fd57600080fd5b91908080601f016020809104026020016040519081016040528093929190818152602001838380828437600081840152601f19601f82011690508083019250505050505050919291929050505061127a565b6040518082815260200191505060405180910390f35b6106a56004803603606081101561067b57600080fd5b8101908080359060200190929190803590602001909291908035906020019092919050505061138d565b6040518082815260200191505060405180910390f35b610774600480360360208110156106d157600080fd5b81019080803590602001906401000000008111156106ee57600080fd5b82018360208201111561070057600080fd5b8035906020019184600183028401116401000000008311171561072257600080fd5b91908080601f016020809104026020016040519081016040528093929190818152602001838380828437600081840152601f19601f820116905080830192505050505050509192919290505050611478565b6040518082815260200191505060405180910390f35b6107ca600480360360608110156107a057600080fd5b810190808035906020019092919080359060200190929190803590602001909291905050506115ee565b6040518082815260200191505060405180910390f35b610899600480360360208110156107f657600080fd5b810190808035906020019064010000000081111561081357600080fd5b82018360208201111561082557600080fd5b8035906020019184600183028401116401000000008311171561084757600080fd5b91908080601f016020809104026020016040519081016040528093929190818152602001838380828437600081840152601f19601f820116905080830192505050505050509192919290505050611706565b6040518080602001828103825283818151815260200191508051906020019080838360005b838110156108d95780820151818401526020810190506108be565b50505050905090810190601f1680156109065780820380516001836020036101000a031916815260200191505b509250505060405180910390f35b61091c611849565b6040518080602001828103825283818151815260200191508051906020019080838360005b8381101561095c578082015181840152602081019050610941565b50505050905090810190601f1680156109895780820380516001836020036101000a031916815260200191505b509250505060405180910390f35b61099f611896565b6040518082815260200191505060405180910390f35b6109f5600480360360608110156109cb57600080fd5b8101908080359060200190929190803590602001909291908035906020019092919050505061189e565b6040518082600260200280838360005b83811015610a20578082015181840152602081019050610a05565b5050505090500191505060405180910390f35b610aec60048036036020811015610a4957600080fd5b8101908080359060200190640100000000811115610a6657600080fd5b820183602082011115610a7857600080fd5b80359060200191846001830284011164010000000083111715610a9a57600080fd5b91908080601f016020809104026020016040519081016040528093929190818152602001838380828437600081840152601f19601f8201169050808301925050505050505091929192905050506119ed565b6040518082815260200191505060405180910390f35b610b0a611acc565b6040518082815260200191505060405180910390f35b6000805a90506003836040518082805190602001908083835b60208310610b5c5780518252602082019150602081019050602083039250610b39565b6001836020036101000a038019825116818451168082178552505050505050905001915050602060405180830381855afa158015610b9e573d6000803e3d6000fd5b5050506040515160601b6bffffffffffffffffffffffff1916915060005a9050600081830390507fdfa34a4e767d8f6cbaddb2435b6adb60a8c66377dc9885145c3c4ccfaf58d6d2816040518082815260200191505060405180910390a16127108111158015610c1057506102588110155b610c82576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260138152602001807f6761736c65667420636865636b206572726f720000000000000000000000000081525060200191505060405180910390fd5b505050919050565b610c92611ad4565b610c9a611ad4565b60608787600060028110610caa57fe5b602002015188600160028110610cbc57fe5b602002015188600060048110610cce57fe5b602002015189600160048110610ce057fe5b60200201518a600260048110610cf257fe5b60200201518b600360048110610d0457fe5b60200201518b600060028110610d1657fe5b60200201518c600160028110610d2857fe5b60200201518c604051602001808b63ffffffff1663ffffffff1660e01b81526004018a81526020018981526020018881526020018781526020018681526020018581526020018477ffffffffffffffffffffffffffffffffffffffffffffffff191677ffffffffffffffffffffffffffffffffffffffffffffffff191681526008018377ffffffffffffffffffffffffffffffffffffffffffffffff191677ffffffffffffffffffffffffffffffffffffffffffffffff19168152600801821515151560f81b81526001019a5050505050505050505050604051602081830303815290604052905060005a905060408360d5602085016009600019fa610e2d57600080fd5b60005a9050600081830390507fdfa34a4e767d8f6cbaddb2435b6adb60a8c66377dc9885145c3c4ccfaf58d6d2816040518082815260200191505060405180910390a1620186a0811115610ee9576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260138152602001807f6761736c65667420636865636b206572726f720000000000000000000000000081525060200191505060405180910390fd5b849550505050505095945050505050565b600042905090565b6000805a905060018686868660405160008152602001604052604051808581526020018460ff1660ff1681526020018381526020018281526020019450505050506020604051602081039080840390855afa158015610f65573d6000803e3d6000fd5b50505060206040510351915060005a9050600081830390507fdfa34a4e767d8f6cbaddb2435b6adb60a8c66377dc9885145c3c4ccfaf58d6d2816040518082815260200191505060405180910390a16127108111158015610fc85750610bb88110155b61103a576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260138152602001807f6761736c65667420636865636b206572726f720000000000000000000000000081525060200191505060405180910390fd5b505050949350505050565b600081409050919050565b611058611ad4565b611060611af6565b858160006004811061106e57fe5b602002018181525050848160016004811061108557fe5b602002018181525050838160026004811061109c57fe5b60200201818152505082816003600481106110b357fe5b60200201818152505060005a9050604083608084600060065af180600081146110db576110e0565b600080fd5b505060005a9050600081830390507fdfa34a4e767d8f6cbaddb2435b6adb60a8c66377dc9885145c3c4ccfaf58d6d2816040518082815260200191505060405180910390a16127108111158015611138575060968110155b6111aa576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260138152602001807f6761736c65667420636865636b206572726f720000000000000000000000000081525060200191505060405180910390fd5b50505050949350505050565b600032905090565b600033905090565b6000805a9050600083806111d657fe5b858709905060005a90506000818403905061271081111580156111fa575060008110155b61126c576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260138152602001807f6761736c65667420636865636b206572726f720000000000000000000000000081525060200191505060405180910390fd5b829450505050509392505050565b60008082519050600060c0828161128d57fe5b061461129857600080fd5b60005a90506040516020818460208801600060085af180600081146112c057825195506112c5565b600080fd5b50505060005a9050600081830390507fdfa34a4e767d8f6cbaddb2435b6adb60a8c66377dc9885145c3c4ccfaf58d6d2816040518082815260200191505060405180910390a1620186a0811115611384576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260138152602001807f6761736c65667420636865636b206572726f720000000000000000000000000081525060200191505060405180910390fd5b50505050919050565b6000805a90506000838061139d57fe5b858708905060005a9050600081840390507fdfa34a4e767d8f6cbaddb2435b6adb60a8c66377dc9885145c3c4ccfaf58d6d2816040518082815260200191505060405180910390a161271081111580156113f8575060008110155b61146a576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260138152602001807f6761736c65667420636865636b206572726f720000000000000000000000000081525060200191505060405180910390fd5b829450505050509392505050565b6000805a90506002836040518082805190602001908083835b602083106114b45780518252602082019150602081019050602083039250611491565b6001836020036101000a038019825116818451168082178552505050505050905001915050602060405180830381855afa1580156114f6573d6000803e3d6000fd5b5050506040513d602081101561150b57600080fd5b8101908080519060200190929190505050915060005a9050600081830390507fdfa34a4e767d8f6cbaddb2435b6adb60a8c66377dc9885145c3c4ccfaf58d6d2816040518082815260200191505060405180910390a161271081111580156115745750603c8110155b6115e6576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260138152602001807f6761736c65667420636865636b206572726f720000000000000000000000000081525060200191505060405180910390fd5b505050919050565b6000805a905060405160208152602080820152602060408201528560608201528460808201528360a082015260208160c083600060055af18060008114611638578251945061163d565b600080fd5b50505060005a9050600081830390507fdfa34a4e767d8f6cbaddb2435b6adb60a8c66377dc9885145c3c4ccfaf58d6d2816040518082815260200191505060405180910390a1620186a08111156116fc576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260138152602001807f6761736c65667420636865636b206572726f720000000000000000000000000081525060200191505060405180910390fd5b5050509392505050565b606080825167ffffffffffffffff8111801561172157600080fd5b506040519080825280601f01601f1916602001820160405280156117545781602001600182028036833780820191505090505b50905060005a9050835180602084018260208801600060045af161177457fe5b5060005a9050600081830390507fdfa34a4e767d8f6cbaddb2435b6adb60a8c66377dc9885145c3c4ccfaf58d6d2816040518082815260200191505060405180910390a161271081111580156117cb5750600f8110155b61183d576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260138152602001807f6761736c65667420636865636b206572726f720000000000000000000000000081525060200191505060405180910390fd5b83945050505050919050565b60606000368080601f016020809104026020016040519081016040528093929190818152602001838380828437600081840152601f19601f82011690508083019250505050505050905090565b600045905090565b6118a6611ad4565b6118ae611b18565b84816000600381106118bc57fe5b60200201818152505083816001600381106118d357fe5b60200201818152505082816002600381106118ea57fe5b60200201818152505060005a9050604083606084600060075af1806000811461191257611917565b600080fd5b505060005a9050600081830390507fdfa34a4e767d8f6cbaddb2435b6adb60a8c66377dc9885145c3c4ccfaf58d6d2816040518082815260200191505060405180910390a1612710811115801561197057506117708110155b6119e2576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260138152602001807f6761736c65667420636865636b206572726f720000000000000000000000000081525060200191505060405180910390fd5b505050509392505050565b6000805a90508280519060200120915060005a9050600081830390507fdfa34a4e767d8f6cbaddb2435b6adb60a8c66377dc9885145c3c4ccfaf58d6d2816040518082815260200191505060405180910390a16127108111158015611a525750600081115b611ac4576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004018080602001828103825260138152602001807f6761736c65667420636865636b206572726f720000000000000000000000000081525060200191505060405180910390fd5b505050919050565b600043905090565b6040518060400160405280600290602082028036833780820191505090505090565b6040518060800160405280600490602082028036833780820191505090505090565b604051806060016040528060039060208202803683378082019150509050509056fea2646970667358221220fcd3862acd2cfe62485e2ed162542e61e370b685319c69893f7b168021fbdd0f64736f6c634300060a0033";
    // clang-format on
};

BOOST_FIXTURE_TEST_SUITE(EVMStateContextTest, EVMStateContextFixture)

BOOST_AUTO_TEST_CASE(testEVMPrecompiled)
{
    deployTest();
    BlockNumber number = 2;
    // sha256
    {
        std::string stringData = "abcd";
        bytesConstRef dataRef(stringData);
        bytes encodedData = codec->encodeWithSig("sha256Test(bytes)", dataRef.toBytes());

        auto result = callTest(number++, ref(encodedData));

        string32 re;
        codec->decode(result->data(), re);
        uint64_t gasUsed;
        auto logEntry = result->takeLogEntries();
        codec->decode(logEntry.at(0).data(), gasUsed);
        std::cout << "sha256 gasUsed: " << gasUsed << std::endl;

        BOOST_CHECK_EQUAL("0x88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589",
            toHexStringWithPrefix(fromString32(re).asBytes()));
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }

    // keccak256
    {
        std::string stringData = "test123";
        bytesConstRef dataRef(stringData);
        bytes encodedData = codec->encodeWithSig("keccak256Test(bytes)", dataRef.toBytes());

        auto result = callTest(number++, ref(encodedData));

        string32 re;
        codec->decode(result->data(), re);
        uint64_t gasUsed;
        auto logEntry = result->takeLogEntries();
        codec->decode(logEntry.at(0).data(), gasUsed);
        std::cout << "keccak256 gasUsed: " << gasUsed << std::endl;

        BOOST_CHECK_EQUAL("0xf81b517a242b218999ec8eec0ea6e2ddbef2a367a14e93f4a32a39e260f686ad",
            toHexStringWithPrefix(fromString32(re).asBytes()));
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }

    // ecRecover
    {
        std::string stringData = "test_ecRecover";
        HashType hash = hashImpl->hash(asBytes(stringData));

        h256 fixedSec1("bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd");
        auto sec1 = std::make_shared<KeyImpl>(fixedSec1.asBytes());
        auto keyFactory = std::make_shared<KeyFactoryImpl>();
        auto secCreated = keyFactory->createKey(fixedSec1.asBytes());
        auto keyPair = std::make_shared<Secp256k1KeyPair>(sec1);
        auto sign = secp256k1Sign(*keyPair, hash);
        auto signWithV = std::make_shared<SignatureDataWithV>(ref(*sign));

        auto hash32 = toString32(hash);
        string32 r = toString32(signWithV->r());
        string32 s = toString32(signWithV->s());
        uint8_t v = signWithV->v() + 27;
        auto p = secp256k1Recover(hash, ref(*sign));
        bytes encodedData =
            codec->encodeWithSig("ecRecoverTest(bytes32,uint8,bytes32,bytes32)", hash32, v, r, s);
        auto result = callTest(number++, ref(encodedData));

        Address pub;
        codec->decode(result->data(), pub);
        uint64_t gasUsed;
        auto logEntry = result->takeLogEntries();
        codec->decode(logEntry.at(0).data(), gasUsed);
        std::cout << "ecRecover gasUsed: " << gasUsed << std::endl;

        BOOST_CHECK_EQUAL(keyPair->address(hashImpl), pub);
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }

    // ripemd160Test
    {
        std::string stringData = "test_ripemd160";
        bytesConstRef dataRef(stringData);
        bytes encodedData = codec->encodeWithSig("ripemd160Test(bytes)", dataRef.toBytes());

        auto result = callTest(number++, ref(encodedData));

        string32 re;
        codec->decode(result->data(), re);
        uint64_t gasUsed;
        auto logEntry = result->takeLogEntries();
        codec->decode(logEntry.at(0).data(), gasUsed);
        std::cout << "ripemd160 gasUsed: " << gasUsed << std::endl;
        BOOST_CHECK_EQUAL("0x983fcf500b356d411f44fac0c54b9156bac3a22d000000000000000000000000",
            toHexStringWithPrefix(fromString32(re).asBytes()));
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }

    // identityTest
    {
        std::string stringData = "test_ripemd160";
        bytesConstRef dataRef(stringData);
        bytes encodedData = codec->encodeWithSig("callDatacopy(bytes)", dataRef.toBytes());

        auto result = callTest(number++, ref(encodedData));

        bytes re;
        codec->decode(result->data(), re);
        uint64_t gasUsed;
        auto logEntry = result->takeLogEntries();
        codec->decode(logEntry.at(0).data(), gasUsed);
        std::cout << "identityTest gasUsed: " << gasUsed << std::endl;

        BOOST_CHECK_EQUAL(toHexStringWithPrefix(asBytes(stringData)), toHexStringWithPrefix(re));
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }

    // modexp
    {
        string32 base =
            toString32("0000000000000000000000000000000000000000000000000000000000000003");
        string32 exponent =
            toString32("0000000000000000000000000000000000000000000000000000000000000003");
        string32 modulus =
            toString32("0000000000000000000000000000000000000000000000000000000000000009");
        bytes encodedData =
            codec->encodeWithSig("callBigModExp(bytes32,bytes32,bytes32)", base, exponent, modulus);

        auto result = callTest(number++, ref(encodedData));

        string32 re;
        codec->decode(result->data(), re);
        uint64_t gasUsed;
        auto logEntry = result->takeLogEntries();
        codec->decode(logEntry.at(0).data(), gasUsed);
        std::cout << "modexp gasUsed: " << gasUsed << std::endl;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000000000",
            toHexStringWithPrefix(fromString32(re).asBytes()));
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }

    // alt_bn128_G1_add
    /*
    {
        string32 ax =
            toString32("0000000000000000000000000000000000000000000000000000000000000003");
        string32 ay =
            toString32("0000000000000000000000000000000000000000000000000000000000000003");
        string32 bx =
            toString32("0000000000000000000000000000000000000000000000000000000000000009");
        string32 by =
            toString32("0000000000000000000000000000000000000000000000000000000000000009");
        bytes encodedData =
            codec->encodeWithSig("callBn256Add(bytes32,bytes32,bytes32,bytes32)", ax, ay, bx, by);

        auto result = callTest(number++, ref(encodedData));

        std::vector<string32> re;
        codec->decode(result->data(), re);
        uint64_t gasUsed;
        auto logEntry = result->takeLogEntries();
        codec->decode(logEntry.at(0).data(), gasUsed);
        std::cout << "alt_bn128_G1_add gasUsed: " << gasUsed << std::endl;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000000000",
            toHexStringWithPrefix(fromString32(re[0]).asBytes()));
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000000000",
            toHexStringWithPrefix(fromString32(re[1]).asBytes()));
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }


    // alt_bn128_G1_mul
    {
        string32 x =
            toString32("0000000000000000000000000000000000000000000000000000000000000003");
        string32 y =
            toString32("0000000000000000000000000000000000000000000000000000000000000003");
        string32 scalar =
            toString32("0000000000000000000000000000000000000000000000000000000000000009");
        bytes encodedData =
            codec->encodeWithSig("callBn256ScalarMul(bytes32,bytes32,bytes32)", x, y, scalar);

        auto result = callTest(number++, ref(encodedData));

        std::vector<string32> re;
        codec->decode(result->data(), re);
        uint64_t gasUsed;
        auto logEntry = result->takeLogEntries();
        codec->decode(logEntry.at(0).data(), gasUsed);
        std::cout << "alt_bn128_G1_add gasUsed: " << gasUsed << std::endl;
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000000000",
            toHexStringWithPrefix(fromString32(re[0]).asBytes()));
        BOOST_CHECK_EQUAL("0x0000000000000000000000000000000000000000000000000000000000000000",
            toHexStringWithPrefix(fromString32(re[1]).asBytes()));
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }
     */

    // alt_bn128_pairing_product
    // blake2

    // addmod
    {
        u256 x = 100;
        u256 y = 89;
        u256 k = 99;
        bytes encodedData = codec->encodeWithSig("addmodTest(uint256,uint256,uint256)", x, y, k);

        auto result = callTest(number++, ref(encodedData));

        u256 re;
        codec->decode(result->data(), re);
        uint64_t gasUsed;
        auto logEntry = result->takeLogEntries();
        codec->decode(logEntry.at(0).data(), gasUsed);
        std::cout << "addmod gasUsed: " << gasUsed << std::endl;
        BOOST_CHECK_EQUAL(re, 90);
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }

    // mulmod
    {
        u256 x = 3;
        u256 y = 89;
        u256 k = 99;
        bytes encodedData = codec->encodeWithSig("mulmodTest(uint256,uint256,uint256)", x, y, k);

        auto result = callTest(number++, ref(encodedData));

        u256 re;
        codec->decode(result->data(), re);
        BOOST_CHECK_EQUAL(re, 69);
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }

    // gasLimit
    {
        bytes encodedData = codec->encodeWithSig("gasLimitTest()");

        auto result = callTest(number++, ref(encodedData));

        u256 re;
        codec->decode(result->data(), re);
        BOOST_CHECK_EQUAL(re, gas);
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }

    // number
    {
        bytes encodedData = codec->encodeWithSig("numberTest()");

        number++;
        auto _number = number;
        auto result = callTest(_number, ref(encodedData));

        u256 re;
        codec->decode(result->data(), re);
        BOOST_CHECK_EQUAL(re, _number);
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }

    // timestamp
    {
        bytes encodedData = codec->encodeWithSig("timeStampTest()");

        auto result = callTest(number++, ref(encodedData));

        u256 re;
        codec->decode(result->data(), re);
        BOOST_CHECK_LT(re, utcTime());
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }

    // callData
    {
        bytes encodedData = codec->encodeWithSig("callDataTest()");

        auto result = callTest(number++, ref(encodedData));

        bytes re;
        codec->decode(result->data(), re);
        BOOST_CHECK_EQUAL(toHexStringWithPrefix(re), toHexStringWithPrefix(encodedData));
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }

    // sender
    {
        bytes encodedData = codec->encodeWithSig("senderTest()");

        auto result = callTest(number++, ref(encodedData));

        Address re;
        codec->decode(result->data(), re);
        BOOST_CHECK_EQUAL(re, Address(sender));
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }

    // origin
    {
        bytes encodedData = codec->encodeWithSig("originTest()");

        auto result = callTest(number++, ref(encodedData), origin);

        Address re;
        codec->decode(result->data(), re);
        BOOST_CHECK_EQUAL(re, Address(origin));
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }

    // blockHash
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test