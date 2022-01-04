/**
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
 * @file CNSPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-06-03
 */

#include "precompiled/CNSPrecompiled.h"
#include "PreCompiledFixture.h"
#include "precompiled/Common.h"
#include <bcos-framework/interfaces/executor/PrecompiledTypeDef.h>
#include <bcos-framework/testutils/TestPromptFixture.h>
#include <json/json.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class CNSPrecompiledFixture : public PrecompiledFixture
{
public:
    CNSPrecompiledFixture()
    {
        codec = std::make_shared<PrecompiledCodec>(hashImpl, false);
        setIsWasm(false);
        h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
        addressString = addressCreate.hex().substr(0, 40);
        bin =
            "60806040523480156200001157600080fd5b506110046000806101000a81548173ffffffffffffffffffff"
            "ffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555062"
            "00005e62000298565b604051809103906000f0801580156200007b573d6000803e3d6000fd5b5060016000"
            "6101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffff"
            "ffffffffffffffffffffff1602179055506040805190810160405280600a81526020017f48656c6c6f576f"
            "726c6400000000000000000000000000000000000000000000815250600290805190602001906200010992"
            "9190620002a9565b50600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff"
            "1673ffffffffffffffffffffffffffffffffffffffff1663b83686156040518163ffffffff167c01000000"
            "00000000000000000000000000000000000000000000000000028152600401602060405180830381600087"
            "803b1580156200019157600080fd5b505af1158015620001a6573d6000803e3d6000fd5b50505050604051"
            "3d6020811015620001bd57600080fd5b8101908080519060200190929190505050600360006101000a8154"
            "8173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffff"
            "ffffffffff1602179055506101606040519081016040528061012681526020016200162e61012691396004"
            "908051906020019062000243929190620002a9565b506040805190810160405280600381526020017f312e"
            "30000000000000000000000000000000000000000000000000000000000081525060059080519060200190"
            "62000291929190620002a9565b5062000358565b604051610590806200109e83390190565b828054600181"
            "600116156101000203166002900490600052602060002090601f016020900481019282601f10620002ec57"
            "805160ff19168380011785556200031d565b828001600101855582156200031d579182015b828111156200"
            "031c578251825591602001919060010190620002ff565b5b5090506200032c919062000330565b5090565b"
            "6200035591905b808211156200035157600081600090555060010162000337565b5090565b90565b610d36"
            "80620003686000396000f300608060405260043610610057576000357c0100000000000000000000000000"
            "000000000000000000000000000000900463ffffffff16806341634fab1461005c57806375cc6f05146101"
            "85578063a179e308146102e0575b600080fd5b34801561006857600080fd5b5061016f6004803603810190"
            "80803590602001908201803590602001908080601f01602080910402602001604051908101604052809392"
            "91908181526020018383808284378201915050505050509192919290803590602001908201803590602001"
            "908080601f0160208091040260200160405190810160405280939291908181526020018383808284378201"
            "915050505050509192919290803573ffffffffffffffffffffffffffffffffffffffff1690602001909291"
            "90803590602001908201803590602001908080601f01602080910402602001604051908101604052809392"
            "9190818152602001838380828437820191505050505050919291929050505061030f565b60405180828152"
            "60200191505060405180910390f35b34801561019157600080fd5b50610232600480360381019080803590"
            "602001908201803590602001908080601f0160208091040260200160405190810160405280939291908181"
            "52602001838380828437820191505050505050919291929080359060200190820180359060200190808060"
            "1f016020809104026020016040519081016040528093929190818152602001838380828437820191505050"
            "505050919291929050505061055e565b604051808373ffffffffffffffffffffffffffffffffffffffff16"
            "73ffffffffffffffffffffffffffffffffffffffff16815260200180602001828103825283818151815260"
            "200191508051906020019080838360005b838110156102a457808201518184015260208101905061028956"
            "5b50505050905090810190601f1680156102d15780820380516001836020036101000a0319168152602001"
            "91505b50935050505060405180910390f35b3480156102ec57600080fd5b506102f5610788565b60405180"
            "8215151515815260200191505060405180910390f35b6000806000809054906101000a900473ffffffffff"
            "ffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16634acd311e"
            "878787876040518563ffffffff167c01000000000000000000000000000000000000000000000000000000"
            "000281526004018080602001806020018573ffffffffffffffffffffffffffffffffffffffff1673ffffff"
            "ffffffffffffffffffffffffffffffffff1681526020018060200184810384528881815181526020019150"
            "8051906020019080838360005b838110156103f85780820151818401526020810190506103dd565b505050"
            "50905090810190601f1680156104255780820380516001836020036101000a031916815260200191505b50"
            "848103835287818151815260200191508051906020019080838360005b8381101561045e57808201518184"
            "0152602081019050610443565b50505050905090810190601f16801561048b578082038051600183602003"
            "6101000a031916815260200191505b50848103825285818151815260200191508051906020019080838360"
            "005b838110156104c45780820151818401526020810190506104a9565b50505050905090810190601f1680"
            "156104f15780820380516001836020036101000a031916815260200191505b509750505050505050506020"
            "60405180830381600087803b15801561051557600080fd5b505af1158015610529573d6000803e3d6000fd"
            "5b505050506040513d602081101561053f57600080fd5b8101908080519060200190929190505050905080"
            "915050949350505050565b600060606000809054906101000a900473ffffffffffffffffffffffffffffff"
            "ffffffffff1673ffffffffffffffffffffffffffffffffffffffff1663897f02518560056040518363ffff"
            "ffff167c010000000000000000000000000000000000000000000000000000000002815260040180806020"
            "0180602001838103835285818151815260200191508051906020019080838360005b838110156106115780"
            "820151818401526020810190506105f6565b50505050905090810190601f16801561063e57808203805160"
            "01836020036101000a031916815260200191505b5083810382528481815460018160011615610100020316"
            "60029004815260200191508054600181600116156101000203166002900480156106c05780601f10610695"
            "576101008083540402835291602001916106c0565b820191906000526020600020905b8154815290600101"
            "906020018083116106a357829003601f168201915b5050945050505050600060405180830381600087803b"
            "1580156106e257600080fd5b505af11580156106f6573d6000803e3d6000fd5b505050506040513d600082"
            "3e3d601f19601f82011682018060405250604081101561072057600080fd5b810190808051906020019092"
            "9190805164010000000081111561074257600080fd5b8281019050602081018481111561075857600080fd"
            "5b815185600182028301116401000000008211171561077557600080fd5b50509291905050509150915092"
            "50929050565b600080600060606000809054906101000a900473ffffffffffffffffffffffffffffffffff"
            "ffffff1673ffffffffffffffffffffffffffffffffffffffff16634acd311e600260056003600090549061"
            "01000a900473ffffffffffffffffffffffffffffffffffffffff1660046040518563ffffffff167c010000"
            "00000000000000000000000000000000000000000000000000000281526004018080602001806020018573"
            "ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16"
            "81526020018060200184810384528881815460018160011615610100020316600290048152602001915080"
            "54600181600116156101000203166002900480156108e35780601f106108b8576101008083540402835291"
            "602001916108e3565b820191906000526020600020905b8154815290600101906020018083116108c65782"
            "9003601f168201915b50508481038352878181546001816001161561010002031660029004815260200191"
            "508054600181600116156101000203166002900480156109665780601f1061093b57610100808354040283"
            "529160200191610966565b820191906000526020600020905b815481529060010190602001808311610949"
            "57829003601f168201915b5050848103825285818154600181600116156101000203166002900481526020"
            "0191508054600181600116156101000203166002900480156109e95780601f106109be5761010080835404"
            "02835291602001916109e9565b820191906000526020600020905b81548152906001019060200180831161"
            "09cc57829003601f168201915b5050975050505050505050602060405180830381600087803b158015610a"
            "0e57600080fd5b505af1158015610a22573d6000803e3d6000fd5b505050506040513d6020811015610a38"
            "57600080fd5b81019080805190602001909291905050509250600083141515610a5e5760009350610d0456"
            "5b6000809054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffff"
            "ffffffffffffffffffffffffff1663897f0251600260056040518363ffffffff167c010000000000000000"
            "00000000000000000000000000000000000000000281526004018080602001806020018381038352858181"
            "54600181600116156101000203166002900481526020019150805460018160011615610100020316600290"
            "048015610b575780601f10610b2c57610100808354040283529160200191610b57565b8201919060005260"
            "20600020905b815481529060010190602001808311610b3a57829003601f168201915b5050838103825284"
            "81815460018160011615610100020316600290048152602001915080546001816001161561010002031660"
            "0290048015610bda5780601f10610baf57610100808354040283529160200191610bda565b820191906000"
            "526020600020905b815481529060010190602001808311610bbd57829003601f168201915b505094505050"
            "5050600060405180830381600087803b158015610bfc57600080fd5b505af1158015610c10573d6000803e"
            "3d6000fd5b505050506040513d6000823e3d601f19601f820116820180604052506040811015610c3a5760"
            "0080fd5b81019080805190602001909291908051640100000000811115610c5c57600080fd5b8281019050"
            "6020810184811115610c7257600080fd5b8151856001820283011164010000000082111715610c8f576000"
            "80fd5b50509291905050508092508193505050600360009054906101000a900473ffffffffffffffffffff"
            "ffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168273ffffffffffffffff"
            "ffffffffffffffffffffffff16141515610cff5760009350610d04565b600193505b505050905600a16562"
            "7a7a723058206e0c9d278042741b5432a7c0da01d2545361877343d43bc36bda8b700c7941bb0029608060"
            "405234801561001057600080fd5b506040805190810160405280600d81526020017f48656c6c6f2c20576f"
            "726c6421000000000000000000000000000000000000008152506000908051906020019061005c92919061"
            "0062565b50610107565b828054600181600116156101000203166002900490600052602060002090601f01"
            "6020900481019282601f106100a357805160ff19168380011785556100d1565b8280016001018555821561"
            "00d1579182015b828111156100d05782518255916020019190600101906100b5565b5b5090506100de9190"
            "6100e2565b5090565b61010491905b808211156101005760008160009055506001016100e8565b5090565b"
            "90565b61047a806101166000396000f300608060405260043610610062576000357c010000000000000000"
            "0000000000000000000000000000000000000000900463ffffffff16806306fdde03146100675780634ed3"
            "885e146100f75780636d4ce63c14610160578063b8368615146101f0575b600080fd5b3480156100735760"
            "0080fd5b5061007c610247565b604051808060200182810382528381815181526020019150805190602001"
            "9080838360005b838110156100bc5780820151818401526020810190506100a1565b505050509050908101"
            "90601f1680156100e95780820380516001836020036101000a031916815260200191505b50925050506040"
            "5180910390f35b34801561010357600080fd5b5061015e6004803603810190808035906020019082018035"
            "90602001908080601f01602080910402602001604051908101604052809392919081815260200183838082"
            "843782019150505050505091929192905050506102e5565b005b34801561016c57600080fd5b5061017561"
            "02ff565b6040518080602001828103825283818151815260200191508051906020019080838360005b8381"
            "10156101b557808201518184015260208101905061019a565b50505050905090810190601f1680156101e2"
            "5780820380516001836020036101000a031916815260200191505b509250505060405180910390f35b3480"
            "156101fc57600080fd5b506102056103a1565b604051808273ffffffffffffffffffffffffffffffffffff"
            "ffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390f35b60"
            "008054600181600116156101000203166002900480601f0160208091040260200160405190810160405280"
            "929190818152602001828054600181600116156101000203166002900480156102dd5780601f106102b257"
            "6101008083540402835291602001916102dd565b820191906000526020600020905b815481529060010190"
            "6020018083116102c057829003601f168201915b505050505081565b80600090805190602001906102fb92"
            "91906103a9565b5050565b606060008054600181600116156101000203166002900480601f016020809104"
            "02602001604051908101604052809291908181526020018280546001816001161561010002031660029004"
            "80156103975780601f1061036c57610100808354040283529160200191610397565b820191906000526020"
            "600020905b81548152906001019060200180831161037a57829003601f168201915b505050505090509056"
            "5b600030905090565b828054600181600116156101000203166002900490600052602060002090601f0160"
            "20900481019282601f106103ea57805160ff1916838001178555610418565b828001600101855582156104"
            "18579182015b828111156104175782518255916020019190600101906103fc565b5b509050610425919061"
            "0429565b5090565b61044b91905b8082111561044757600081600090555060010161042f565b5090565b90"
            "5600a165627a7a723058208c7b44898edb531f977931e72d1195b47424ff97a80e0d22932be8fab36bd975"
            "00295b7b22636f6e7374616e74223a66616c73652c22696e70757473223a5b7b226e616d65223a226e756d"
            "222c2274797065223a2275696e74323536227d5d2c226e616d65223a227472616e73222c226f7574707574"
            "73223a5b5d2c2270617961626c65223a66616c73652c2274797065223a2266756e6374696f6e227d2c7b22"
            "636f6e7374616e74223a747275652c22696e70757473223a5b5d2c226e616d65223a22676574222c226f75"
            "7470757473223a5b7b226e616d65223a22222c2274797065223a2275696e74323536227d5d2c2270617961"
            "626c65223a66616c73652c2274797065223a2266756e6374696f6e227d2c7b22696e70757473223a5b5d2c"
            "2270617961626c65223a66616c73652c2274797065223a22636f6e7374727563746f72227d5d";
    }

    virtual ~CNSPrecompiledFixture() {}
    void deployTestContract(protocol::BlockNumber _number)
    {
        std::string helloBin =
            "608060405234801561001057600080fd5b506040805190810160405280600d81526020017f48656c6c6f2c"
            "20576f726c6421000000000000000000000000000000000000008152506000908051906020019061005c92"
            "9190610062565b50610107565b828054600181600116156101000203166002900490600052602060002090"
            "601f016020900481019282601f106100a357805160ff19168380011785556100d1565b8280016001018555"
            "82156100d1579182015b828111156100d05782518255916020019190600101906100b5565b5b5090506100"
            "de91906100e2565b5090565b61010491905b808211156101005760008160009055506001016100e8565b50"
            "90565b90565b61047a806101166000396000f300608060405260043610610062576000357c010000000000"
            "0000000000000000000000000000000000000000000000900463ffffffff16806306fdde03146100675780"
            "634ed3885e146100f75780636d4ce63c14610160578063b8368615146101f0575b600080fd5b3480156100"
            "7357600080fd5b5061007c610247565b604051808060200182810382528381815181526020019150805190"
            "6020019080838360005b838110156100bc5780820151818401526020810190506100a1565b505050509050"
            "90810190601f1680156100e95780820380516001836020036101000a031916815260200191505b50925050"
            "5060405180910390f35b34801561010357600080fd5b5061015e6004803603810190808035906020019082"
            "01803590602001908080601f01602080910402602001604051908101604052809392919081815260200183"
            "838082843782019150505050505091929192905050506102e5565b005b34801561016c57600080fd5b5061"
            "01756102ff565b604051808060200182810382528381815181526020019150805190602001908083836000"
            "5b838110156101b557808201518184015260208101905061019a565b50505050905090810190601f168015"
            "6101e25780820380516001836020036101000a031916815260200191505b509250505060405180910390f3"
            "5b3480156101fc57600080fd5b506102056103a1565b604051808273ffffffffffffffffffffffffffffff"
            "ffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200191505060405180910390"
            "f35b60008054600181600116156101000203166002900480601f0160208091040260200160405190810160"
            "405280929190818152602001828054600181600116156101000203166002900480156102dd5780601f1061"
            "02b2576101008083540402835291602001916102dd565b820191906000526020600020905b815481529060"
            "0101906020018083116102c057829003601f168201915b505050505081565b806000908051906020019061"
            "02fb9291906103a9565b5050565b606060008054600181600116156101000203166002900480601f016020"
            "80910402602001604051908101604052809291908181526020018280546001816001161561010002031660"
            "02900480156103975780601f1061036c57610100808354040283529160200191610397565b820191906000"
            "526020600020905b81548152906001019060200180831161037a57829003601f168201915b505050505090"
            "5090565b600030905090565b82805460018160011615610100020316600290049060005260206000209060"
            "1f016020900481019282601f106103ea57805160ff1916838001178555610418565b828001600101855582"
            "15610418579182015b828111156104175782518255916020019190600101906103fc565b5b509050610425"
            "9190610429565b5090565b61044b91905b8082111561044757600081600090555060010161042f565b5090"
            "565b905600a165627a7a723058208c7b44898edb531f977931e72d1195b47424ff97a80e0d22932be8fab3"
            "6bd9750029";
        bytes input;
        boost::algorithm::unhex(bin, std::back_inserter(input));
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
        params->setTo(addressString);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        NativeExecutionMessage paramsBak = *params;

        nextBlock(_number);
        // --------------------------------
        // Create contract test_precompiled
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
        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::MESSAGE);
        BOOST_CHECK_EQUAL(result->contextID(), 99);
        BOOST_CHECK_EQUAL(result->seq(), 1000);
        BOOST_CHECK_EQUAL(result->create(), true);
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), "");
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), addressString);
        BOOST_CHECK(result->to().empty());
        BOOST_CHECK_LT(result->gasAvailable(), gas);
        BOOST_CHECK_GT(result->keyLocks().size(), 0);

        // --------------------------------
        // Message 1: Create contract HelloWorld, set new seq 1001
        // --------------------------------
        result->setSeq(1001);
        result->setKeyLocks({});

        h256 addressCreate2(
            "ee6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");  // ee6f30856ad3bae00b1169808488502786a13e3c
        helloAddress = addressCreate2.hex().substr(0, 40);
        result->setTo(helloAddress);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(
            std::move(result), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        BOOST_CHECK(result2);
        BOOST_CHECK_EQUAL(result2->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result2->data().size(), 0);
        BOOST_CHECK_EQUAL(result2->contextID(), 99);
        BOOST_CHECK_EQUAL(result2->seq(), 1001);
        BOOST_CHECK_EQUAL(result2->origin(), sender);
        BOOST_CHECK_EQUAL(result2->from(), helloAddress);
        BOOST_CHECK_EQUAL(result2->to(), addressString);
        BOOST_CHECK_EQUAL(result2->newEVMContractAddress(), helloAddress);
        BOOST_CHECK_EQUAL(result2->create(), false);
        BOOST_CHECK_EQUAL(result2->status(), 0);

        // --------------------------------
        // Message 2: Create contract Hello success return,
        // set previous seq 1000
        // HelloWorld -> TestCNS
        // --------------------------------
        result2->setSeq(1000);
        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(
            std::move(result2), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        BOOST_CHECK(result3);
        BOOST_CHECK_EQUAL(result3->type(), ExecutionMessage::MESSAGE);
        BOOST_CHECK_GT(result3->data().size(), 0);
        auto param = codec->encodeWithSig("getThis()");
        BOOST_CHECK(result3->data().toBytes() == param);
        BOOST_CHECK_EQUAL(result3->contextID(), 99);
        BOOST_CHECK_EQUAL(result3->seq(), 1000);
        BOOST_CHECK_EQUAL(result3->from(), addressString);
        BOOST_CHECK_EQUAL(
            result3->to(), boost::algorithm::to_lower_copy(std::string(helloAddress)));
        BOOST_CHECK(!result3->keyLocks().empty());

        // --------------------------------
        // Message 3: TestCNS call HelloWorld's getThis(),
        // set new seq 1002
        // --------------------------------
        result3->setSeq(1002);
        result3->setKeyLocks({});
        std::promise<ExecutionMessage::UniquePtr> executePromise4;
        executor->executeTransaction(
            std::move(result3), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise4.set_value(std::move(result));
            });
        auto result4 = executePromise4.get_future().get();

        BOOST_CHECK(result4);
        BOOST_CHECK_EQUAL(result4->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_GT(result4->data().size(), 0);
        param = codec->encode(Address(helloAddress));
        BOOST_CHECK(result4->data().toBytes() == param);
        BOOST_CHECK_EQUAL(result4->contextID(), 99);
        BOOST_CHECK_EQUAL(result4->seq(), 1002);
        BOOST_CHECK_EQUAL(
            result4->from(), boost::algorithm::to_lower_copy(std::string(helloAddress)));
        BOOST_CHECK_EQUAL(result4->to(), addressString);
        BOOST_CHECK_EQUAL(result4->status(), 0);
        BOOST_CHECK(result4->message().empty());

        // --------------------------------
        // Message 4: TestCNS call HelloWorld success return,
        // set previous seq 1000
        // --------------------------------
        result4->setSeq(1000);
        result4->setKeyLocks({});
        std::promise<ExecutionMessage::UniquePtr> executePromise5;
        executor->executeTransaction(
            std::move(result4), [&](bcos::Error::UniquePtr&& error,
                                    bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise5.set_value(std::move(result));
            });
        auto result5 = executePromise5.get_future().get();

        BOOST_CHECK(result5);
        BOOST_CHECK_EQUAL(result5->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result5->contextID(), 99);
        BOOST_CHECK_EQUAL(result5->seq(), 1000);
        BOOST_CHECK_EQUAL(result5->from(), addressString);
        BOOST_CHECK_EQUAL(result5->to(), sender);
        BOOST_CHECK_EQUAL(result5->origin(), sender);
        BOOST_CHECK_EQUAL(result5->status(), 0);
        BOOST_CHECK(result5->message().empty());
    }

    std::string helloAddress;
    std::string bin;
    std::string addressString;
    std::string sender;
};

