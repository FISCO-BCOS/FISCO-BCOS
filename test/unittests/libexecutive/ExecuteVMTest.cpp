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
 * @author: jimmyshi
 * @date 2018-09-21
 */

#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/CommonJS.h>
#include <libdevcore/FixedHash.h>
#include <libethcore/Block.h>
#include <libethcore/Transaction.h>
#include <libexecutive/Executive.h>
#include <libexecutive/ExtVM.h>
#include <libmptstate/MPTState.h>
#include <libstorage/MemoryTableFactory.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <iostream>


using namespace std;
using namespace dev::eth;
using namespace dev::mptstate;
using namespace dev::executive;

namespace dev
{
namespace test
{
class TestLastBlockHashes : public eth::LastBlockHashesFace
{
public:
    explicit TestLastBlockHashes(h256s const& _hashes) : m_hashes(_hashes) {}

    h256s precedingHashes(h256 const& /* _mostRecentHash */) const override { return m_hashes; }
    void clear() override {}

private:
    h256s const m_hashes;
};

class ExecuteVMTestFixture : public TestOutputHelperFixture
{
public:
    static dev::h256 fakeCallBack(int64_t) { return h256(); }

    ExecuteVMTestFixture()
      : TestOutputHelperFixture(),
        m_mptStates(std::make_shared<MPTState>(
            u256(0), MPTState::openDB("./", h256("0x2234")), BaseState::Empty)),
        m_e(m_mptStates, initEnvInfo())
    {}

    EnvInfo initEnvInfo()
    {
        EnvInfo envInfo{fakeBlockHeader(), fakeCallBack, 0};
        blockverifier::ExecutiveContext::Ptr executiveContext =
            make_shared<blockverifier::ExecutiveContext>();
        executiveContext->setMemoryTableFactory(make_shared<storage::MemoryTableFactory>());
        envInfo.setPrecompiledEngine(executiveContext);
        return envInfo;
    }

    void executeTransaction(Transaction const& _transaction)
    {
        cout << "init" << endl;
        m_e.initialize(_transaction);
        cout << "execute" << endl;
        if (!m_e.execute())
        {
            cout << "go" << endl;
            m_e.go();
        }
        cout << "finalize" << endl;
        m_e.finalize();
    }

    std::shared_ptr<MPTState> m_mptStates;
    Executive m_e;

private:
    BlockHeader fakeBlockHeader()
    {
        BlockHeader fakeHeader;
        fakeHeader.setParentHash(sha3("parent"));
        fakeHeader.setRoots(sha3("transactionRoot"), sha3("receiptRoot"), sha3("stateRoot"));
        fakeHeader.setLogBloom(LogBloom(0));
        fakeHeader.setNumber(int64_t(0));
        fakeHeader.setGasLimit(u256(3000000000000000000));
        fakeHeader.setGasUsed(u256(1000000));
        uint64_t current_time = utcTime();
        fakeHeader.setTimestamp(current_time);
        fakeHeader.appendExtraDataArray(jsToBytes("0x1020"));
        fakeHeader.setSealer(u256("0x00"));
        std::vector<h512> sealer_list;
        for (unsigned int i = 0; i < 10; i++)
        {
            sealer_list.push_back(toPublic(Secret::random()));
        }
        fakeHeader.setSealerList(sealer_list);
        return fakeHeader;
    }

    TestLastBlockHashes fakeLastHashes()
    {
        return TestLastBlockHashes(h256s{h256("0xaaabbbccc"), h256("0xdddeeefff")});
    }
};

BOOST_FIXTURE_TEST_SUITE(ExecuteVMTest, ExecuteVMTestFixture)

BOOST_AUTO_TEST_CASE(DeployGetSetContractTest)
{
    /*
    pragma solidity ^0.4.2;
    contract HelloWorld{
        uint256 x;
        function HelloWorld(){
           x = 123;
        }
        function get()constant returns(uint256){
            return x;
        }
        function set(uint256 n){
            x = n;
        }
    }
    */

    // Deploy a contract
    u256 value = 0;
    u256 gasPrice = 0;
    u256 gas = 100000000;
    Address caller = Address("1000000000000000000000000000000000000000");
    bytes code = fromHex(
        string("608060405234801561001057600080fd5b50607b60008190555060df806100276000396000f30060806"
               "04052600436106049576000357c01000000000000000000000000000000000000000000000000000000"
               "00900463ffffffff16806360fe47b114604e5780636d4ce63c146078575b600080fd5b3480156059576"
               "00080fd5b5060766004803603810190808035906020019092919050505060a0565b005b348015608357"
               "600080fd5b50608a60aa565b6040518082815260200191505060405180910390f35b806000819055505"
               "0565b600080549050905600a165627a7a7230582093ef3ef61e120625973ff74daef914bf89008283e9"
               "c9937238f291c672adeb0d0029") +
        string(""));

    Transaction tx(value, gasPrice, gas, code);  // Use contract creation constructor
    tx.forceSender(caller);
    executeTransaction(tx);

    Address newAddress = m_e.newAddress();
    cout << "Contract created at: " << newAddress << endl;

    bytes createdCode = m_mptStates->code(newAddress);
    cout << "Created code: " << toHex(createdCode) << endl;

    bytes runtimeCode = fromHex(
        "6080604052600436106049576000357c0100000000000000000000000000000000000000000000000000000000"
        "900463ffffffff16806360fe47b114604e5780636d4ce63c146078575b600080fd5b348015605957600080fd5b"
        "5060766004803603810190808035906020019092919050505060a0565b005b348015608357600080fd5b50608a"
        "60aa565b6040518082815260200191505060405180910390f35b8060008190555050565b600080549050905600"
        "a165627a7a7230582093ef3ef61e120625973ff74daef914bf89008283e9c9937238f291c672adeb0d0029");

    BOOST_CHECK(runtimeCode == createdCode);

    // set()
    bytes callDataToSet =
        fromHex(string("0x60fe47b1") +  // set(0xaa)
                string("00000000000000000000000000000000000000000000000000000000000000aa"));
    Transaction setTx(value, gasPrice, gas, newAddress, callDataToSet);  // Use message call
                                                                         // constructor
    setTx.forceSender(caller);

    ExecutionResult setExeRes;
    m_e.setResultRecipient(setExeRes);
    executeTransaction(setTx);

    // get()
    bytes callDataToGet = fromHex(string("6d4ce63c") +  // get()
                                  string(""));

    Transaction getTx(value, gasPrice, gas, newAddress, callDataToGet);  // Use message call
                                                                         // constructor
    getTx.forceSender(caller);

    ExecutionResult getExeRes;
    m_e.setResultRecipient(getExeRes);
    executeTransaction(getTx);

    bytes compareName = fromHex("00000000000000000000000000000000000000000000000000000000000000aa");
    cout << "get() result: " << toHex(getExeRes.output) << endl;
    BOOST_CHECK(getExeRes.output == compareName);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
