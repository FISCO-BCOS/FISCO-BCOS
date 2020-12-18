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
 * (c) 2016-2020 fisco-dev contributors.
 */

/**
 * @brief: unit test for TxGeneratorTest
 * @file: TxGeneratorTest.cpp
 * @author: yujiechen
 * @date: 2020-6-03
 */
#include "Common.h"
#include <libconsensus/TxGenerator.h>
#include <libprecompiled/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::consensus;
using namespace bcos::crypto;
using namespace bcos::protocol;
using namespace bcos::precompiled;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(TxGeneratorTest, TestOutputHelperFixture)

void checkTxField(Transaction::Ptr _orgTx, Transaction::Ptr _decodedTx)
{
    BOOST_CHECK(_orgTx->nonce() == _decodedTx->nonce());
    BOOST_CHECK(_orgTx->value() == _decodedTx->value());
    BOOST_CHECK(_orgTx->receiveAddress() == _decodedTx->receiveAddress());
    BOOST_CHECK(_orgTx->gasPrice() == _decodedTx->gasPrice());
    BOOST_CHECK(_orgTx->gas() == _decodedTx->gas());
    BOOST_CHECK(_orgTx->sender() == _decodedTx->sender());
    BOOST_CHECK(_orgTx->data() == _decodedTx->data());
    BOOST_CHECK(_orgTx->chainId() == _orgTx->chainId());
    BOOST_CHECK(_orgTx->groupId() == _orgTx->groupId());
    BOOST_CHECK(_orgTx->extraData() == _orgTx->extraData());
    BOOST_CHECK(_orgTx->type() == _orgTx->type());
}

BOOST_AUTO_TEST_CASE(testGenerateTransaction)
{
    std::string interface =
        "rotateWorkingSealer(std::vector<uint8_t>,std::vector<uint8_t>,std::vector<uint8_t>)";
    std::string vrfProof = "abcd";
    std::string vrfPk = "bcde";
    std::string vrfInput = "addtestsd";
    BlockNumber currentNumber = 100;
    unsigned blockLimit = 1000;
    Address to = Address("0x1002");
    auto keyPair = KeyPair::create();

    // get selector for "rotateWorkingSealer"
    auto selector = getFuncSelectorByFunctionName(interface);

    std::shared_ptr<TxGenerator> txGenerator = std::make_shared<TxGenerator>(1, 1, blockLimit);
    auto generatedTx = txGenerator->generateTransactionWithSig(interface, currentNumber, to,
        keyPair, bcos::protocol::Transaction::Type::MessageCall, vrfProof, vrfPk, vrfInput);

    // encode the transaction
    std::shared_ptr<bcos::bytes> encodedTxData = std::make_shared<bcos::bytes>();
    generatedTx->encode(*encodedTxData);

    // construct transaction from the encodedTxData
    std::shared_ptr<Transaction> decodedTx;
    BOOST_CHECK_NO_THROW(
        decodedTx = std::make_shared<Transaction>(*encodedTxData, CheckTransaction::Everything));

    // check fields for transacton
    checkTxField(decodedTx, generatedTx);

    // check value of decodedTx
    BOOST_CHECK(decodedTx->sender() == toAddress(keyPair.pub()));
    BOOST_CHECK(decodedTx->receiveAddress() == to);
    BOOST_CHECK(decodedTx->blockLimit() == currentNumber + blockLimit);
    BOOST_CHECK(decodedTx->type() == bcos::protocol::Transaction::Type::MessageCall);

    // parse the data
    // 1. check selector
    auto functionSelector = getParamFunc(ref(decodedTx->data()));
    BOOST_CHECK(functionSelector == selector);
    auto paramData = getParamData(ref(decodedTx->data()));
    // decode data and check params
    bcos::protocol::ContractABI abi;
    std::string decodedvrfProof;
    std::string decodedvrfPk;
    std::string decodedvrfInput;
    BOOST_CHECK_NO_THROW(abi.abiOut(paramData, decodedvrfProof, decodedvrfPk, decodedvrfInput));
    BOOST_CHECK(decodedvrfProof == vrfProof);
    BOOST_CHECK(decodedvrfPk == vrfPk);
    BOOST_CHECK(decodedvrfInput == vrfInput);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
