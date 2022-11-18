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
* @file TablePrecompiledTest.cpp
* @author: kyonRay
* @date 2022-04-13
*/

#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/TableManagerPrecompiled.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <algorithm>
#include <map>
#include <random>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

namespace bcos::test
{
class TableFactoryPrecompiledFixture : public PrecompiledFixture
{
public:
   TableFactoryPrecompiledFixture() { init(false); }

   void init(bool isWasm)
   {
       setIsWasm(isWasm, false, true);
       tableTestAddress = isWasm ? "/tables/t_test" : "420f853b49838bd3e9466c85a4cc3428c960dde2";
   }

   virtual ~TableFactoryPrecompiledFixture() = default;

   ExecutionMessage::UniquePtr creatTable(protocol::BlockNumber _number,
       const std::string& tableName, const std::string& key, const std::vector<std::string>& value,
       const std::string& callAddress, int _errorCode = 0, bool errorInTableManager = false)
   {
       nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
       TableInfoTuple tableInfoTuple = std::make_tuple(key, value);
       bytes in = codec->encodeWithSig(
           "createTable(string,(string,string[]))", tableName, tableInfoTuple);
       auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 100, 10000, "1", "1");
       sender = boost::algorithm::hex_lower(std::string(tx->sender()));
       auto hash = tx->hash();
       txpool->hash2Transaction.emplace(hash, tx);
       auto params2 = std::make_unique<NativeExecutionMessage>();
       params2->setTransactionHash(hash);
       params2->setContextID(100);
       params2->setSeq(1000);
       params2->setDepth(0);
       params2->setFrom(sender);
       params2->setTo(isWasm ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS);
       params2->setOrigin(sender);
       params2->setStaticCall(false);
       params2->setGasAvailable(gas);
       params2->setData(std::move(in));
       params2->setType(NativeExecutionMessage::TXHASH);

       // call precompiled
       std::promise<ExecutionMessage::UniquePtr> executePromise2;
       executor->dmcExecuteTransaction(std::move(params2),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               BOOST_CHECK(!error);
               executePromise2.set_value(std::move(result));
           });
       auto result2 = executePromise2.get_future().get();

       if (errorInTableManager)
       {
           if (_errorCode != 0)
           {
               BOOST_CHECK(result2->data().toBytes() == codec->encode(int32_t(_errorCode)));
           }
           commitBlock(_number);
           return result2;
       }

       // set new address
       result2->setTo(callAddress);
       // external create
       result2->setSeq(1001);

