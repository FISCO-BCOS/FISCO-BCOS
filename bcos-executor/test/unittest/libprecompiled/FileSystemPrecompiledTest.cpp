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
 * @file FileSystemPrecompiledTest.cpp
 * @author: kyonRay
 * @date 2021-06-20
 */

#include "libprecompiled/PreCompiledFixture.h"
#include <bcos-framework/interfaces/executor/PrecompiledTypeDef.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <json/json.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class FileSystemPrecompiledFixture : public PrecompiledFixture
{
public:
    FileSystemPrecompiledFixture()
    {
        codec = std::make_shared<PrecompiledCodec>(hashImpl, false);
        setIsWasm(false);

        auto result1 = createTable(1, "test1", "id", "item1,item2");
        BOOST_CHECK(result1->data().toBytes() == codec->encode(s256(0)));
        auto result2 = createTable(2, "test2", "id", "item1,item2");
        BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(0)));
        h256 addressCreate("ff6f30856ad3bae00b1169808488502786a13e3c174d85682135ffd51310310e");
        addressString = addressCreate.hex().substr(0, 40);
    }

    virtual ~FileSystemPrecompiledFixture() {}

    void deployHelloContract(protocol::BlockNumber _number, std::string const& address)
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
        boost::algorithm::unhex(helloBin, std::back_inserter(input));
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
        params->setTo(address);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(input);
        params->setType(NativeExecutionMessage::TXHASH);
        params->setTransactionHash(hash);
        params->setCreate(true);

        nextBlock(_number);
        // --------------------------------
        // Create contract HelloWorld
        // --------------------------------

        std::promise<bcos::protocol::ExecutionMessage::UniquePtr> executePromise;
        executor->executeTransaction(
            std::move(params), [&](bcos::Error::UniquePtr&& error,
                                   bcos::protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });

        auto result = executePromise.get_future().get();
        commitBlock(_number);
        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(result->type(), ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(result->contextID(), 99);
        BOOST_CHECK_EQUAL(result->seq(), 1000);
        BOOST_CHECK_EQUAL(result->create(), false);
        BOOST_CHECK_EQUAL(result->newEVMContractAddress(), address);
        BOOST_CHECK_EQUAL(result->origin(), sender);
        BOOST_CHECK_EQUAL(result->from(), address);
    }

    ExecutionMessage::UniquePtr createTable(protocol::BlockNumber _number,
        std::string const& tableName, std::string const& key, std::string const& value,
        int _errorCode = 0)
    {
        bytes in = codec->encodeWithSig("createTable(string,string,string)", tableName, key, value);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::KV_TABLE_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        nextBlock(_number);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        // call precompiled
        result2->setSeq(1001);
        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr list(
        protocol::BlockNumber _number, std::string const& path, int _errorCode = 0)
    {
        bytes in = codec->encodeWithSig("list(string)", path);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::BFS_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        if (_errorCode != 0)
        {
            std::vector<BfsTuple> empty;
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode), empty));
        }

        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr mkdir(
        protocol::BlockNumber _number, std::string const& path, int _errorCode = 0)
    {
        bytes in = codec->encodeWithSig("mkdir(string)", path);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::BFS_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        // call precompiled
        result2->setSeq(1001);
        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result2;
    };

    ExecutionMessage::UniquePtr link(protocol::BlockNumber _number, std::string const& name,
        std::string const& version, std::string const& address, std::string const& abi,
        int _errorCode = 0)
    {
        bytes in =
            codec->encodeWithSig("link(string,string,string,string)", name, version, address, abi);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 101, 100001, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(1000);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(precompiled::BFS_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number);

        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->executeTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();
        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }

        commitBlock(_number);
        return result2;
    };

    std::string sender;
    std::string addressString;
};
BOOST_FIXTURE_TEST_SUITE(precompiledFileSystemTest, FileSystemPrecompiledFixture)

BOOST_AUTO_TEST_CASE(lsTest)
{
    BlockNumber _number = 3;

    // ls dir
    {
        auto result = list(_number++, "/tables");
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_CHECK(code == (int)CODE_SUCCESS);
        BOOST_CHECK(ls.size() == 2);
        BOOST_CHECK(std::get<0>(ls.at(0)) == "test1");
        BOOST_CHECK(std::get<0>(ls.at(1)) == "test2");
    }

    // ls regular
    {
        auto result = list(_number++, "/tables/test2");
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_CHECK(code == (int)CODE_SUCCESS);
        BOOST_CHECK(ls.size() == 1);
        BOOST_CHECK(std::get<0>(ls.at(0)) == "test2");
        BOOST_CHECK(std::get<1>(ls.at(0)) == FS_TYPE_CONTRACT);
    }

    // ls not exist
    {
        auto result = list(_number++, "/tables/test3", CODE_FILE_NOT_EXIST);
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_CHECK(code == s256((int)CODE_FILE_NOT_EXIST));
        BOOST_CHECK(ls.empty());
    }

    // ls /
    {
        auto result = list(_number++, "/");
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_CHECK(code == (int)CODE_SUCCESS);
        BOOST_CHECK(ls.size() == 3);  // with '/'
    }

    // mkdir invalid path
    {
        list(_number++, "", CODE_FILE_INVALID_PATH);
        std::stringstream errorPath;
        errorPath << std::setfill('0') << std::setw(56) << 1;
        list(_number++, "/" + errorPath.str(), CODE_FILE_INVALID_PATH);
        list(_number++, "/path/level/too/deep/not/over/six/", CODE_FILE_INVALID_PATH);
    }
}