BOOST_FIXTURE_TEST_SUITE(precompiledCNSTest, CNSPrecompiledFixture)

BOOST_AUTO_TEST_CASE(helloTest)
{
    deployTestContract(1);
    bytes input = codec->encodeWithSig("helloTest()");
    auto tx = fakeTransaction(cryptoSuite, keyPair, "", input, 101, 100001, "1", "1");
    sender = boost::algorithm::hex_lower(std::string(tx->sender()));

    auto hash = tx->hash();
    txpool->hash2Transaction.emplace(hash, tx);

    auto params = std::make_unique<NativeExecutionMessage>();
    params = std::make_unique<NativeExecutionMessage>();
    params->setContextID(100);
    params->setSeq(2000);
    params->setDepth(0);
    params->setFrom(sender);
    params->setTo(addressString);
    params->setOrigin(sender);
    params->setStaticCall(false);
    params->setGasAvailable(gas);
    params->setCreate(false);
    params->setTransactionHash(hash);

    params->setData(codec->encodeWithSig("helloTest()"));
    params->setType(NativeExecutionMessage::TXHASH);

    std::promise<ExecutionMessage::UniquePtr> executePromise1;
    executor->executeTransaction(std::move(params),
        [&](bcos::Error::UniquePtr&& error, NativeExecutionMessage::UniquePtr&& result) {
            if (error)
            {
                std::cout << "Error!" << boost::diagnostic_information(*error);
            }
            executePromise1.set_value(std::move(result));
        });
    auto result1 = executePromise1.get_future().get();
    BOOST_CHECK(result1);
    BOOST_CHECK_EQUAL(result1->type(), ExecutionMessage::MESSAGE);
    BOOST_CHECK_GT(result1->data().size(), 0);
    std::string abiStr =
        "[{\"constant\":false,\"inputs\":[{\"name\":\"num\",\"type\":\"uint256\"}],\"name\":"
        "\"trans\",\"outputs\":[],\"payable\":false,\"type\":\"function\"},{\"constant\":true,"
        "\"inputs\":[],\"name\":\"get\",\"outputs\":[{\"name\":\"\",\"type\":\"uint256\"}],"
        "\"payable\":false,\"type\":\"function\"},{\"inputs\":[],\"payable\":false,\"type\":"
        "\"constructor\"}]";
    bytes r = codec->encodeWithSig("insert(string,string,address,string)",
        std::string("HelloWorld"), std::string("1.0"), Address(helloAddress), abiStr);

    //    BOOST_CHECK(result1->data().toBytes() == r);
    BOOST_CHECK_EQUAL(result1->contextID(), 100);
    BOOST_CHECK_EQUAL(result1->seq(), 2000);
    BOOST_CHECK_EQUAL(result1->from(), addressString);
    BOOST_CHECK_EQUAL(result1->to(), std::string(CNS_ADDRESS));
    BOOST_CHECK_EQUAL(result1->origin(), sender);
    BOOST_CHECK_EQUAL(result1->status(), 0);
    BOOST_CHECK(result1->message().empty());

    // call cns.insert
    result1->setSeq(2001);
    std::promise<ExecutionMessage::UniquePtr> executePromise2;
    executor->executeTransaction(std::move(result1),
        [&](bcos::Error::UniquePtr&& error, NativeExecutionMessage::UniquePtr&& result) {
            if (error)
            {
                std::cout << "Error!" << boost::diagnostic_information(*error);
            }
            executePromise2.set_value(std::move(result));
        });
    auto result2 = executePromise2.get_future().get();
    BOOST_CHECK(result2);
    BOOST_CHECK_EQUAL(result2->type(), ExecutionMessage::FINISHED);
    BOOST_CHECK_GT(result2->data().size(), 0);
    r = codec->encode(u256(0));
    BOOST_CHECK(result2->data().toBytes() == r);
    BOOST_CHECK_EQUAL(result2->contextID(), 100);
    BOOST_CHECK_EQUAL(result2->seq(), 2001);
    BOOST_CHECK_EQUAL(result2->from(), std::string(CNS_ADDRESS));
    BOOST_CHECK_EQUAL(result2->to(), addressString);
    BOOST_CHECK_EQUAL(result2->origin(), sender);
    BOOST_CHECK_EQUAL(result2->status(), 0);
    BOOST_CHECK(result2->message().empty());

    // callback to TestCNS, TestCNS call cns.selectByNameAndVersion
    result2->setSeq(2000);
    result2->setKeyLocks({});
    std::promise<ExecutionMessage::UniquePtr> executePromise3;
    executor->executeTransaction(std::move(result2),
        [&](bcos::Error::UniquePtr&& error, NativeExecutionMessage::UniquePtr&& result) {
            if (error)
            {
                std::cout << "Error!" << boost::diagnostic_information(*error);
            }
            executePromise3.set_value(std::move(result));
        });
    auto result3 = executePromise3.get_future().get();
    BOOST_CHECK(result3);
    BOOST_CHECK_EQUAL(result3->type(), ExecutionMessage::MESSAGE);
    BOOST_CHECK_GT(result3->data().size(), 0);
    BOOST_CHECK_EQUAL(result3->contextID(), 100);
    BOOST_CHECK_EQUAL(result3->seq(), 2000);
    BOOST_CHECK_EQUAL(result3->from(), addressString);
    BOOST_CHECK_EQUAL(result3->to(), std::string(CNS_ADDRESS));
    BOOST_CHECK_EQUAL(result3->origin(), sender);
    BOOST_CHECK_EQUAL(result3->status(), 0);
    BOOST_CHECK(result3->message().empty());

    // call cns.selectByNameAndVersion
    result3->setSeq(2002);
    std::promise<ExecutionMessage::UniquePtr> executePromise4;
    executor->executeTransaction(std::move(result3),
        [&](bcos::Error::UniquePtr&& error, NativeExecutionMessage::UniquePtr&& result) {
            if (error)
            {
                std::cout << "Error!" << boost::diagnostic_information(*error);
            }
            executePromise4.set_value(std::move(result));
        });
    auto result4 = executePromise4.get_future().get();
    BOOST_CHECK(result4);
    BOOST_CHECK_EQUAL(result4->type(), ExecutionMessage::FINISHED);
    BOOST_CHECK_GT(result4->data().size(), 0);
    r = codec->encode(Address(helloAddress), abiStr);
    BOOST_CHECK(result4->data().toBytes() == r);
    BOOST_CHECK_EQUAL(result4->contextID(), 100);
    BOOST_CHECK_EQUAL(result4->seq(), 2002);
    BOOST_CHECK_EQUAL(result4->from(), std::string(CNS_ADDRESS));
    BOOST_CHECK_EQUAL(result4->to(), addressString);
    BOOST_CHECK_EQUAL(result4->origin(), sender);
    BOOST_CHECK_EQUAL(result4->status(), 0);
    BOOST_CHECK(result4->message().empty());

    // callback to TestCNS, return true
    result4->setSeq(2000);
    result4->setKeyLocks({});
    std::promise<ExecutionMessage::UniquePtr> executePromise5;
    executor->executeTransaction(std::move(result4),
        [&](bcos::Error::UniquePtr&& error, NativeExecutionMessage::UniquePtr&& result) {
            if (error)
            {
                std::cout << "Error!" << boost::diagnostic_information(*error);
            }
            executePromise5.set_value(std::move(result));
        });
    auto result5 = executePromise5.get_future().get();
    BOOST_CHECK(result5);
    BOOST_CHECK_EQUAL(result5->type(), ExecutionMessage::FINISHED);
    BOOST_CHECK_GT(result5->data().size(), 0);
    r = codec->encode(true);
    BOOST_CHECK(result5->data().toBytes() == r);
    BOOST_CHECK_EQUAL(result5->contextID(), 100);
    BOOST_CHECK_EQUAL(result5->seq(), 2000);
    BOOST_CHECK_EQUAL(result5->from(), addressString);
    BOOST_CHECK_EQUAL(result5->to(), sender);
    BOOST_CHECK_EQUAL(result5->origin(), sender);
    BOOST_CHECK_EQUAL(result5->status(), 0);
    BOOST_CHECK(result5->message().empty());
}

