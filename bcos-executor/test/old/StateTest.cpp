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
 * @brief the test of State
 * @file StateTest.cpp
 */

#include "state/State.h"
#include "MemoryStorage.h"
#include "bcos-framework/libtable/StateStorage.h"
#include "bcos-framework/testutils/crypto/HashImpl.h"
#include "bcos-framework/testutils/crypto/SignatureImpl.h"
#include "bcos-protocol/protobuf/PBBlock.h"
#include "bcos-protocol/protobuf/PBBlockFactory.h"
#include "bcos-protocol/protobuf/PBBlockHeaderFactory.h"
#include "bcos-protocol/protobuf/PBTransactionFactory.h"
#include "bcos-protocol/protobuf/PBTransactionReceiptFactory.h"
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace bcos;
using namespace bcos::storage;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::executor;

namespace bcos
{
namespace test
{
namespace test_StorageState
{
struct StorageStateFixture
{
    StorageStateFixture()
    {
        hashImpl = std::make_shared<Sm3Hash>();
        memoryStorage = make_shared<MemoryStorage>();
        tableFactory = make_shared<StateStorage>(memoryStorage, hashImpl, m_blockNumber);
        state = make_shared<State>(tableFactory, hashImpl, false);
    }

    std::shared_ptr<crypto::Hash> hashImpl = nullptr;
    std::shared_ptr<StorageInterface> memoryStorage = nullptr;
    protocol::BlockNumber m_blockNumber = 0;
    std::shared_ptr<StateStorage> tableFactory = nullptr;
    std::shared_ptr<State> state = nullptr;
};

BOOST_FIXTURE_TEST_SUITE(StorageState, StorageStateFixture)

BOOST_AUTO_TEST_CASE(Balance)
{
    string addr1("0x100001");
    string addr2("0x100002");
    BOOST_TEST(state->balance(addr1) == u256(0));
    state->addBalance(addr1, u256(10));
    BOOST_TEST(state->balance(addr1) == u256(10));
    state->addBalance(addr1, u256(15));
    BOOST_TEST(state->balance(addr1) == u256(25));
    state->subBalance(addr1, u256(3));
    BOOST_TEST(state->balance(addr1) == u256(22));
    state->setBalance(addr1, u256(25));
    BOOST_TEST(state->balance(addr1) == u256(25));
    state->setBalance(addr2, u256(100));
    BOOST_TEST(state->balance(addr2) == u256(100));
    state->transferBalance(addr2, addr1, u256(55));
    BOOST_TEST(state->balance(addr2) == u256(45));
    BOOST_TEST(state->balance(addr1) == u256(80));
}

BOOST_AUTO_TEST_CASE(Account)
{
    string addr1("0x100001");
    auto isUse = state->addressInUse(addr1);
    BOOST_TEST(isUse == false);
    state->checkAuthority(addr1, addr1);
    state->createContract(addr1);
    isUse = state->addressInUse(addr1);
    BOOST_TEST(isUse == true);
    auto balance = state->balance(addr1);
    BOOST_TEST(balance == u256(0));
    auto nonce = state->getNonce(addr1);
    BOOST_TEST(nonce == u256(0));
    auto hash = state->codeHash(addr1);
    BOOST_TEST(hash == hashImpl->emptyHash());
    auto sign = state->accountNonemptyAndExisting(addr1);
    BOOST_TEST(sign == false);
    state->kill(addr1);
    state->rootHash();
    nonce = state->accountStartNonce();
    BOOST_TEST(nonce == u256(0));
    state->checkAuthority(addr1, addr1);
}

BOOST_AUTO_TEST_CASE(Storage)
{
    string addr1("0x100001");
    state->addBalance(addr1, u256(10));
    state->storageRoot(addr1);
    auto value = state->storage(addr1, "123");
    BOOST_TEST(value == "");
    state->setStorage(addr1, "123", "456");
    value = state->storage(addr1, "123");
    BOOST_TEST(value == "456");
    state->setStorage(addr1, "123", "567");
    state->clearStorage(addr1);
}

BOOST_AUTO_TEST_CASE(Code)
{
    string addr1("0x100001");
    auto hasCode = state->addressHasCode(addr1);
    BOOST_TEST(hasCode == false);
    auto code = state->code(addr1);
    BOOST_TEST(code == nullptr);
    auto hash = state->codeHash(addr1);
    BOOST_TEST(hash == hashImpl->emptyHash());
    state->addBalance(addr1, u256(10));
    code = state->code(addr1);
    BOOST_TEST(code == nullptr);
    std::string codeString("aaaaaaaaaaaaa");
    auto codeBytes = bytes(codeString.begin(), codeString.end());
    state->setCode(addr1, bytesConstRef(codeBytes.data(), codeBytes.size()));
    auto code2 = state->code(addr1);
    BOOST_TEST(codeBytes == *code2);
    hash = state->codeHash(addr1);
    BOOST_TEST(hash == hashImpl->hash(codeBytes));
    auto size = state->codeSize(addr1);
    BOOST_TEST(codeBytes.size() == size);
    hasCode = state->addressHasCode(addr1);
    BOOST_TEST(hasCode == true);
}

BOOST_AUTO_TEST_CASE(Nonce)
{
    string addr1("0x100001");
    state->addBalance(addr1, u256(10));
    auto nonce = state->getNonce(addr1);
    BOOST_TEST(nonce == state->accountStartNonce());
    state->setNonce(addr1, u256(5));
    nonce = state->getNonce(addr1);
    BOOST_TEST(nonce == u256(5));
    state->incNonce(addr1);
    nonce = state->getNonce(addr1);
    BOOST_TEST(nonce == u256(6));
    state->incNonce(addr1);
    nonce = state->getNonce(addr1);
    BOOST_TEST(nonce == u256(7));
    state->incNonce(addr1);
    nonce = state->getNonce(addr1);
    BOOST_TEST(nonce == u256(8));
    state->incNonce(addr1);
    nonce = state->getNonce(addr1);
    BOOST_TEST(nonce == u256(9));

    string addr2("0x100002");
    state->incNonce(addr2);
    nonce = state->getNonce(addr2);
    BOOST_TEST(nonce == state->accountStartNonce() + 1);
    state->incNonce(addr2);
    nonce = state->getNonce(addr2);
    BOOST_TEST(nonce == state->accountStartNonce() + 2);
    state->incNonce(addr2);
    nonce = state->getNonce(addr2);
    BOOST_TEST(nonce == state->accountStartNonce() + 3);
    state->incNonce(addr2);
    nonce = state->getNonce(addr2);
    BOOST_TEST(nonce == state->accountStartNonce() + 4);

    string addr3("0x100003");
    state->setNonce(addr3, nonce);
    nonce = state->getNonce(addr3);
    BOOST_TEST(nonce == state->accountStartNonce() + 4);
}

BOOST_AUTO_TEST_CASE(Operate)
{
    string addr1("0x100001");
    auto savepoint0 = state->savepoint();
    BOOST_TEST(state->balance(addr1) == u256(0));
    state->addBalance(addr1, u256(10));
    BOOST_TEST(state->balance(addr1) == u256(10));
    auto sign = state->accountNonemptyAndExisting(addr1);
    BOOST_TEST(sign == true);
    auto savepoint1 = state->savepoint();
    state->addBalance(addr1, u256(10));
    BOOST_TEST(state->balance(addr1) == u256(20));
    auto savepoint2 = state->savepoint();
    BOOST_TEST(savepoint1 < savepoint2);

    state->addBalance(addr1, u256(10));
    BOOST_TEST(state->balance(addr1) == u256(30));
    state->rollback(savepoint2);
    BOOST_TEST(state->balance(addr1) == u256(20));
    state->rollback(savepoint1);
    BOOST_TEST(state->balance(addr1) == u256(10));

    state->rollback(savepoint0);
    // BOOST_TEST(state->addressInUse(addr1) == false);
    state->commit();
    state->clear();

    state->commit();
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_StorageState

}  // namespace test
}  // namespace bcos