BOOST_AUTO_TEST_CASE(mkdirTest)
{
    BlockNumber _number = 3;
    // simple mkdir
    {
        auto result = mkdir(_number++, "/tables/temp/test");
        s256 m;
        codec->decode(result->data(), m);
        BOOST_TEST(m == 0u);

        auto lsResult = list(_number++, "/tables");
        std::vector<BfsTuple> ls;
        s256 code;
        codec->decode(lsResult->data(), code, ls);
        BOOST_CHECK(code == (int)CODE_SUCCESS);
        BOOST_CHECK(ls.size() == 3);

        auto lsResult2 = list(_number++, "/tables/temp");
        std::vector<BfsTuple> ls2;
        codec->decode(lsResult2->data(), code, ls2);
        BOOST_CHECK(ls2.size() == 1);
        BOOST_CHECK(std::get<0>(ls2[0]) == "test");
        BOOST_CHECK(std::get<1>(ls2[0]) == FS_TYPE_DIR);
    }

    // mkdir /tables/test1/test
    {
        auto result = mkdir(_number++, "/tables/test1/test", CODE_FILE_BUILD_DIR_FAILED);
    }

    // mkdir /tables/test1
    {
        auto result = mkdir(_number++, "/tables/test1", CODE_FILE_ALREADY_EXIST);
    }

    // mkdir /tables
    {
        auto result = mkdir(_number++, "/tables", CODE_FILE_INVALID_PATH);
    }

    // mkdir in wrong path
    {
        auto result = mkdir(_number++, "/sys/test1", CODE_FILE_INVALID_PATH);
        auto result2 = mkdir(_number++, "/user/test1", CODE_FILE_INVALID_PATH);
        auto result3 = mkdir(_number++, "/test1", CODE_FILE_INVALID_PATH);
    }

    // mkdir invalid path
    {
        mkdir(_number++, "", CODE_FILE_INVALID_PATH);
        std::stringstream errorPath;
        errorPath << std::setfill('0') << std::setw(56) << 1;
        mkdir(_number++, "/" + errorPath.str(), CODE_FILE_INVALID_PATH);
        mkdir(_number++, "/path/level/too/deep/not/over/six/", CODE_FILE_INVALID_PATH);
    }
}

BOOST_AUTO_TEST_CASE(linkTest)
{
    BlockNumber number = 1;
    deployHelloContract(number++, addressString);

    std::string contractName = "Hello";
    std::string contractVersion = "1.0";
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

    // link overflow
    std::string overflowVersion130 =
        "012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789";
    // simple link
    {
        link(number++, contractName, contractVersion, addressString, contractAbi);
        auto result = list(number++, "/apps/Hello");
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_CHECK(ls.size() == 1);
        BOOST_CHECK(std::get<0>(ls.at(0)) == contractVersion);
        BOOST_CHECK(std::get<1>(ls.at(0)) == FS_TYPE_LINK);

        auto result2 = list(number++, "/apps/Hello/1.0");
        std::vector<BfsTuple> ls2;
        codec->decode(result2->data(), code, ls2);
        BOOST_CHECK(ls2.size() == 1);
        BOOST_CHECK(std::get<0>(ls2.at(0)) == contractVersion);
        BOOST_CHECK(std::get<1>(ls2.at(0)) == FS_TYPE_LINK);
        BOOST_CHECK(std::get<2>(ls2.at(0)) == addressString);
    }

    // overwrite link
    {
        auto latestVersion = "latest";
        link(number++, contractName, latestVersion, addressString, contractAbi);
        auto result = list(number++, "/apps/Hello");
        s256 code;
        std::vector<BfsTuple> ls;
        codec->decode(result->data(), code, ls);
        BOOST_CHECK(ls.size() == 2);
        BOOST_CHECK(std::get<0>(ls.at(0)) == contractVersion);
        BOOST_CHECK(std::get<1>(ls.at(0)) == FS_TYPE_LINK);

        auto result2 = list(number++, "/apps/Hello/latest");
        std::vector<BfsTuple> ls2;
        codec->decode(result2->data(), code, ls2);
        BOOST_CHECK(ls2.size() == 1);
        BOOST_CHECK(std::get<2>(ls2.at(0)) == addressString);

        auto newAddress = "420f853b49838bd3e9466c85a4cc3428c960dde2";
        deployHelloContract(number++, newAddress);
        link(number++, contractName, latestVersion, newAddress, contractAbi);
        auto result3 = list(number++, "/apps/Hello/latest");
        std::vector<BfsTuple> ls3;
        codec->decode(result3->data(), code, ls3);
        BOOST_CHECK(ls3.size() == 1);
        BOOST_CHECK(std::get<2>(ls3.at(0)) == newAddress);
    }

    // overflow version
    {
        link(number++, contractName, overflowVersion130, addressString, contractAbi,
            CODE_ADDRESS_OR_VERSION_ERROR);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