BOOST_AUTO_TEST_CASE(insertTest)
{
    deployTestContract(1);
    commitBlock(1);

    std::string contractName = "Ok";
    std::string contractVersion = "1.0";
    Address contractAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2");
    std::string contractAbi =
        "[{\"constant\":false,\"inputs\":[{\"name\":"
        "\"num\",\"type\":\"uint256\"}],\"name\":"
        "\"trans\",\"outputs\":[],\"payable\":false,"
        "\"type\":\"function\"},{\"constant\":true,"
        "\"inputs\":[],\"name\":\"get\",\"outputs\":[{"
        "\"name\":\"\",\"type\":\"uint256\"}],"
        "\"payable\":false,\"type\":\"function\"},{"
        "\"inputs\":[],\"payable\":false,\"type\":"
        "\"constructor\"}]";

    // insert overflow
    std::string overflowVersion130 =
        "012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789";

    // insert simple
    {
        nextBlock(2);
        bytes in = codec->encodeWithSig("insertTest(string,string,address,string)", contractName,
            contractVersion, contractAddress, contractAbi);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(101);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(addressString);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        // call cns.insert
        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();
        commitBlock(2);

        // query
        std::promise<Table> tablePromise;
        storage->asyncOpenTable(
            SYS_CNS, [&](Error::UniquePtr&& error, std::optional<Table>&& table) {
                BOOST_CHECK(!error);
                BOOST_CHECK(table);
                tablePromise.set_value(std::move(*table));
            });
        auto entry = tablePromise.get_future().get().getRow(contractName);
        BOOST_CHECK(entry.has_value());
        CNSInfoVec cnsInfoVec;

        auto&& out = asBytes(std::string(entry->getField(SYS_VALUE)));
        codec::scale::decode(cnsInfoVec, gsl::make_span(out));
        for (const auto& cnsInfo : cnsInfoVec)
        {
            if (std::get<0>(cnsInfo) == contractVersion)
            {
                BOOST_CHECK_EQUAL(std::get<1>(cnsInfo), "420f853b49838bd3e9466c85a4cc3428c960dde2");
            }
        }
    }

    // insert again with same item
    {
        nextBlock(3);
        bytes in = codec->encodeWithSig("insertTest(string,string,address,string)", contractName,
            contractVersion, contractAddress, contractAbi);

        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(102);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(addressString);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(error == nullptr);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        // call cns.insert
        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        s256 errCode;
        codec->decode(result3->data(), errCode);
        BOOST_TEST(errCode == s256((int)CODE_ADDRESS_AND_VERSION_EXIST));
        commitBlock(3);
    }

    // insert overflow version
    {
        nextBlock(4);

        bytes in = codec->encodeWithSig("insertTest(string,string,address,string)", contractName,
            overflowVersion130, contractAddress, contractAbi);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(103);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(addressString);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(error == nullptr);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        s256 errCode;
        codec->decode(result3->data(), errCode);
        BOOST_TEST(errCode == s256((int)CODE_VERSION_LENGTH_OVERFLOW));
        commitBlock(4);
    }

    // insert new item with same name, address and abi
    {
        nextBlock(5);
        contractVersion = "2.0";
        bytes in = codec->encodeWithSig("insertTest(string,string,address,string)", contractName,
            contractVersion, contractAddress, contractAbi);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(104);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(addressString);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(error == nullptr);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        commitBlock(5);

        // query
        auto table2 = memoryTableFactory->openTable(SYS_CNS);
        auto entry2 = table2->getRow(contractName);
        BOOST_TEST(entry2.has_value());
        CNSInfoVec cnsInfoVec;
        auto&& out = asBytes(std::string(entry2->getField(SYS_VALUE)));
        codec::scale::decode(cnsInfoVec, gsl::make_span(out));
        BOOST_CHECK(std::any_of(cnsInfoVec.begin(), cnsInfoVec.end(),
            [&](const auto& item) { return std::get<0>(item) == contractVersion; }));
    }
}

BOOST_AUTO_TEST_CASE(selectTest)
{
    deployTestContract(1);
    commitBlock(1);

    // select not exist key
    {
        nextBlock(2);
        bytes in = codec->encodeWithSig(
            "selectTest(string,string)", std::string("error"), std::string("err"));
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);

        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(101);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(addressString);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        // call cns.selectByNameAndVersion
        result2->setSeq(1001);

        std::promise<ExecutionMessage::UniquePtr> executePromise3;
        executor->executeTransaction(std::move(result2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise3.set_value(std::move(result));
            });
        auto result3 = executePromise3.get_future().get();

        Address address;
        std::string abi;
        codec->decode(result3->data(), address, abi);
        BOOST_CHECK(address == Address());
        BOOST_CHECK(abi.empty());
        commitBlock(2);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test