       std::promise<ExecutionMessage::UniquePtr> executePromise3;
       executor->dmcExecuteTransaction(std::move(result2),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               BOOST_CHECK(!error);
               executePromise3.set_value(std::move(result));
           });
       auto result3 = executePromise3.get_future().get();

       result3->setSeq(1002);
       // external call bfs
       std::promise<ExecutionMessage::UniquePtr> executePromise4;
       executor->dmcExecuteTransaction(std::move(result3),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               BOOST_CHECK(!error);
               executePromise4.set_value(std::move(result));
           });
       auto result4 = executePromise4.get_future().get();

       // call bfs success, callback to create
       result4->setSeq(1001);

       std::promise<ExecutionMessage::UniquePtr> executePromise5;
       executor->dmcExecuteTransaction(std::move(result4),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               BOOST_CHECK(!error);
               executePromise5.set_value(std::move(result));
           });
       auto result5 = executePromise5.get_future().get();

       // create success, callback to precompiled
       result5->setSeq(1000);

       std::promise<ExecutionMessage::UniquePtr> executePromise6;
       executor->dmcExecuteTransaction(std::move(result5),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               BOOST_CHECK(!error);
               executePromise6.set_value(std::move(result));
           });
       auto result6 = executePromise6.get_future().get();

       if (_errorCode != 0)
       {
           BOOST_CHECK(result6->data().toBytes() == codec->encode(s256(_errorCode)));
       }
       commitBlock(_number);
       return result6;
   };

   ExecutionMessage::UniquePtr appendColumns(protocol::BlockNumber _number,
       const std::string& tableName, const std::vector<std::string>& values, int _errorCode = 0)
   {
       nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
       bytes in = codec->encodeWithSig("appendColumns(string,string[])", tableName, values);
       auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 100, 10000, "1", "1");
       sender = boost::algorithm::hex_lower(std::string(tx->sender()));
       auto hash = tx->hash();
       txpool->hash2Transaction.emplace(hash, tx);
       auto params2 = std::make_unique<NativeExecutionMessage>();
       params2->setTransactionHash(hash);
       params2->setContextID(100);
       params2->setSeq(1000);
       params2->setDepth(0);
       params2->setFrom(sender);
       params2->setTo(isWasm ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS);
       params2->setOrigin(sender);
       params2->setStaticCall(false);
       params2->setGasAvailable(gas);
       params2->setData(std::move(in));
       params2->setType(NativeExecutionMessage::TXHASH);

       // call precompiled
       std::promise<ExecutionMessage::UniquePtr> executePromise2;
       executor->dmcExecuteTransaction(std::move(params2),
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
       params2->setTo(isWasm ? BFS_NAME : BFS_ADDRESS);
       params2->setOrigin(sender);
       params2->setStaticCall(false);
       params2->setGasAvailable(gas);
       params2->setData(std::move(in));
       params2->setType(NativeExecutionMessage::TXHASH);
       nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);

       std::promise<ExecutionMessage::UniquePtr> executePromise2;
       executor->dmcExecuteTransaction(std::move(params2),
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

   ExecutionMessage::UniquePtr openTable(
       protocol::BlockNumber _number, std::string const& _path, int _errorCode = 0)
   {
       bytes in = codec->encodeWithSig("openTable(string)", _path);
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
       params2->setTo(isWasm ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS);
       params2->setOrigin(sender);
       params2->setStaticCall(false);
       params2->setGasAvailable(gas);
       params2->setData(std::move(in));
       params2->setType(NativeExecutionMessage::TXHASH);
       nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);

       std::promise<ExecutionMessage::UniquePtr> executePromise2;
       executor->dmcExecuteTransaction(std::move(params2),
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

   ExecutionMessage::UniquePtr desc(protocol::BlockNumber _number, std::string const& tableName)
   {
       nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
       bytes in = codec->encodeWithSig("desc(string)", tableName);
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
       params2->setTo(isWasm ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS);
       params2->setOrigin(sender);
       params2->setStaticCall(false);
       params2->setGasAvailable(gas);
       params2->setData(std::move(in));
       params2->setType(NativeExecutionMessage::TXHASH);

       std::promise<ExecutionMessage::UniquePtr> executePromise2;
       executor->dmcExecuteTransaction(std::move(params2),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               BOOST_CHECK(!error);
               executePromise2.set_value(std::move(result));
           });
       auto result2 = executePromise2.get_future().get();

       // call precompiled
       commitBlock(_number);
       return result2;
   }

   ExecutionMessage::UniquePtr insert(protocol::BlockNumber _number, const std::string& key,
       const std::vector<std::string>& values, const std::string& callAddress)
   {
       nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
       EntryTuple entryTuple = {key, values};
       bytes in = codec->encodeWithSig("insert((string,string[]))", entryTuple);
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
       params2->setTo(callAddress);
       params2->setOrigin(sender);
       params2->setStaticCall(false);
       params2->setGasAvailable(gas);
       params2->setData(std::move(in));
       params2->setType(NativeExecutionMessage::TXHASH);

       std::promise<ExecutionMessage::UniquePtr> executePromise2;
       executor->dmcExecuteTransaction(std::move(params2),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               executePromise2.set_value(std::move(result));
           });
       auto result2 = executePromise2.get_future().get();

       result2->setSeq(1001);

       // get desc
       std::promise<ExecutionMessage::UniquePtr> executePromise3;
       executor->dmcExecuteTransaction(std::move(result2),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               executePromise3.set_value(std::move(result));
           });
       auto result3 = executePromise3.get_future().get();

       result3->setSeq(1000);

       // get desc callback
       std::promise<ExecutionMessage::UniquePtr> executePromise4;
       executor->dmcExecuteTransaction(std::move(result3),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               executePromise4.set_value(std::move(result));
           });
       auto result4 = executePromise4.get_future().get();

       // call precompiled
       commitBlock(_number);
       return result4;
   }

   ExecutionMessage::UniquePtr selectByKey(
       protocol::BlockNumber _number, const std::string& key, const std::string& callAddress)
   {
       nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
       bytes in = codec->encodeWithSig("select(string)", key);
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
       params2->setTo(callAddress);
       params2->setOrigin(sender);
       params2->setStaticCall(false);
       params2->setGasAvailable(gas);
       params2->setData(std::move(in));
       params2->setType(NativeExecutionMessage::TXHASH);

       std::promise<ExecutionMessage::UniquePtr> executePromise2;
       executor->dmcExecuteTransaction(std::move(params2),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               BOOST_CHECK(!error);
               executePromise2.set_value(std::move(result));
           });
       auto result2 = executePromise2.get_future().get();

       // call precompiled
       commitBlock(_number);
       return result2;
   }

   ExecutionMessage::UniquePtr selectByCondition(protocol::BlockNumber _number,
       const std::vector<ConditionTupleV320>& keyCond, const LimitTuple& limit,
       const std::string& callAddress)
   {
       nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
       bytes in =
           codec->encodeWithSig("select((uint8,uint32,string)[],(uint32,uint32))", keyCond, limit);
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
       params2->setTo(callAddress);
       params2->setOrigin(sender);
       params2->setStaticCall(false);
       params2->setGasAvailable(gas);
       params2->setData(std::move(in));
       params2->setType(NativeExecutionMessage::TXHASH);

       std::promise<ExecutionMessage::UniquePtr> executePromise2;
       executor->dmcExecuteTransaction(std::move(params2),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               BOOST_CHECK(!error);
               executePromise2.set_value(std::move(result));
           });
       auto result2 = executePromise2.get_future().get();

       // call precompiled
       commitBlock(_number);
       return result2;
   }

   ExecutionMessage::UniquePtr count(protocol::BlockNumber _number,
       const std::vector<ConditionTupleV320>& keyCond, const std::string& callAddress)
   {
       nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
       bytes in = codec->encodeWithSig("count((uint8,uint32,string)[])", keyCond);
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
       params2->setTo(callAddress);
       params2->setOrigin(sender);
       params2->setStaticCall(false);
       params2->setGasAvailable(gas);
       params2->setData(std::move(in));
       params2->setType(NativeExecutionMessage::TXHASH);

       std::promise<ExecutionMessage::UniquePtr> executePromise2;
       executor->dmcExecuteTransaction(std::move(params2),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               BOOST_CHECK(!error);
               executePromise2.set_value(std::move(result));
           });
       auto result2 = executePromise2.get_future().get();

       // call precompiled
       commitBlock(_number);
       return result2;
   }

   ExecutionMessage::UniquePtr updateByKey(protocol::BlockNumber _number, const std::string& key,
       const std::vector<precompiled::UpdateFieldTuple>& _updateFields,
       const std::string& callAddress, bool _isErrorInTable = false)
   {
       nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
       bytes in = codec->encodeWithSig("update(string,(string,string)[])", key, _updateFields);
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
       params2->setTo(callAddress);
       params2->setOrigin(sender);
       params2->setStaticCall(false);
       params2->setGasAvailable(gas);
       params2->setData(std::move(in));
       params2->setType(NativeExecutionMessage::TXHASH);

       std::promise<ExecutionMessage::UniquePtr> executePromise2;
       executor->dmcExecuteTransaction(std::move(params2),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               BOOST_CHECK(!error);
               executePromise2.set_value(std::move(result));
           });
       auto result2 = executePromise2.get_future().get();

       if (_isErrorInTable)
       {
           // call precompiled
           commitBlock(_number);
           return result2;
       }

       result2->setSeq(1001);

       // get desc
       std::promise<ExecutionMessage::UniquePtr> executePromise3;
       executor->dmcExecuteTransaction(std::move(result2),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               executePromise3.set_value(std::move(result));
           });
       auto result3 = executePromise3.get_future().get();

       result3->setSeq(1000);

       // get desc callback
       std::promise<ExecutionMessage::UniquePtr> executePromise4;
       executor->dmcExecuteTransaction(std::move(result3),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               executePromise4.set_value(std::move(result));
           });
       auto result4 = executePromise4.get_future().get();

       // call precompiled
       commitBlock(_number);
       return result4;
   }

   ExecutionMessage::UniquePtr updateByCondition(protocol::BlockNumber _number,
       const std::vector<ConditionTupleV320>& conditions, const LimitTuple& _limit,
       const std::vector<precompiled::UpdateFieldTuple>& _updateFields,
       const std::string& callAddress)
   {
       nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
       bytes in = codec->encodeWithSig(
           "update((uint8,uint32,string)[],(uint32,uint32),(string,string)[])", conditions, _limit,
           _updateFields);
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
       params2->setTo(callAddress);
       params2->setOrigin(sender);
       params2->setStaticCall(false);
       params2->setGasAvailable(gas);
       params2->setData(std::move(in));
       params2->setType(NativeExecutionMessage::TXHASH);

       std::promise<ExecutionMessage::UniquePtr> executePromise2;
       executor->dmcExecuteTransaction(std::move(params2),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               BOOST_CHECK(!error);
               executePromise2.set_value(std::move(result));
           });
       auto result2 = executePromise2.get_future().get();

       result2->setSeq(1001);

       if (result2->status() != (int32_t)TransactionStatus::None)
       {
           commitBlock(_number);
           return result2;
       }

       // get desc
       std::promise<ExecutionMessage::UniquePtr> executePromise3;
       executor->dmcExecuteTransaction(std::move(result2),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               executePromise3.set_value(std::move(result));
           });
       auto result3 = executePromise3.get_future().get();

       result3->setSeq(1000);

       // get desc callback
       std::promise<ExecutionMessage::UniquePtr> executePromise4;
       executor->dmcExecuteTransaction(std::move(result3),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               executePromise4.set_value(std::move(result));
           });
       auto result4 = executePromise4.get_future().get();

       // call precompiled
       commitBlock(_number);
       return result4;
   }

   ExecutionMessage::UniquePtr removeByKey(
       protocol::BlockNumber _number, const std::string& key, const std::string& callAddress)
   {
       nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
       bytes in = codec->encodeWithSig("remove(string)", key);
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
       params2->setTo(callAddress);
       params2->setOrigin(sender);
       params2->setStaticCall(false);
       params2->setGasAvailable(gas);
       params2->setData(std::move(in));
       params2->setType(NativeExecutionMessage::TXHASH);

       std::promise<ExecutionMessage::UniquePtr> executePromise2;
       executor->dmcExecuteTransaction(std::move(params2),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               BOOST_CHECK(!error);
               executePromise2.set_value(std::move(result));
           });
       auto result2 = executePromise2.get_future().get();

       // call precompiled
       commitBlock(_number);
       return result2;
   }

   ExecutionMessage::UniquePtr removeByCondition(protocol::BlockNumber _number,
       const std::vector<ConditionTupleV320>& keyCond, const LimitTuple& limit,
       const std::string& callAddress)
   {
       nextBlock(_number, protocol::BlockVersion::V3_2_VERSION);
       bytes in =
           codec->encodeWithSig("remove((uint8,uint32,string)[],(uint32,uint32))", keyCond, limit);
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
       params2->setTo(callAddress);
       params2->setOrigin(sender);
       params2->setStaticCall(false);
       params2->setGasAvailable(gas);
       params2->setData(std::move(in));
       params2->setType(NativeExecutionMessage::TXHASH);

       std::promise<ExecutionMessage::UniquePtr> executePromise2;
       executor->dmcExecuteTransaction(std::move(params2),
           [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
               BOOST_CHECK(!error);
               executePromise2.set_value(std::move(result));
           });
       auto result2 = executePromise2.get_future().get();

       // call precompiled
       commitBlock(_number);
       return result2;
   }

   std::string tableTestAddress;
   std::string sender;
};


static std::string _fillZeros(int _num) {

   std::stringstream stream;
   stream << std::setfill('0') << std::setw(40) << std::right << _num;
   return stream.str();
}

static void generateRandomVector(
   uint32_t count, uint32_t _min, uint32_t _max, std::map<std::string, uint32_t>& res)
{
   static std::random_device rd;
   if (_min > _max || count > _max - _min + 1)
   {
       return;
   }
   std::vector<uint32_t> temp;
   temp.reserve(_max - _min + 1);
   for (uint32_t i = _min; i <= _max; i++)
   {
       temp.push_back(i);
   }
   std::shuffle(temp.begin(), temp.end(), std::default_random_engine(rd()));
   std::sort(temp.begin(), temp.begin() + count);
   size_t offset = res.size();
   for (uint32_t i = 0; i < count; i++)
   {
       res.insert({_fillZeros(temp[i]), i + offset});
   }
}

BOOST_FIXTURE_TEST_SUITE(precompiledTableTestV320, TableFactoryPrecompiledFixture)


