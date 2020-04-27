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
 * @brief : unitest for DAG(Directed Acyclic Graph) basic implement
 * @author: jimmyshi
 * @date: 2019-1-9
 */

#include <libblockverifier/TxDAG.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libethcore/Transaction.h>
#include <libprecompiled/ParallelConfigPrecompiled.h>
#include <libprecompiled/extension/DagTransferPrecompiled.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <set>

using namespace std;
using namespace dev;
using namespace dev::blockverifier;
using namespace dev::eth;

namespace dev
{
namespace test
{
class TxDAGFixture : TestOutputHelperFixture
{
public:
    TxDAGFixture() : TestOutputHelperFixture(){};

    Transaction::Ptr createParallelTransferTx(
        const string& _userFrom, const string& _userTo, u256 _money)
    {
        auto keyPair = KeyPair::create();
        u256 value = 0;
        u256 gasPrice = 0;
        u256 gas = 10000000;
        Address dest = Address(0x5002);  // DagTransfer precompile address

        dev::eth::ContractABI abi;
        bytes data = abi.abiIn("userTransfer(string,string,uint256)", _userFrom, _userTo,
            _money);  // add 1000000000 to user i
        u256 nonce = u256(utcTime() + rand());

        Transaction::Ptr tx =
            std::make_shared<Transaction>(value, gasPrice, gas, dest, data, nonce);
        tx->setBlockLimit(500);
        auto sig = dev::crypto::Sign(keyPair, tx->sha3(WithoutSignature));
        tx->updateSignature(sig);
        tx->forceSender(Address(0x2333));

        return tx;
    }

    Transaction::Ptr createNormalTx()
    {
        u256 value = 0;
        u256 gasPrice = 0;
        u256 gas = 10000000;
        Address dest = Address("2233445566778899");  // Normal transaction address

        dev::eth::ContractABI abi;
        bytes data = abi.abiIn("fake(string,string,uint256)", 0, 0, 0);
        u256 nonce = u256(utcTime() + rand());

        Transaction::Ptr tx =
            std::make_shared<Transaction>(value, gasPrice, gas, dest, data, nonce);
        tx->setBlockLimit(500);
        auto keyPair = KeyPair::create();
        auto sig = dev::crypto::Sign(keyPair, tx->sha3(WithoutSignature));
        tx->updateSignature(sig);
        tx->forceSender(Address(0x2333));

        return tx;
    }

    ExecutiveContext::Ptr createCtx()
    {
        ExecutiveContext::Ptr ctx = std::make_shared<ExecutiveContext>();
        ctx->setAddress2Precompiled(
            Address(0x5002), make_shared<dev::precompiled::DagTransferPrecompiled>());
        ctx->setAddress2Precompiled(
            Address(0x1006), make_shared<dev::precompiled::ParallelConfigPrecompiled>());
        return ctx;
    }

public:
    Secret sec = KeyPair::create().secret();
};

BOOST_FIXTURE_TEST_SUITE(TxDAGTest, TxDAGFixture)

BOOST_AUTO_TEST_CASE(PureParallelTxDAGTest)
{
    shared_ptr<TxDAG> txDag = make_shared<TxDAG>();
    ExecutiveContext::Ptr executiveContext = createCtx();

    std::shared_ptr<Transactions> trans = std::make_shared<Transactions>();
    trans->emplace_back(createParallelTransferTx("A", "B", 100));
    trans->emplace_back(createParallelTransferTx("C", "D", 100));
    trans->emplace_back(createParallelTransferTx("E", "F", 100));
    trans->emplace_back(createParallelTransferTx("A", "D", 100));
    trans->emplace_back(createParallelTransferTx("D", "F", 100));

    txDag->init(executiveContext, trans, 0);

    Transactions exeTrans;
    txDag->setTxExecuteFunc([&](Transaction::Ptr _tr, ID _txId, dev::executive::Executive::Ptr) {
        (void)_txId;
        exeTrans.emplace_back(_tr);
        return true;
    });

    dev::executive::Executive::Ptr executive = std::make_shared<dev::executive::Executive>();
    while (!txDag->hasFinished())
    {
        txDag->executeUnit(executive);
    }

    BOOST_CHECK_EQUAL(exeTrans[0]->sha3(), (*trans)[0]->sha3());
    BOOST_CHECK_EQUAL(exeTrans[1]->sha3(), (*trans)[1]->sha3());
    BOOST_CHECK_EQUAL(exeTrans[2]->sha3(), (*trans)[3]->sha3());
    BOOST_CHECK_EQUAL(exeTrans[3]->sha3(), (*trans)[2]->sha3());
    BOOST_CHECK_EQUAL(exeTrans[4]->sha3(), (*trans)[4]->sha3());
}


BOOST_AUTO_TEST_CASE(NormalAndParallelTxDAGTest)
{
    shared_ptr<TxDAG> txDag = make_shared<TxDAG>();
    ExecutiveContext::Ptr executiveContext = createCtx();

    std::shared_ptr<Transactions> trans = std::make_shared<Transactions>();
    trans->emplace_back(createParallelTransferTx("A", "B", 100));
    trans->emplace_back(createParallelTransferTx("C", "D", 100));
    trans->emplace_back(createParallelTransferTx("E", "F", 100));
    trans->emplace_back(createNormalTx());
    trans->emplace_back(createParallelTransferTx("A", "D", 100));
    trans->emplace_back(createParallelTransferTx("D", "F", 100));

    txDag->init(executiveContext, trans, 0);

    Transactions exeTrans;
    txDag->setTxExecuteFunc([&](Transaction::Ptr _tr, ID _txId, dev::executive::Executive::Ptr) {
        (void)_txId;
        exeTrans.emplace_back(_tr);
        return true;
    });

    dev::executive::Executive::Ptr executive = std::make_shared<dev::executive::Executive>();
    while (!txDag->hasFinished())
    {
        txDag->executeUnit(executive);
    }

    BOOST_CHECK_EQUAL(exeTrans[0]->sha3(), (*trans)[0]->sha3());
    BOOST_CHECK_EQUAL(exeTrans[1]->sha3(), (*trans)[1]->sha3());
    BOOST_CHECK_EQUAL(exeTrans[2]->sha3(), (*trans)[2]->sha3());
    BOOST_CHECK_EQUAL(exeTrans[3]->sha3(), (*trans)[3]->sha3());
    BOOST_CHECK_EQUAL(exeTrans[4]->sha3(), (*trans)[4]->sha3());
    BOOST_CHECK_EQUAL(exeTrans[5]->sha3(), (*trans)[5]->sha3());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev