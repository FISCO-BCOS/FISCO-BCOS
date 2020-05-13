/*
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
 */

/**
 * @brief: unit test for CommonTransactionNonceCheck
 * @file: CommonTransactionNonceCheck.cpp
 * @author: jimmyshi
 * @date: 2019-05-29
 */
#include "FakeBlockChain.h"
#include <libdevcrypto/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
using namespace dev;
using namespace dev::txpool;
using namespace dev::blockchain;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(CommonTransactionNonceCheckTest, TestOutputHelperFixture)

/// fake single transaction
Transaction fakeTransaction(size_t _idx = 0)
{
    u256 value = u256(100);
    u256 gas = u256(100000000);
    u256 gasPrice = u256(0);
    Address dst;
    std::string str =
        "test transaction for CommonTransactionNonceCheck" + std::to_string(utcTime());
    bytes data(str.begin(), str.end());
    u256 const& nonce = u256(utcTime() + _idx);
    Transaction fakeTx(value, gasPrice, gas, dst, data, nonce);

    auto keyPair = KeyPair::create();
    std::shared_ptr<crypto::Signature> sig =
        dev::crypto::Sign(keyPair, fakeTx.sha3(WithoutSignature));
    /// update the signature of transaction
    fakeTx.updateSignature(sig);
    return fakeTx;
}


BOOST_AUTO_TEST_CASE(updateTest)
{
    // Generate txs
    Transactions txs;
    for (size_t i = 0; i < 2; i++)
    {
        std::shared_ptr<Transaction> p_tx = std::make_shared<Transaction>(fakeTransaction(i));
        txs.emplace_back(p_tx);
    }

    CommonTransactionNonceCheck cache;

    BOOST_CHECK(cache.isNonceOk(*txs[0]));

    cache.insertCache(*txs[0]);
    BOOST_CHECK(!cache.isNonceOk(*txs[0]));

    BOOST_CHECK(cache.isNonceOk(*txs[1]));
    BOOST_CHECK(cache.isNonceOk(*txs[1], true));  // insert cache if true
    BOOST_CHECK(!cache.isNonceOk(*txs[1]));

    dev::eth::NonceKeyType nonce = txs[0]->nonce();
    cache.delCache(nonce);
    BOOST_CHECK(cache.isNonceOk(*txs[0]));

    cache.delCache(txs);
    BOOST_CHECK(cache.isNonceOk(*txs[1]));
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
