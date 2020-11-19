/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief
 *
 * @file ExecuteVMTest.cpp
 * @author: xingqiangbai
 * @date 2018-10-25
 */

#include "libstoragestate/StorageState.h"
#include "../libstorage/MemoryStorage.h"
#include "libdevcrypto/CryptoInterface.h"
#include "libstorage/MemoryTableFactory2.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;

namespace test_StorageState
{
struct StorageStateFixture
{
    StorageStateFixture() : m_state(bcos::u256(0))
    {
        auto storage = std::make_shared<bcos::storage::MemoryStorage>();
        auto tableFactory = std::make_shared<bcos::storage::MemoryTableFactory2>();
        tableFactory->setStateStorage(storage);
        m_state.setMemoryTableFactory(tableFactory);
    }

    bcos::storagestate::StorageState m_state;
};

BOOST_FIXTURE_TEST_SUITE(StorageState, StorageStateFixture)

BOOST_AUTO_TEST_CASE(Balance)
{
    Address addr1(0x100001);
    Address addr2(0x100002);
    BOOST_TEST(m_state.balance(addr1) == u256(0));
    m_state.addBalance(addr1, u256(10));
    BOOST_TEST(m_state.balance(addr1) == u256(10));
    m_state.addBalance(addr1, u256(15));
    BOOST_TEST(m_state.balance(addr1) == u256(25));
    m_state.subBalance(addr1, u256(3));
    BOOST_TEST(m_state.balance(addr1) == u256(22));
    m_state.setBalance(addr1, u256(25));
    BOOST_TEST(m_state.balance(addr1) == u256(25));
    m_state.setBalance(addr2, u256(100));
    BOOST_TEST(m_state.balance(addr2) == u256(100));
    m_state.transferBalance(addr2, addr1, u256(55));
    BOOST_TEST(m_state.balance(addr2) == u256(45));
    BOOST_TEST(m_state.balance(addr1) == u256(80));
}

BOOST_AUTO_TEST_CASE(Account)
{
    Address addr1(0x100001);
    auto isUse = m_state.addressInUse(addr1);
    BOOST_TEST(isUse == false);
    m_state.checkAuthority(addr1, addr1);
    m_state.createContract(addr1);
    isUse = m_state.addressInUse(addr1);
    BOOST_TEST(isUse == true);
    auto balance = m_state.balance(addr1);
    BOOST_TEST(balance == u256(0));
    auto nonce = m_state.getNonce(addr1);
    BOOST_TEST(nonce == u256(0));
    auto hash = m_state.codeHash(addr1);
    BOOST_TEST(hash == EmptyHash);
    auto sign = m_state.accountNonemptyAndExisting(addr1);
    BOOST_TEST(sign == false);
    m_state.kill(addr1);
    m_state.rootHash();
    m_state.noteAccountStartNonce(u256(0));
    nonce = m_state.accountStartNonce();
    BOOST_TEST(nonce == u256(0));
    m_state.checkAuthority(addr1, addr1);
}

BOOST_AUTO_TEST_CASE(Storage)
{
    Address addr1(0x100001);
    m_state.addBalance(addr1, u256(10));
    m_state.storageRoot(addr1);
    auto value = m_state.storage(addr1, u256(123));
    BOOST_TEST(value == u256());
    m_state.setStorage(addr1, u256(123), u256(456));
    value = m_state.storage(addr1, u256(123));
    BOOST_TEST(value == u256(456));
    m_state.setStorage(addr1, u256(123), u256(567));
    m_state.clearStorage(addr1);
}

BOOST_AUTO_TEST_CASE(Code)
{
    Address addr1(0x100001);
    auto hasCode = m_state.addressHasCode(addr1);
    BOOST_TEST(hasCode == false);
    auto code = m_state.code(addr1);
    BOOST_TEST(code == NullBytes);
    auto hash = m_state.codeHash(addr1);
    BOOST_TEST(hash == EmptyHash);
    m_state.addBalance(addr1, u256(10));
    code = m_state.code(addr1);
    BOOST_TEST(code == NullBytes);
    std::string codeString("aaaaaaaaaaaaa");
    code = bytes(codeString.begin(), codeString.end());
    m_state.setCode(addr1, bytes(codeString.begin(), codeString.end()));
    auto code2 = m_state.code(addr1);
    BOOST_TEST(code == code2);
    hash = m_state.codeHash(addr1);
    BOOST_TEST(hash == crypto::Hash(code));
    auto size = m_state.codeSize(addr1);
    BOOST_TEST(code.size() == size);
    hasCode = m_state.addressHasCode(addr1);
    BOOST_TEST(hasCode == true);
}

BOOST_AUTO_TEST_CASE(Nonce)
{
    Address addr1(0x100001);
    m_state.addBalance(addr1, u256(10));
    auto nonce = m_state.getNonce(addr1);
    BOOST_TEST(nonce == m_state.accountStartNonce());
    m_state.setNonce(addr1, u256(5));
    nonce = m_state.getNonce(addr1);
    BOOST_TEST(nonce == u256(5));
    m_state.incNonce(addr1);
    nonce = m_state.getNonce(addr1);
    BOOST_TEST(nonce == u256(6));

    Address addr2(0x100002);
    m_state.incNonce(addr2);
    nonce = m_state.getNonce(addr2);
    BOOST_TEST(nonce == m_state.accountStartNonce() + 1);

    Address addr3(0x100003);
    m_state.setNonce(addr3, nonce);
    nonce = m_state.getNonce(addr3);
    BOOST_TEST(nonce == m_state.accountStartNonce() + 1);
}

BOOST_AUTO_TEST_CASE(Operate)
{
    Address addr1(0x100001);
    auto savepoint0 = m_state.savepoint();
    BOOST_TEST(m_state.balance(addr1) == u256(0));
    m_state.addBalance(addr1, u256(10));
    BOOST_TEST(m_state.balance(addr1) == u256(10));
    auto sign = m_state.accountNonemptyAndExisting(addr1);
    BOOST_TEST(sign == true);
    auto savepoint1 = m_state.savepoint();
    m_state.addBalance(addr1, u256(10));
    BOOST_TEST(m_state.balance(addr1) == u256(20));
    auto savepoint2 = m_state.savepoint();
    BOOST_TEST(savepoint1 < savepoint2);

    m_state.addBalance(addr1, u256(10));
    BOOST_TEST(m_state.balance(addr1) == u256(30));
    m_state.rollback(savepoint2);
    BOOST_TEST(m_state.balance(addr1) == u256(20));
    m_state.rollback(savepoint1);
    BOOST_TEST(m_state.balance(addr1) == u256(10));

    m_state.rollback(savepoint0);
    // BOOST_TEST(m_state.addressInUse(addr1) == false);
    m_state.dbCommit(h256(), 5u);
    m_state.clear();
    m_state.setRoot(h256());

    m_state.commit();
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_StorageState
