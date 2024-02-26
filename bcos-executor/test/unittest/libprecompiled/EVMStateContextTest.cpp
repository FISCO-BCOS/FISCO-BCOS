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
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", input, std::to_string(101), 100001, "1", "1");
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
        executor->executeTransaction(
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
        auto tx = fakeTransaction(
            cryptoSuite, keyPair, "", encodedData.toBytes(), std::to_string(101), 100001, "1", "1");
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
    std::string testBin = "608060405234801561001057600080fd5b50611286806100206000396000f3fe608060405234801561001057600080fd5b50600436106101375760003560e01c806370a3872f116100b8578063caa260321161007c578063caa2603214610251578063cbbab99c14610271578063da1ce32314610279578063ec8b466a1461027f578063ff0732ec14610292578063fffd0167146102a557600080fd5b806370a3872f146101f2578063b2acd50914610205578063b815849414610218578063c13be6ea1461022b578063c3e6b0181461023e57600080fd5b80632a3c3ee5116100ff5780632a3c3ee5146101c5578063455259cb146101cd5780634849f279146101d3578063653d710c146101e65780636a4dc7f3146101ec57600080fd5b8063086ecbfa1461013c5780630c6daa6e146101625780631d1f490d146101825780631ecf4f211461018857806329470338146101b3575b600080fd5b61014f61014a366004610de3565b6102ab565b6040519081526020015b60405180910390f35b610175610170366004610f26565b61039b565b604051610159919061100c565b4261014f565b61019b610196366004611034565b61051f565b6040516001600160a01b039091168152602001610159565b61014f6101c1366004611077565b4090565b61014f610620565b3a61014f565b6101756101e1366004611090565b610667565b3261019b565b3361019b565b61014f6102003660046110c2565b610745565b61014f610213366004610de3565b6107ab565b61014f6102263660046110c2565b610874565b61014f610239366004610de3565b610902565b61014f61024c3660046110c2565b6109e9565b61026461025f366004610de3565b610abb565b604051610159919061111e565b610264610bbe565b4561014f565b61017561028d3660046110c2565b610bfd565b61014f6102a0366004610de3565b610cd4565b4361014f565b6000805a9050600080516020611211833981519152816040516102d091815260200190565b60405180910390a16003836040516102e89190611151565b602060405180830381855afa158015610305573d6000803e3d6000fd5b5050506040515160601b6bffffffffffffffffffffffff1916915060005a90506000610331828461116d565b90506000805160206112318339815191528160405161035291815260200190565b60405180910390a1612710811115801561036e57506102588110155b6103935760405162461bcd60e51b815260040161038a90611192565b60405180910390fd5b505050919050565b6103a3610d4a565b6103ab610d4a565b600087878260200201518860016020020151886000602002015189600160200201518a600260200201518b600360200201518b600060200201518c6001602090810291909101516040516001600160e01b031960e09b909b1b9a909a16918a0191909152602489019790975260448801959095526064870193909352608486019190915260a485015260c48401526001600160c01b031990811660e48401521660ec82015284151560f81b60f482015260f501604051602081830303815290604052905060005a90506000805160206112118339815191528160405161049391815260200190565b60405180910390a160408360d5602085016009600019fa6104b357600080fd5b60005a905060006104c4828461116d565b9050600080516020611231833981519152816040516104e591815260200190565b60405180910390a1620186a08111156105105760405162461bcd60e51b815260040161038a90611192565b50929998505050505050505050565b6000805a90506000805160206112118339815191528160405161054491815260200190565b60405180910390a160408051600081526020810180835288905260ff871691810191909152606081018590526080810184905260019060a0016020604051602081039080840390855afa15801561059f573d6000803e3d6000fd5b50505060206040510351915060005a905060006105bc828461116d565b9050600080516020611231833981519152816040516105dd91815260200190565b60405180910390a161271081111580156105f95750610bb88110155b6106155760405162461bcd60e51b815260040161038a90611192565b505050949350505050565b600048156106645760405162461bcd60e51b8152602060048201526011602482015270062617365666565206d757374206265203607c1b604482015260640161038a565b90565b61066f610d4a565b610677610d68565b85815260208101859052604081018490526060810183905260005a9050600080516020611211833981519152816040516106b391815260200190565b60405180910390a1604083608084600060065af180801561013757505060005a905060006106e1828461116d565b90506000805160206112318339815191528160405161070291815260200190565b60405180910390a1612710811115801561071d575060968110155b6107395760405162461bcd60e51b815260040161038a90611192565b50505050949350505050565b6000805a90506000838061075b5761075b6111bf565b858709905060005a90506000610771828561116d565b90506127108111158015610783575060015b61079f5760405162461bcd60e51b815260040161038a90611192565b50909695505050505050565b80516000906107bb60c0826111d5565b156107c557600080fd5b60005a9050600080516020611211833981519152816040516107e991815260200190565b60405180910390a16040516020818460208801600060085af180801561013757505051925060005a9050600061081f828461116d565b90506000805160206112318339815191528160405161084091815260200190565b60405180910390a1620186a081111561086b5760405162461bcd60e51b815260040161038a90611192565b50505050919050565b6000805a90506000805160206112118339815191528160405161089991815260200190565b60405180910390a1600083806108b1576108b16111bf565b858708905060005a905060006108c7828561116d565b9050600080516020611231833981519152816040516108e891815260200190565b60405180910390a16127108111158015610783575061079f565b6000805a90506000805160206112118339815191528160405161092791815260200190565b60405180910390a160028360405161093f9190611151565b602060405180830381855afa15801561095c573d6000803e3d6000fd5b5050506040513d601f19601f8201168201806040525081019061097f91906111f7565b915060005a90506000610992828461116d565b9050600080516020611231833981519152816040516109b391815260200190565b60405180910390a1612710811115801561036e5750603c8110156103935760405162461bcd60e51b815260040161038a90611192565b6000805a905060008051602061121183398151915281604051610a0e91815260200190565b60405180910390a160405160208152602080820152602060408201528560608201528460808201528360a082015260208160c083600060055af180801561013757505051915060005a90506000610a65828461116d565b905060008051602061123183398151915281604051610a8691815260200190565b60405180910390a1620186a0811115610ab15760405162461bcd60e51b815260040161038a90611192565b5050509392505050565b60606000825167ffffffffffffffff811115610ad957610ad9610da4565b6040519080825280601f01601f191660200182016040528015610b03576020820181803683370190505b50905060005a905060008051602061121183398151915281604051610b2a91815260200190565b60405180910390a1835180602084018260208801600060045af1610b4a57fe5b5060005a90506000610b5c828461116d565b905060008051602061123183398151915281604051610b7d91815260200190565b60405180910390a16127108111158015610b985750600f8110155b610bb45760405162461bcd60e51b815260040161038a90611192565b5091949350505050565b60606000368080601f01602080910402602001604051908101604052809392919081815260200183838082843760009201919091525092949350505050565b610c05610d4a565b610c0d610d86565b848152602081018490526040810183905260005a905060008051602061121183398151915281604051610c4291815260200190565b60405180910390a1604083606084600060075af180801561013757505060005a90506000610c70828461116d565b905060008051602061123183398151915281604051610c9191815260200190565b60405180910390a16127108111158015610cad57506117708110155b610cc95760405162461bcd60e51b815260040161038a90611192565b505050509392505050565b6000805a835160208501209250905060005a90506000610cf4828461116d565b905060008051602061123183398151915281604051610d1591815260200190565b60405180910390a1612710811115801561036e5750600081116103935760405162461bcd60e51b815260040161038a90611192565b60405180604001604052806002906020820280368337509192915050565b60405180608001604052806004906020820280368337509192915050565b60405180606001604052806003906020820280368337509192915050565b634e487b7160e01b600052604160045260246000fd5b6040805190810167ffffffffffffffff81118282101715610ddd57610ddd610da4565b60405290565b600060208284031215610df557600080fd5b813567ffffffffffffffff80821115610e0d57600080fd5b818401915084601f830112610e2157600080fd5b813581811115610e3357610e33610da4565b604051601f8201601f19908116603f01168101908382118183101715610e5b57610e5b610da4565b81604052828152876020848701011115610e7457600080fd5b826020860160208301376000928101602001929092525095945050505050565b6000610e9e610dba565b9050806040830184811115610eb257600080fd5b835b81811015610ecc578035835260209283019201610eb4565b50505092915050565b60006040516080810181811067ffffffffffffffff82111715610efa57610efa610da4565b6040529050806080830184811115610eb257600080fd5b80358015158114610f2157600080fd5b919050565b60008060008060006101408688031215610f3f57600080fd5b853563ffffffff81168114610f5357600080fd5b94506020603f87018813610f6657600080fd5b610f7288828901610e94565b945087607f880112610f8357600080fd5b610f908860608901610ed5565b93508760ff880112610fa157600080fd5b610fa9610dba565b8061012089018a811115610fbc57600080fd5b60e08a015b81811015610fef5780356001600160c01b031981168114610fe25760008081fd5b8452928401928401610fc1565b50819550610ffc81610f11565b9450505050509295509295909350565b60408101818360005b6002811015610ecc578151835260209283019290910190600101611015565b6000806000806080858703121561104a57600080fd5b84359350602085013560ff8116811461106257600080fd5b93969395505050506040820135916060013590565b60006020828403121561108957600080fd5b5035919050565b600080600080608085870312156110a657600080fd5b5050823594602084013594506040840135936060013592509050565b6000806000606084860312156110d757600080fd5b505081359360208301359350604090920135919050565b60005b838110156111095781810151838201526020016110f1565b83811115611118576000848401525b50505050565b602081526000825180602084015261113d8160408501602087016110ee565b601f01601f19169190910160400192915050565b600082516111638184602087016110ee565b9190910192915050565b60008282101561118d57634e487b7160e01b600052601160045260246000fd5b500390565b60208082526013908201527233b0b9b632b33a1031b432b1b59032b93937b960691b604082015260600190565b634e487b7160e01b600052601260045260246000fd5b6000826111f257634e487b7160e01b600052601260045260246000fd5b500690565b60006020828403121561120957600080fd5b505191905056fe356cba5013b6fef640a25e8fe894765f1a35b3c0f53cddddf906425862fa85a8dfa34a4e767d8f6cbaddb2435b6adb60a8c66377dc9885145c3c4ccfaf58d6d2a26469706673582212202bb4344be472753eed4cc2480f3e3f4a56d764280b0ea8154bf381e274c0370e64736f6c634300080b0033"; // 0.8.11 solc generate shorter codes
    // clang-format on
};