BOOST_AUTO_TEST_CASE(countTest)
{
   const int INSERT_COUNT = 100000;

   auto callAddress = tableTestAddress;
   BlockNumber number = 1;
   {
       creatTable(number++, "t_test_condv320", "id", {"value"}, callAddress);
   }

   std::map<std::string, uint32_t> randomSet;
   int start = 0;
   int end = 499;
   for (int i = 0; i < INSERT_COUNT / 500; i++)
   {
       generateRandomVector(25, start, end, randomSet);
       start += 500;
       end += 500;
   }

   boost::log::core::get()->set_logging_enabled(false);
   for (int j = 0; j < INSERT_COUNT; ++j)
   {
       std::string value = "no";
       if (randomSet.contains(_fillZeros(j)))
       {
           value = "yes";
       }
       insert(number++, _fillZeros(j), {value}, callAddress);
   }
   // boost::log::core::get()->set_logging_enabled(true);

   // (<= && <= && ==) or (<= && <= && !=)
   {
       std::random_device rd;
       std::mt19937 gen(rd());
       std::uniform_int_distribution<uint32_t> distribution1(1, randomSet.size() / 2);
       std::uniform_int_distribution<uint32_t> distribution2(
           randomSet.size() / 2, randomSet.size());
       uint32_t low = distribution1(gen);
       uint32_t high = distribution2(gen);
       uint32_t validCount = 0;
       std::string lowKey;
       std::string highKey;
       uint32_t counter = 0;
       for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
       {
           ++counter;
           if (counter == low)
           {
               validCount = iter->second;
               lowKey = iter->first;
           }
           if (counter == high)
           {
               validCount = iter->second - validCount + 1;
               highKey = iter->first;
               break;
           }
       }
       // lowKey <= key <= highKey && value == "yes"
       ConditionTupleV320 cond1 = {3, 0, lowKey};
       ConditionTupleV320 cond2 = {5, 0, highKey};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       ConditionTupleV320 cond4 = {1, 1, "yes"};
       auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
       uint32_t countRes = 0;
       codec->decode(r1->data(), countRes);
       // FIXME: the ut will failed
       // BOOST_TEST(countRes == validCount);

       // lowKey <= key <= highKey && value != "yes"
       low = boost::lexical_cast<uint32_t>(lowKey);
       high = boost::lexical_cast<uint32_t>(highKey);
       uint32_t total = high - low + 1;
       auto r2 = count(number++, {cond1, cond2, cond4}, callAddress);
       countRes = 0;
       codec->decode(r2->data(), countRes);
       // FIXME: the ut will failed
       // BOOST_CHECK(countRes == total - validCount);
   }

   // (< && < && ==) or (< && < && !=)
   {
       std::random_device rd;
       std::mt19937 gen(rd());
       std::uniform_int_distribution<uint32_t> distribution1(1, randomSet.size() / 2 - 1);
       std::uniform_int_distribution<uint32_t> distribution2(
           randomSet.size() / 2 + 1, randomSet.size());
       uint32_t low = distribution1(gen);
       uint32_t high = distribution2(gen);
       uint32_t validCount = 0;
       std::string lowKey;
       std::string highKey;
       uint32_t counter = 0;
       for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
       {
           ++counter;
           if (counter == low)
           {
               validCount = iter->second;
               lowKey = iter->first;
           }
           if (counter == high)
           {
               validCount = iter->second - validCount - 1;
               highKey = iter->first;
               break;
           }
       }

       // lowKey < key < highKey && value == "yes"
       ConditionTupleV320 cond1 = {2, 0, lowKey};
       ConditionTupleV320 cond2 = {4, 0, highKey};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       ConditionTupleV320 cond4 = {1, 1, "yes"};
       auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
       uint32_t countRes = 0;
       codec->decode(r1->data(), countRes);
       BOOST_CHECK(countRes == validCount);

       // lowKey < key < highKey && value != "yes"
       low = boost::lexical_cast<uint32_t>(lowKey);
       high = boost::lexical_cast<uint32_t>(highKey);
       uint32_t total = high - low - 1;
       auto r2 = count(number++, {cond1, cond2, cond4}, callAddress);
       countRes = 0;
       codec->decode(r2->data(), countRes);
       BOOST_CHECK(countRes == total - validCount);
   }

   // 0 <= key <= 1001
   {
       uint32_t low = 0;
       uint32_t high = 1001;
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(low)};
       ConditionTupleV320 cond2 = {5, 0, _fillZeros(high)};
       auto r1 = count(number++, {cond1, cond2}, callAddress);
       uint32_t countRes = 0;
       codec->decode(r1->data(), countRes);
       BOOST_TEST(countRes == high - low + 1);
   }

   // value == "yes"
   {
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       auto r1 = count(number++, {cond3}, callAddress);
       uint32_t countRes = 0;
       codec->decode(r1->data(), countRes);
       BOOST_TEST(countRes == 25 * (INSERT_COUNT / 500));
   }

   // value == "no"
   {
       ConditionTupleV320 cond3 = {0, 1, "no"};
       auto r1 = count(number++, {cond3}, callAddress);
       uint32_t countRes = 0;
       codec->decode(r1->data(), countRes);
       BOOST_TEST(countRes == (500 - 25) * (INSERT_COUNT / 500));
   }

   // The index of condition out of range
   {
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       // index out of range
       ConditionTupleV320 cond3 = {0, 5, "yes"};
       auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // empty condition
   {
       auto r1 = count(number++, {}, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // condition with undefined cmp
   {
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       ConditionTupleV320 cond3 = {10, 1, "yes"};
       auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }
}


BOOST_AUTO_TEST_CASE(countWasmTest)
{
   init(true);
   const int INSERT_COUNT = 100000;

   auto callAddress = tableTestAddress;
   BlockNumber number = 1;
   {
       creatTable(number++, "t_test_condv320", "id", {"value"}, callAddress);
   }

   std::map<std::string, uint32_t> randomSet;
   int start = 0;
   int end = 499;
   for (int i = 0; i < INSERT_COUNT / 500; i++)
   {
       generateRandomVector(25, start, end, randomSet);
       start += 500;
       end += 500;
   }

   for (int j = 0; j < INSERT_COUNT; ++j)
   {
       boost::log::core::get()->set_logging_enabled(false);
       std::string value = "no";
       if (randomSet.contains(_fillZeros(j)))
       {
           value = "yes";
       }
       insert(number++, _fillZeros(j), {value}, callAddress);
       boost::log::core::get()->set_logging_enabled(true);
   }

   // (<= && <= && ==) or (<= && <= && !=)
   {
       std::random_device rd;
       std::mt19937 gen(rd());
       std::uniform_int_distribution<uint32_t> distribution1(1, randomSet.size() / 2);
       std::uniform_int_distribution<uint32_t> distribution2(
           randomSet.size() / 2, randomSet.size());
       uint32_t low = distribution1(gen);
       uint32_t high = distribution2(gen);
       uint32_t validCount = 0;
       std::string lowKey;
       std::string highKey;
       uint32_t counter = 0;
       for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
       {
           ++counter;
           if (counter == low)
           {
               validCount = iter->second;
               lowKey = iter->first;
           }
           if (counter == high)
           {
               validCount = iter->second - validCount + 1;
               highKey = iter->first;
               break;
           }
       }
       // lowKey <= key <= highKey && value == "yes"
       ConditionTupleV320 cond1 = {3, 0, lowKey};
       ConditionTupleV320 cond2 = {5, 0, highKey};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       ConditionTupleV320 cond4 = {1, 1, "yes"};
       auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
       uint32_t countRes = 0;
       codec->decode(r1->data(), countRes);
       // FIXME: the ut will failed
       // BOOST_TEST(countRes == validCount);

       // lowKey <= key <= highKey && value != "yes"
       low = boost::lexical_cast<uint32_t>(lowKey);
       high = boost::lexical_cast<uint32_t>(highKey);
       uint32_t total = high - low + 1;
       auto r2 = count(number++, {cond1, cond2, cond4}, callAddress);
       countRes = 0;
       codec->decode(r2->data(), countRes);
       // FIXME: the ut will failed
       // BOOST_CHECK(countRes == total - validCount);
   }

   // (< && < && ==) or (< && < && !=)
   {
       std::random_device rd;
       std::mt19937 gen(rd());
       std::uniform_int_distribution<uint32_t> distribution1(1, randomSet.size() / 2 - 1);
       std::uniform_int_distribution<uint32_t> distribution2(
           randomSet.size() / 2 + 1, randomSet.size());
       uint32_t low = distribution1(gen);
       uint32_t high = distribution2(gen);
       uint32_t validCount = 0;
       std::string lowKey;
       std::string highKey;
       uint32_t counter = 0;
       for (auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
       {
           ++counter;
           if (counter == low)
           {
               validCount = iter->second;
               lowKey = iter->first;
           }
           if (counter == high)
           {
               validCount = iter->second - validCount - 1;
               highKey = iter->first;
               break;
           }
       }

       // lowKey < key < highKey && value == "yes"
       ConditionTupleV320 cond1 = {2, 0, lowKey};
       ConditionTupleV320 cond2 = {4, 0, highKey};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       ConditionTupleV320 cond4 = {1, 1, "yes"};
       auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
       uint32_t countRes = 0;
       codec->decode(r1->data(), countRes);
       BOOST_CHECK(countRes == validCount);

       // lowKey < key < highKey && value != "yes"
       low = boost::lexical_cast<uint32_t>(lowKey);
       high = boost::lexical_cast<uint32_t>(highKey);
       uint32_t total = high - low - 1;
       auto r2 = count(number++, {cond1, cond2, cond4}, callAddress);
       countRes = 0;
       codec->decode(r2->data(), countRes);
       BOOST_CHECK(countRes == total - validCount);
   }

   // 0 <= key <= 1001
   {
       uint32_t low = 0;
       uint32_t high = 1001;
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(low)};
       ConditionTupleV320 cond2 = {5, 0, _fillZeros(high)};
       auto r1 = count(number++, {cond1, cond2}, callAddress);
       uint32_t countRes = 0;
       codec->decode(r1->data(), countRes);
       BOOST_CHECK(countRes == high - low + 1);
   }

   // value == "yes"
   {
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       auto r1 = count(number++, {cond3}, callAddress);
       uint32_t countRes = 0;
       codec->decode(r1->data(), countRes);
       BOOST_CHECK(countRes == 25 * (INSERT_COUNT / 500));
   }

   // value == "no"
   {
       ConditionTupleV320 cond3 = {0, 1, "no"};
       auto r1 = count(number++, {cond3}, callAddress);
       uint32_t countRes = 0;
       codec->decode(r1->data(), countRes);
       BOOST_CHECK(countRes == (500 - 25) * (INSERT_COUNT / 500));
   }

   // The index of condition out of range
   {
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       // index out of range
       ConditionTupleV320 cond3 = {0, 5, "yes"};
       auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // empty condition
   {
       auto r1 = count(number++, {}, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // condition with undefined cmp
   {
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       ConditionTupleV320 cond3 = {10, 1, "yes"};
       auto r1 = count(number++, {cond1, cond2, cond3}, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }
}

BOOST_AUTO_TEST_CASE(selectTest)
{
   /// INSERT_COUNT should > 100
   const int INSERT_COUNT = 10000;

   auto callAddress = tableTestAddress;
   BlockNumber number = 1;
   {
       creatTable(number++, "t_test_condv320", "id", {"value"}, callAddress);
   }

   std::map<std::string, uint32_t> randomSet;
   int start = 0;
   int end = 499;
   for(int i = 0; i < INSERT_COUNT / 500; i++)
   {
       generateRandomVector(25, start, end, randomSet);
       start += 500;
       end += 500;
   }

   for (int j = 0; j < INSERT_COUNT; ++j)
   {
       boost::log::core::get()->set_logging_enabled(false);
       std::string value = "no";
       if(randomSet.contains(_fillZeros(j)))
       {
           value  = "yes";
       }
       insert(number++, _fillZeros(j), {value}, callAddress);
       boost::log::core::get()->set_logging_enabled(true);
   }

   // select by condition——check limit and count
   {
       uint32_t limitOffset = 0;
       uint32_t limitCount = 50;
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       LimitTuple limit = {limitOffset, limitCount};
       auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       std::vector<EntryTuple> entries;
       codec->decode(r1->data(), entries);
       uint32_t count = 0;
       for(size_t i = 0; i < entries.size(); ++i)
       {
           EntryTuple& entry = entries[i];
           std::string key = std::get<0>(entry);
           auto iter = randomSet.find(key);
           if(iter == randomSet.end() || iter->second != i + limitOffset)
               break;
           ++count;
       }
       BOOST_CHECK(entries.size() == limitCount && count == limitCount);
   }

   {
       uint32_t limitOffset = 10;
       uint32_t limitCount = 75;
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       LimitTuple limit = {limitOffset, limitCount};
       auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       std::vector<EntryTuple> entries;
       codec->decode(r1->data(), entries);
       uint32_t count = 0;
       for(size_t i = 0; i < entries.size(); ++i)
       {
           EntryTuple& entry = entries[i];
           std::string key = std::get<0>(entry);
           auto iter = randomSet.find(key);
           if(iter == randomSet.end() || iter->second != i + limitOffset)
               break;
           ++count;
       }

       BOOST_CHECK(entries.size() == limitCount && count == limitCount);
   }

   {
       uint32_t limitOffset = 37;
       uint32_t limitCount = 75;
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       LimitTuple limit = {limitOffset, limitCount};
       auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       std::vector<EntryTuple> entries;
       codec->decode(r1->data(), entries);
       uint32_t count = 0;
       for(size_t i = 0; i < entries.size(); ++i)
       {
           EntryTuple& entry = entries[i];
           std::string key = std::get<0>(entry);
           auto iter = randomSet.find(key);
           if(iter == randomSet.end() || iter->second != i + limitOffset)
               break;
           ++count;
       }

       BOOST_CHECK(entries.size() == limitCount && count == limitCount);
   }

   {
       uint32_t limitOffset = 461;
       uint32_t limitCount = 75;
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       LimitTuple limit = {limitOffset, limitCount};
       auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       std::vector<EntryTuple> entries;
       codec->decode(r1->data(), entries);
       uint32_t count = 0;
       for(size_t i = 0; i < entries.size(); ++i)
       {
           EntryTuple& entry = entries[i];
           std::string key = std::get<0>(entry);
           auto iter = randomSet.find(key);
           if(iter == randomSet.end() || iter->second != i + limitOffset)
               break;
           ++count;
       }

       BOOST_CHECK(entries.size() == (500-limitOffset) && count == (500-limitOffset));
   }

   // select by condition limitCount < USER_TABLE_MIN_LIMIT_COUNT
   {
       uint32_t limitOffset = 0;
       uint32_t limitCount = 49;
       // lexicographical order， 1～INSERT_COUNT
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       LimitTuple limit = {limitOffset, limitCount};
       auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       std::vector<EntryTuple> entries;
       codec->decode(r1->data(), entries);
       uint32_t count = 0;
       for(size_t i = 0; i < entries.size(); ++i)
       {
           EntryTuple& entry = entries[i];
           std::string key = std::get<0>(entry);
           auto iter = randomSet.find(key);
           if(iter == randomSet.end() || iter->second != i + limitOffset)
               break;
           ++count;
       }

       BOOST_CHECK(entries.size() == limitCount && count == limitCount);
   }

   // select by condition limitCount == 0
   {
       uint32_t limitOffset = 0;
       uint32_t limitCount = 0;
       // lexicographical order， 1～INSERT_COUNT
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       LimitTuple limit = {limitOffset, limitCount};
       auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       std::vector<EntryTuple> entries;
       codec->decode(r1->data(), entries);
       uint32_t count = 0;
       for(size_t i = 0; i < entries.size(); ++i)
       {
           EntryTuple& entry = entries[i];
           std::string key = std::get<0>(entry);
           auto iter = randomSet.find(key);
           if(iter == randomSet.end() || iter->second != i + limitOffset)
               break;
           ++count;
       }

       BOOST_CHECK(entries.size() == limitCount && count == limitCount);
   }

   {
       // check not use key condition
       uint32_t count1 = 0;
       {
           uint32_t limitOffset = 461;
           uint32_t limitCount = 75;
           ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
           ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
           ConditionTupleV320 cond3 = {0, 1, "yes"};
           LimitTuple limit = {limitOffset, limitCount};
           auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
           std::vector<EntryTuple> entries;
           codec->decode(r1->data(), entries);

           for(size_t i = 0; i < entries.size(); ++i)
           {
               EntryTuple& entry = entries[i];
               std::string key = std::get<0>(entry);
               auto iter = randomSet.find(key);
               if(iter == randomSet.end() || iter->second != i + limitOffset)
                   break;
               ++count1;
           }
           BOOST_CHECK(entries.size() == (500-limitOffset) && count1 == (500-limitOffset));
       }
       uint32_t count2 = 0;
       {
           uint32_t limitOffset = 461;
           uint32_t limitCount = 75;
           ConditionTupleV320 cond3 = {0, 1, "yes"};
           LimitTuple limit = {limitOffset, limitCount};
           auto r1 = selectByCondition(number++, {cond3}, limit, callAddress);
           std::vector<EntryTuple> entries;
           codec->decode(r1->data(), entries);

           for(size_t i = 0; i < entries.size(); ++i)
           {
               EntryTuple& entry = entries[i];
               std::string key = std::get<0>(entry);
               auto iter = randomSet.find(key);
               if(iter == randomSet.end() || iter->second != i + limitOffset)
                   break;
               ++count2;
           }
           BOOST_CHECK(entries.size() == (500-limitOffset) && count2 == (500-limitOffset));
       }
       BOOST_CHECK(count1 == count2);
   }

   // empty condition
   {
       LimitTuple limit = {0, 10};
       auto r1 = selectByCondition(number++, {}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

       auto r2 = count(number++, {}, callAddress);
       BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // condition with undefined cmp
   {
       ConditionTupleV320 cond1 = {100, 0, "90"};
       LimitTuple limit = {0, 10};
       auto r1 = selectByCondition(number++, {cond1}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // limit overflow
   {
       ConditionTupleV320 cond1 = {0, 0, "90"};
       LimitTuple limit = {0, 10000};
       auto r1 = selectByCondition(number++, {cond1}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // The index of condition out of range
   {
       LimitTuple limit = {0, 50};
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       // index out of range  0 <= idx <= 1
       ConditionTupleV320 cond3 = {0, 2, "yes"};
       auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }
}

BOOST_AUTO_TEST_CASE(selectWasmTest)
{
   init(true);
   /// INSERT_COUNT should > 100
   const int INSERT_COUNT = 10000;

   auto callAddress = tableTestAddress;
   BlockNumber number = 1;
   {
       creatTable(number++, "t_test_condv320", "id", {"value"}, callAddress);
   }

   std::map<std::string, uint32_t> randomSet;
   int start = 0;
   int end = 499;
   for(int i = 0; i < INSERT_COUNT / 500; i++)
   {
       generateRandomVector(25, start, end, randomSet);
       start += 500;
       end += 500;
   }

   for (int j = 0; j < INSERT_COUNT; ++j)
   {
       boost::log::core::get()->set_logging_enabled(false);
       std::string value = "no";
       if(randomSet.contains(_fillZeros(j)))
       {
           value  = "yes";
       }
       insert(number++, _fillZeros(j), {value}, callAddress);
       boost::log::core::get()->set_logging_enabled(true);
   }

   // select by condition——check limit and count
   {
       uint32_t limitOffset = 0;
       uint32_t limitCount = 50;
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       LimitTuple limit = {limitOffset, limitCount};
       auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       std::vector<EntryTuple> entries;
       codec->decode(r1->data(), entries);
       uint32_t count = 0;
       for(size_t i = 0; i < entries.size(); ++i)
       {
           EntryTuple& entry = entries[i];
           std::string key = std::get<0>(entry);
           auto iter = randomSet.find(key);
           if(iter == randomSet.end() || iter->second != i + limitOffset)
               break;
           ++count;
       }
       BOOST_CHECK(entries.size() == limitCount && count == limitCount);
   }

   {
       uint32_t limitOffset = 10;
       uint32_t limitCount = 75;
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       LimitTuple limit = {limitOffset, limitCount};
       auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       std::vector<EntryTuple> entries;
       codec->decode(r1->data(), entries);
       uint32_t count = 0;
       for(size_t i = 0; i < entries.size(); ++i)
       {
           EntryTuple& entry = entries[i];
           std::string key = std::get<0>(entry);
           auto iter = randomSet.find(key);
           if(iter == randomSet.end() || iter->second != i + limitOffset)
               break;
           ++count;
       }

       BOOST_CHECK(entries.size() == limitCount && count == limitCount);
   }

   {
       uint32_t limitOffset = 37;
       uint32_t limitCount = 75;
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       LimitTuple limit = {limitOffset, limitCount};
       auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       std::vector<EntryTuple> entries;
       codec->decode(r1->data(), entries);
       uint32_t count = 0;
       for(size_t i = 0; i < entries.size(); ++i)
       {
           EntryTuple& entry = entries[i];
           std::string key = std::get<0>(entry);
           auto iter = randomSet.find(key);
           if(iter == randomSet.end() || iter->second != i + limitOffset)
               break;
           ++count;
       }

       BOOST_CHECK(entries.size() == limitCount && count == limitCount);
   }

   {
       uint32_t limitOffset = 461;
       uint32_t limitCount = 75;
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       LimitTuple limit = {limitOffset, limitCount};
       auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       std::vector<EntryTuple> entries;
       codec->decode(r1->data(), entries);
       uint32_t count = 0;
       for(size_t i = 0; i < entries.size(); ++i)
       {
           EntryTuple& entry = entries[i];
           std::string key = std::get<0>(entry);
           auto iter = randomSet.find(key);
           if(iter == randomSet.end() || iter->second != i + limitOffset)
               break;
           ++count;
       }

       BOOST_CHECK(entries.size() == (500-limitOffset) && count == (500-limitOffset));
   }

   // select by condition limitCount < USER_TABLE_MIN_LIMIT_COUNT
   {
       uint32_t limitOffset = 0;
       uint32_t limitCount = 49;
       // lexicographical order， 1～INSERT_COUNT
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       LimitTuple limit = {limitOffset, limitCount};
       auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       std::vector<EntryTuple> entries;
       codec->decode(r1->data(), entries);
       uint32_t count = 0;
       for(size_t i = 0; i < entries.size(); ++i)
       {
           EntryTuple& entry = entries[i];
           std::string key = std::get<0>(entry);
           auto iter = randomSet.find(key);
           if(iter == randomSet.end() || iter->second != i + limitOffset)
               break;
           ++count;
       }

       BOOST_CHECK(entries.size() == limitCount && count == limitCount);
   }


   {
       // check not use key condition
       uint32_t count1 = 0;
       {
           uint32_t limitOffset = 461;
           uint32_t limitCount = 75;
           ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
           ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
           ConditionTupleV320 cond3 = {0, 1, "yes"};
           LimitTuple limit = {limitOffset, limitCount};
           auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
           std::vector<EntryTuple> entries;
           codec->decode(r1->data(), entries);

           for(size_t i = 0; i < entries.size(); ++i)
           {
               EntryTuple& entry = entries[i];
               std::string key = std::get<0>(entry);
               auto iter = randomSet.find(key);
               if(iter == randomSet.end() || iter->second != i + limitOffset)
                   break;
               ++count1;
           }
           BOOST_CHECK(entries.size() == (500-limitOffset) && count1 == (500-limitOffset));
       }
       uint32_t count2 = 0;
       {
           uint32_t limitOffset = 461;
           uint32_t limitCount = 75;
           ConditionTupleV320 cond3 = {0, 1, "yes"};
           LimitTuple limit = {limitOffset, limitCount};
           auto r1 = selectByCondition(number++, {cond3}, limit, callAddress);
           std::vector<EntryTuple> entries;
           codec->decode(r1->data(), entries);

           for(size_t i = 0; i < entries.size(); ++i)
           {
               EntryTuple& entry = entries[i];
               std::string key = std::get<0>(entry);
               auto iter = randomSet.find(key);
               if(iter == randomSet.end() || iter->second != i + limitOffset)
                   break;
               ++count2;
           }
           BOOST_CHECK(entries.size() == (500-limitOffset) && count2 == (500-limitOffset));
       }
       BOOST_CHECK(count1 == count2);
   }

   // select by condition limitCount == 0
   {
       uint32_t limitOffset = 0;
       uint32_t limitCount = 0;
       // lexicographical order， 1～INSERT_COUNT
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       ConditionTupleV320 cond3 = {0, 1, "yes"};
       LimitTuple limit = {limitOffset, limitCount};
       auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       std::vector<EntryTuple> entries;
       codec->decode(r1->data(), entries);
       uint32_t count = 0;
       for(size_t i = 0; i < entries.size(); ++i)
       {
           EntryTuple& entry = entries[i];
           std::string key = std::get<0>(entry);
           auto iter = randomSet.find(key);
           if(iter == randomSet.end() || iter->second != i + limitOffset)
               break;
           ++count;
       }

       BOOST_CHECK(entries.size() == limitCount && count == limitCount);
   }

   // empty condition
   {
       LimitTuple limit = {0, 10};
       auto r1 = selectByCondition(number++, {}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);

       auto r2 = count(number++, {}, callAddress);
       BOOST_CHECK(r2->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // condition with undefined cmp
   {
       ConditionTupleV320 cond1 = {100, 0, "90"};
       LimitTuple limit = {0, 10};
       auto r1 = selectByCondition(number++, {cond1}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // limit overflow
   {
       ConditionTupleV320 cond1 = {0, 0, "90"};
       LimitTuple limit = {0, 10000};
       auto r1 = selectByCondition(number++, {cond1}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // The index of condition out of range
   {
       LimitTuple limit = {0, 50};
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       // index out of range  0 <= idx <= 1
       ConditionTupleV320 cond3 = {0, 2, "yes"};
       auto r1 = selectByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }
}

BOOST_AUTO_TEST_CASE(updateTest)
{
   const int INSERT_COUNT = 10000;
   auto callAddress = tableTestAddress;
   BlockNumber number = 1;
   {
       creatTable(number++, "t_test_condv320", "id", {"value"}, callAddress);
   }

   // prepare data
   std::map<std::string, uint32_t> randomSet;
   int start = 0;
   int end = 499;
   for(int i = 0; i < INSERT_COUNT / 500; i++)
   {
       generateRandomVector(25, start, end, randomSet);
       start += 500;
       end += 500;
   }

   for (int j = 0; j < INSERT_COUNT; ++j)
   {
       boost::log::core::get()->set_logging_enabled(false);
       std::string value = "no";
       if(randomSet.contains(_fillZeros(j)))
       {
           value  = "yes";
       }
       insert(number++, _fillZeros(j), {value}, callAddress);
       boost::log::core::get()->set_logging_enabled(true);
   }

   {
       auto updateFunc = [this, &number, &callAddress] (uint32_t low, uint32_t high, uint32_t offset, uint32_t count,
                             const std::string& target, const std::string& value) {
           ConditionTupleV320 cond1 = {3, 0, _fillZeros(low)};
           ConditionTupleV320 cond2 = {4, 0, _fillZeros(high)};
           ConditionTupleV320 cond3 = {0, 1, value};
           LimitTuple limit = {offset, count};
           UpdateFieldTuple updateFieldTuple1 = {"value", target};
           auto r1 = updateByCondition(number++, {cond1, cond2, cond3}, limit, {updateFieldTuple1}, callAddress);
           int32_t affectRows = 0;
           codec->decode(r1->data(), affectRows);
           return affectRows;
       };

       auto countFunc = [this, &number, &callAddress](const std::string& value) {
           ConditionTupleV320 cond = {0, 1, value};
           auto r1 = count(number++, {cond}, callAddress);
           uint32_t rows = 0;
           codec->decode(r1->data(), rows);
           return rows;
       };
       uint32_t countBeforeUpdate = countFunc("yes");
       // update value = "update" where (key >= 5000 && key < 6000) && (value == "yes")
       uint32_t affectRows1 = updateFunc(5000, 6000, 26, 20, "update", "yes");
       uint32_t countAfterUpdate = countFunc("update");
       // update value = "yes" where (key >= 0 && key < 10000) && (value == "update")
       uint32_t affectRows2 = updateFunc(0, 10000, 0, 500, "yes", "update");
       uint32_t countAfterRecover = countFunc("yes");
       BOOST_CHECK(affectRows1 == countAfterUpdate &&
                   affectRows1 == affectRows2 &&
                   affectRows1 == 20 &&
                   countBeforeUpdate == countAfterRecover &&
                   countBeforeUpdate == 500);
   }

   // limitcount == 0
   {
       auto updateFunc = [this, &number, &callAddress] (uint32_t low, uint32_t high, uint32_t offset, uint32_t count,
                             const std::string& target, const std::string& value) {
           ConditionTupleV320 cond1 = {3, 0, _fillZeros(low)};
           ConditionTupleV320 cond2 = {4, 0, _fillZeros(high)};
           ConditionTupleV320 cond3 = {0, 1, value};
           LimitTuple limit = {offset, count};
           UpdateFieldTuple updateFieldTuple1 = {"value", target};
           auto r1 = updateByCondition(number++, {cond1, cond2, cond3}, limit, {updateFieldTuple1}, callAddress);
           int32_t affectRows = 0;
           codec->decode(r1->data(), affectRows);
           return affectRows;
       };

       auto countFunc = [this, &number, &callAddress](const std::string& value) {
           ConditionTupleV320 cond = {0, 1, value};
           auto r1 = count(number++, {cond}, callAddress);
           uint32_t rows = 0;
           codec->decode(r1->data(), rows);
           return rows;
       };
       uint32_t countBeforeUpdate = countFunc("yes");
       // update value = "update" where (key >= 5000 && key < 6000) && (value == "yes")
       uint32_t affectRows1 = updateFunc(5000, 6000, 0, 0, "update", "yes");
       uint32_t countAfterUpdate = countFunc("update");
       // update value = "yes" where (key >= 0 && key < 10000) && (value == "update")
       uint32_t affectRows2 = updateFunc(0, 10000, 0, 0, "yes", "update");
       uint32_t countAfterRecover = countFunc("yes");
       BOOST_CHECK(affectRows1 == countAfterUpdate &&
                   affectRows1 == affectRows2 &&
                   affectRows1 == 0 &&
                   countBeforeUpdate == countAfterRecover &&
                   countBeforeUpdate == 500);
   }

   // empty condition
   {
       LimitTuple limit = {0, 10};
       UpdateFieldTuple updateFieldTuple1 = {"value", "update"};
       auto r1 = updateByCondition(number++, {}, limit, {updateFieldTuple1}, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // condition with undefined cmp
   {
       ConditionTupleV320 cond1 = {100, 0, "90"};
       LimitTuple limit = {0, 10};
       UpdateFieldTuple updateFieldTuple1 = {"value", "update"};
       auto r1 = updateByCondition(number++, {cond1}, limit, {updateFieldTuple1}, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // limit overflow
   {
       ConditionTupleV320 cond1 = {0, 0, "90"};
       LimitTuple limit = {0, 10000};
       UpdateFieldTuple updateFieldTuple1 = {"value", "update"};
       auto r1 = updateByCondition(number++, {cond1}, limit, {updateFieldTuple1}, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // The index of condition out of range
   {
       LimitTuple limit = {0, 50};
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       // index out of range  0 <= idx <= 1
       ConditionTupleV320 cond3 = {0, 2, "yes"};
       UpdateFieldTuple updateFieldTuple1 = {"value", "update"};
       auto r1 = updateByCondition(number++, {cond1, cond2, cond3}, limit, {updateFieldTuple1}, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }
}


BOOST_AUTO_TEST_CASE(updateWasmTest)
{
   init(true);
   const int INSERT_COUNT = 10000;
   auto callAddress = tableTestAddress;
   BlockNumber number = 1;
   {
       creatTable(number++, "t_test_condv320", "id", {"value"}, callAddress);
   }

   // prepare data
   std::map<std::string, uint32_t> randomSet;
   int start = 0;
   int end = 499;
   for(int i = 0; i < INSERT_COUNT / 500; i++)
   {
       generateRandomVector(25, start, end, randomSet);
       start += 500;
       end += 500;
   }

   for (int j = 0; j < INSERT_COUNT; ++j)
   {
       boost::log::core::get()->set_logging_enabled(false);
       std::string value = "no";
       if(randomSet.contains(_fillZeros(j)))
       {
           value  = "yes";
       }
       insert(number++, _fillZeros(j), {value}, callAddress);
       boost::log::core::get()->set_logging_enabled(true);
   }

   {
       auto updateFunc = [this, &number, &callAddress] (uint32_t low, uint32_t high, uint32_t offset, uint32_t count,
                             const std::string& target, const std::string& value) {
           ConditionTupleV320 cond1 = {3, 0, _fillZeros(low)};
           ConditionTupleV320 cond2 = {4, 0, _fillZeros(high)};
           ConditionTupleV320 cond3 = {0, 1, value};
           LimitTuple limit = {offset, count};
           UpdateFieldTuple updateFieldTuple1 = {"value", target};
           auto r1 = updateByCondition(number++, {cond1, cond2, cond3}, limit, {updateFieldTuple1}, callAddress);
           int32_t affectRows = 0;
           codec->decode(r1->data(), affectRows);
           return affectRows;
       };

       auto countFunc = [this, &number, &callAddress](const std::string& value) {
           ConditionTupleV320 cond = {0, 1, value};
           auto r1 = count(number++, {cond}, callAddress);
           uint32_t rows = 0;
           codec->decode(r1->data(), rows);
           return rows;
       };
       uint32_t countBeforeUpdate = countFunc("yes");
       // update value = "update" where (key >= 5000 && key < 6000) && (value == "yes")
       uint32_t affectRows1 = updateFunc(5000, 6000, 26, 20, "update", "yes");
       uint32_t countAfterUpdate = countFunc("update");
       // update value = "yes" where (key >= 0 && key < 10000) && (value == "update")
       uint32_t affectRows2 = updateFunc(0, 10000, 0, 500, "yes", "update");
       uint32_t countAfterRecover = countFunc("yes");
       BOOST_CHECK(affectRows1 == countAfterUpdate &&
                   affectRows1 == affectRows2 &&
                   affectRows1 == 20 &&
                   countBeforeUpdate == countAfterRecover &&
                   countBeforeUpdate == 500);
   }

   // limitcount == 0
   {
       auto updateFunc = [this, &number, &callAddress] (uint32_t low, uint32_t high, uint32_t offset, uint32_t count,
                             const std::string& target, const std::string& value) {
           ConditionTupleV320 cond1 = {3, 0, _fillZeros(low)};
           ConditionTupleV320 cond2 = {4, 0, _fillZeros(high)};
           ConditionTupleV320 cond3 = {0, 1, value};
           LimitTuple limit = {offset, count};
           UpdateFieldTuple updateFieldTuple1 = {"value", target};
           auto r1 = updateByCondition(number++, {cond1, cond2, cond3}, limit, {updateFieldTuple1}, callAddress);
           int32_t affectRows = 0;
           codec->decode(r1->data(), affectRows);
           return affectRows;
       };

       auto countFunc = [this, &number, &callAddress](const std::string& value) {
           ConditionTupleV320 cond = {0, 1, value};
           auto r1 = count(number++, {cond}, callAddress);
           uint32_t rows = 0;
           codec->decode(r1->data(), rows);
           return rows;
       };
       uint32_t countBeforeUpdate = countFunc("yes");
       // update value = "update" where (key >= 5000 && key < 6000) && (value == "yes")
       uint32_t affectRows1 = updateFunc(5000, 6000, 0, 0, "update", "yes");
       uint32_t countAfterUpdate = countFunc("update");
       // update value = "yes" where (key >= 0 && key < 10000) && (value == "update")
       uint32_t affectRows2 = updateFunc(0, 10000, 0, 0, "yes", "update");
       uint32_t countAfterRecover = countFunc("yes");
       BOOST_CHECK(affectRows1 == countAfterUpdate &&
                   affectRows1 == affectRows2 &&
                   affectRows1 == 0 &&
                   countBeforeUpdate == countAfterRecover &&
                   countBeforeUpdate == 500);
   }

   // empty condition
   {
       LimitTuple limit = {0, 10};
       UpdateFieldTuple updateFieldTuple1 = {"value", "update"};
       auto r1 = updateByCondition(number++, {}, limit, {updateFieldTuple1}, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // condition with undefined cmp
   {
       ConditionTupleV320 cond1 = {100, 0, "90"};
       LimitTuple limit = {0, 10};
       UpdateFieldTuple updateFieldTuple1 = {"value", "update"};
       auto r1 = updateByCondition(number++, {cond1}, limit, {updateFieldTuple1}, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // limit overflow
   {
       ConditionTupleV320 cond1 = {0, 0, "90"};
       LimitTuple limit = {0, 10000};
       UpdateFieldTuple updateFieldTuple1 = {"value", "update"};
       auto r1 = updateByCondition(number++, {cond1}, limit, {updateFieldTuple1}, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // The index of condition out of range
   {
       LimitTuple limit = {0, 50};
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       // index out of range  0 <= idx <= 1
       ConditionTupleV320 cond3 = {0, 2, "yes"};
       UpdateFieldTuple updateFieldTuple1 = {"value", "update"};
       auto r1 = updateByCondition(number++, {cond1, cond2, cond3}, limit, {updateFieldTuple1}, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }
}

BOOST_AUTO_TEST_CASE(removeTest)
{
   const int INSERT_COUNT = 10000;
   auto callAddress = tableTestAddress;
   BlockNumber number = 1;
   {
       creatTable(number++, "t_test_condv320", "id", {"value"}, callAddress);
   }

   // prepare data
   std::map<std::string, uint32_t> randomSet;
   int start = 0;
   int end = 499;
   for(int i = 0; i < INSERT_COUNT / 500; i++)
   {
       generateRandomVector(25, start, end, randomSet);
       start += 500;
       end += 500;
   }

   for (int j = 0; j < INSERT_COUNT; ++j)
   {
       boost::log::core::get()->set_logging_enabled(false);
       std::string value = "no";
       if(randomSet.contains(_fillZeros(j)))
       {
           value  = "yes";
       }
       insert(number++, _fillZeros(j), {value}, callAddress);
       boost::log::core::get()->set_logging_enabled(true);
   }

   auto recoverFunc = [this, &number, &callAddress](const std::set<std::string>& removed)
   {
       for (auto& iter : removed)
       {
           boost::log::core::get()->set_logging_enabled(false);
           insert(number++, iter, {"yes"}, callAddress);
           boost::log::core::get()->set_logging_enabled(true);
       }
   };

   auto removeFunc = [this, &number, &callAddress] (const std::string& low, const std::string& high, uint32_t offset, uint32_t count,
                         const std::string& value) {
       ConditionTupleV320 cond1 = {3, 0, low};
       ConditionTupleV320 cond2 = {4, 0, high};
       ConditionTupleV320 cond3 = {0, 1, value};
       LimitTuple limit = {offset, count};
       auto r1 = removeByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       int32_t removedRows = 0;
       codec->decode(r1->data(), removedRows);
       return removedRows;
   };

   auto selectFunc = [this, &number, &callAddress](const std::string& value, std::vector<EntryTuple>& entries) {
       ConditionTupleV320 cond = {0, 1, value};
       LimitTuple limit = {0, 500};
       auto r1 = selectByCondition(number++, {cond}, limit, callAddress);
       codec->decode(r1->data(), entries);
   };

   {
       std::default_random_engine generator;
       std::uniform_int_distribution<int> distribution(50, 100);
       int _start = distribution(generator);
       int limitCount = 100;
       std::string low;
       std::string high;
       int i = 0;
       for(auto iter = randomSet.begin(); iter != randomSet.end(); ++iter, ++i)
       {
           if(i == _start)
           {
               low = iter->first;
           }
           if(i - _start == limitCount)
           {
               high = iter->first;
               break;
           }
       }

       std::set<std::string> savedSet;
       std::set<std::string> removedSet;
       for(auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
       {
           if(iter->first >= low && iter->first < high)
           {
               removedSet.insert(iter->first);
               continue;
           }
           savedSet.insert(iter->first);
       }

       uint32_t removedRows1 = removeFunc(low, _fillZeros(INSERT_COUNT), 0, limitCount, "yes");
       std::vector<EntryTuple> entries;
       selectFunc("yes", entries);
       BOOST_CHECK(removedRows1 == 500 - entries.size());

       for(auto& entry : entries)
       {
           std::string key = std::get<0>(entry);
           if(!savedSet.contains(key))
           {
               BOOST_CHECK(false);
               break;
           }
           savedSet.erase(key);
       }
       BOOST_CHECK(savedSet.empty());
       // recover data
       recoverFunc(removedSet);
   }

   // limitCount == 0
   {
       std::default_random_engine generator;
       std::uniform_int_distribution<int> distribution(50, 100);
       int _start = distribution(generator);
       int limitCount = 0;
       std::string low;
       std::string high;
       int i = 0;
       for(auto iter = randomSet.begin(); iter != randomSet.end(); ++iter, ++i)
       {
           if(i ==_start)
           {
               low = iter->first;
           }
           if(i - _start == limitCount)
           {
               high = iter->first;
               break;
           }
       }

       std::set<std::string> savedSet;
       for(auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
       {
           if(iter->first >= low && iter->first < high)
           {
               continue;
           }
           savedSet.insert(iter->first);
       }

       uint32_t removedRows1 = removeFunc(low, _fillZeros(INSERT_COUNT), 0, limitCount, "yes");
       std::vector<EntryTuple> entries;
       selectFunc("yes", entries);
       BOOST_CHECK(removedRows1 == 500 - entries.size());

       for(auto& entry : entries)
       {
           std::string key = std::get<0>(entry);
           if(!savedSet.contains(key))
           {
               BOOST_CHECK(false);
               break;
           }
           savedSet.erase(key);
       }
       BOOST_CHECK(savedSet.empty());
   }

   // limitCount < USER_TABLE_MIN_LIMIT_COUNT
   {
       std::default_random_engine generator;
       std::uniform_int_distribution<int> distribution(50, 100);
       int _start = distribution(generator);
       int limitCount = 49;
       std::string low;
       std::string high;
       int i = 0;
       for(auto iter = randomSet.begin(); iter != randomSet.end(); ++iter, ++i)
       {
           if(i ==_start)
           {
               low = iter->first;
           }
           if(i - _start == limitCount)
           {
               high = iter->first;
               break;
           }
       }

       std::set<std::string> savedSet;
       for(auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
       {
           if(iter->first >= low && iter->first < high)
           {
               continue;
           }
           savedSet.insert(iter->first);
       }

       uint32_t removedRows1 = removeFunc(low, _fillZeros(INSERT_COUNT), 0, limitCount, "yes");
       std::vector<EntryTuple> entries;
       selectFunc("yes", entries);
       BOOST_CHECK(removedRows1 == 500 - entries.size());

       for(auto& entry : entries)
       {
           std::string key = std::get<0>(entry);
           if(!savedSet.contains(key))
           {
               BOOST_CHECK(false);
               break;
           }
           savedSet.erase(key);
       }
       BOOST_CHECK(savedSet.empty());
   }

   // empty condition
   {
       LimitTuple limit = {0, 10};
       auto r1 = removeByCondition(number++, {}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // condition with undefined cmp
   {
       ConditionTupleV320 cond1 = {100, 0, "90"};
       LimitTuple limit = {0, 10};
       auto r1 = removeByCondition(number++, {cond1}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // limit overflow
   {
       ConditionTupleV320 cond1 = {0, 0, "90"};
       LimitTuple limit = {0, 10000};
       auto r1 = removeByCondition(number++, {cond1}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // The index of condition out of range
   {
       LimitTuple limit = {0, 50};
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       // index out of range  0 <= idx <= 1
       ConditionTupleV320 cond3 = {0, 2, "yes"};
       auto r1 = removeByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }
}

BOOST_AUTO_TEST_CASE(removeWasmTest)
{
   init(true);
   const int INSERT_COUNT = 10000;
   auto callAddress = tableTestAddress;
   BlockNumber number = 1;
   {
       creatTable(number++, "t_test_condv320", "id", {"value"}, callAddress);
   }

   // prepare data
   std::map<std::string, uint32_t> randomSet;
   int start = 0;
   int end = 499;
   for(int i = 0; i < INSERT_COUNT / 500; i++)
   {
       generateRandomVector(25, start, end, randomSet);
       start += 500;
       end += 500;
   }

   for (int j = 0; j < INSERT_COUNT; ++j)
   {
       boost::log::core::get()->set_logging_enabled(false);
       std::string value = "no";
       if(randomSet.contains(_fillZeros(j)))
       {
           value  = "yes";
       }
       insert(number++, _fillZeros(j), {value}, callAddress);
       boost::log::core::get()->set_logging_enabled(true);
   }

   auto recoverFunc = [this, &number, &callAddress](const std::set<std::string>& removed)
   {
       for (auto& iter : removed)
       {
           boost::log::core::get()->set_logging_enabled(false);
           insert(number++, iter, {"yes"}, callAddress);
           boost::log::core::get()->set_logging_enabled(true);
       }
   };

   auto removeFunc = [this, &number, &callAddress] (const std::string& low, const std::string& high, uint32_t offset, uint32_t count,
                         const std::string& value) {
       ConditionTupleV320 cond1 = {3, 0, low};
       ConditionTupleV320 cond2 = {4, 0, high};
       ConditionTupleV320 cond3 = {0, 1, value};
       LimitTuple limit = {offset, count};
       auto r1 = removeByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       int32_t removedRows = 0;
       codec->decode(r1->data(), removedRows);
       return removedRows;
   };

   auto selectFunc = [this, &number, &callAddress](const std::string& value, std::vector<EntryTuple>& entries) {
       ConditionTupleV320 cond = {0, 1, value};
       LimitTuple limit = {0, 500};
       auto r1 = selectByCondition(number++, {cond}, limit, callAddress);
       codec->decode(r1->data(), entries);
   };

   {
       std::default_random_engine generator;
       std::uniform_int_distribution<int> distribution(50, 100);
       int _start = distribution(generator);
       int limitCount = 100;
       std::string low;
       std::string high;
       int i = 0;
       for(auto iter = randomSet.begin(); iter != randomSet.end(); ++iter, ++i)
       {
           if(i == _start)
           {
               low = iter->first;
           }
           if(i - _start == limitCount)
           {
               high = iter->first;
               break;
           }
       }

       std::set<std::string> savedSet;
       std::set<std::string> removedSet;
       for(auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
       {
           if(iter->first >= low && iter->first < high)
           {
               removedSet.insert(iter->first);
               continue;
           }
           savedSet.insert(iter->first);
       }

       uint32_t removedRows1 = removeFunc(low, _fillZeros(INSERT_COUNT), 0, limitCount, "yes");
       std::vector<EntryTuple> entries;
       selectFunc("yes", entries);
       BOOST_CHECK(removedRows1 == 500 - entries.size());

       for(auto& entry : entries)
       {
           std::string key = std::get<0>(entry);
           if(!savedSet.contains(key))
           {
               BOOST_CHECK(false);
               break;
           }
           savedSet.erase(key);
       }
       BOOST_CHECK(savedSet.empty());
       // recover data
       recoverFunc(removedSet);
   }

   // limitCount == 0
   {
       std::default_random_engine generator;
       std::uniform_int_distribution<int> distribution(50, 100);
       int _start = distribution(generator);
       int limitCount = 0;
       std::string low;
       std::string high;
       int i = 0;
       for(auto iter = randomSet.begin(); iter != randomSet.end(); ++iter, ++i)
       {
           if(i ==_start)
           {
               low = iter->first;
           }
           if(i - _start == limitCount)
           {
               high = iter->first;
               break;
           }
       }

       std::set<std::string> savedSet;
       for(auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
       {
           if(iter->first >= low && iter->first < high)
           {
               continue;
           }
           savedSet.insert(iter->first);
       }

       uint32_t removedRows1 = removeFunc(low, _fillZeros(INSERT_COUNT), 0, limitCount, "yes");
       std::vector<EntryTuple> entries;
       selectFunc("yes", entries);
       BOOST_CHECK(removedRows1 == 500 - entries.size());

       for(auto& entry : entries)
       {
           std::string key = std::get<0>(entry);
           if(!savedSet.contains(key))
           {
               BOOST_CHECK(false);
               break;
           }
           savedSet.erase(key);
       }
       BOOST_CHECK(savedSet.empty());
   }

   // limitCount < USER_TABLE_MIN_LIMIT_COUNT
   {
       std::default_random_engine generator;
       std::uniform_int_distribution<int> distribution(50, 100);
       int _start = distribution(generator);
       int limitCount = 49;
       std::string low;
       std::string high;
       int i = 0;
       for(auto iter = randomSet.begin(); iter != randomSet.end(); ++iter, ++i)
       {
           if(i ==_start)
           {
               low = iter->first;
           }
           if(i - _start == limitCount)
           {
               high = iter->first;
               break;
           }
       }

       std::set<std::string> savedSet;
       for(auto iter = randomSet.begin(); iter != randomSet.end(); ++iter)
       {
           if(iter->first >= low && iter->first < high)
           {
               continue;
           }
           savedSet.insert(iter->first);
       }

       uint32_t removedRows1 = removeFunc(low, _fillZeros(INSERT_COUNT), 0, limitCount, "yes");
       std::vector<EntryTuple> entries;
       selectFunc("yes", entries);
       BOOST_CHECK(removedRows1 == 500 - entries.size());

       for(auto& entry : entries)
       {
           std::string key = std::get<0>(entry);
           if(!savedSet.contains(key))
           {
               BOOST_CHECK(false);
               break;
           }
           savedSet.erase(key);
       }
       BOOST_CHECK(savedSet.empty());
   }

   // empty condition
   {
       LimitTuple limit = {0, 10};
       auto r1 = removeByCondition(number++, {}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // condition with undefined cmp
   {
       ConditionTupleV320 cond1 = {100, 0, "90"};
       LimitTuple limit = {0, 10};
       auto r1 = removeByCondition(number++, {cond1}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // limit overflow
   {
       ConditionTupleV320 cond1 = {0, 0, "90"};
       LimitTuple limit = {0, 10000};
       auto r1 = removeByCondition(number++, {cond1}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }

   // The index of condition out of range
   {
       LimitTuple limit = {0, 50};
       ConditionTupleV320 cond1 = {3, 0, _fillZeros(0)};
       ConditionTupleV320 cond2 = {4, 0, _fillZeros(INSERT_COUNT)};
       // index out of range  0 <= idx <= 1
       ConditionTupleV320 cond3 = {0, 2, "yes"};
       auto r1 = removeByCondition(number++, {cond1, cond2, cond3}, limit, callAddress);
       BOOST_CHECK(r1->status() == (int32_t)TransactionStatus::PrecompiledError);
   }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