BOOST_FIXTURE_TEST_SUITE(EVMStateContextTest, EVMStateContextFixture)

BOOST_AUTO_TEST_CASE(testEVMPrecompiled)
{
    deployTest();
    bcos::protocol::BlockNumber number = 2;
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
        BOOST_CHECK_EQUAL(logEntry.size(), 2);  // must has 2 events
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
        BOOST_CHECK_EQUAL(logEntry.size(), 2);  // must has 2 events
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
        BOOST_CHECK_EQUAL(logEntry.size(), 2);  // must has 2 events
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
        BOOST_CHECK_EQUAL(logEntry.size(), 2);  // must has 2 events
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
        BOOST_CHECK_EQUAL(logEntry.size(), 2); // must has 2 events
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
        BOOST_CHECK_EQUAL(logEntry.size(), 2); // must has 2 events
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
        BOOST_CHECK_EQUAL(logEntry.size(), 2);  // must has 2 events
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

    // baseFee
    {
        bytes encodedData = codec->encodeWithSig("checkBaseFee()");

        auto result = callTest(number++, ref(encodedData), origin);
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }

    // gasPrice
    {
        bytes encodedData = codec->encodeWithSig("getGasPrice()");

        auto result = callTest(number++, ref(encodedData), origin);

        u256 re;
        codec->decode(result->data(), re);
        BOOST_CHECK_EQUAL(re, 0);
        BOOST_CHECK_EQUAL(result->status(), (int32_t)TransactionStatus::None);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test