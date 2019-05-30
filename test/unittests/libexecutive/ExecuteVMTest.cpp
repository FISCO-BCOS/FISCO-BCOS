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
            u256(0), MPTState::openDB("./", h256("0x2234")), BaseState::Empty))
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

    void executeTransaction(Executive& _e, Transaction const& _transaction)
    {
        cout << "init" << endl;
        _e.initialize(_transaction);
        cout << "execute" << endl;
        if (!_e.execute())
        {
            cout << "go" << endl;
            _e.go();
        }
        cout << "finalize" << endl;
        _e.finalize();
    }

    std::shared_ptr<MPTState> m_mptStates;

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
    function getByCall()constant returns(uint256){
        return HelloWorld(this).get();
    }
}
    */

    // Deploy a contract
    u256 value = 0;
    u256 gasPrice = 0;
    u256 gas = 100000000;
    Address caller = Address("1000000000000000000000000000000000000000");
    bytes code = fromHex(
        string("608060405234801561001057600080fd5b50607b6000819055506101c4806100286000396000f300608"
               "060405260043610610057576000357c0100000000000000000000000000000000000000000000000000"
               "000000900463ffffffff16806360fe47b11461005c5780636d4ce63c14610089578063b737973314610"
               "0b4575b600080fd5b34801561006857600080fd5b506100876004803603810190808035906020019092"
               "91905050506100df565b005b34801561009557600080fd5b5061009e6100e9565b60405180828152602"
               "00191505060405180910390f35b3480156100c057600080fd5b506100c96100f2565b60405180828152"
               "60200191505060405180910390f35b8060008190555050565b60008054905090565b60003073fffffff"
               "fffffffffffffffffffffffffffffffff16636d4ce63c6040518163ffffffff167c0100000000000000"
               "000000000000000000000000000000000000000000028152600401602060405180830381600087803b1"
               "5801561015857600080fd5b505af115801561016c573d6000803e3d6000fd5b505050506040513d6020"
               "81101561018257600080fd5b81019080805190602001909291905050509050905600a165627a7a72305"
               "820b0ddb009a501f80430e5ec6de7e52c2a7dfa838141b3706ada9d91359e3f22c80029") +
        string(""));

    Executive e0(m_mptStates, initEnvInfo());
    Transaction tx(value, gasPrice, gas, code);  // Use contract creation constructor
    tx.forceSender(caller);
    executeTransaction(e0, tx);

    Address newAddress = e0.newAddress();
    cout << "Contract created at: " << newAddress << endl;

    bytes createdCode = m_mptStates->code(newAddress);
    cout << "Created code: " << toHex(createdCode) << endl;

    bytes runtimeCode = fromHex(
        "608060405260043610610057576000357c01000000000000000000000000000000000000000000000000000000"
        "00900463ffffffff16806360fe47b11461005c5780636d4ce63c14610089578063b7379733146100b4575b6000"
        "80fd5b34801561006857600080fd5b50610087600480360381019080803590602001909291905050506100df56"
        "5b005b34801561009557600080fd5b5061009e6100e9565b6040518082815260200191505060405180910390f3"
        "5b3480156100c057600080fd5b506100c96100f2565b6040518082815260200191505060405180910390f35b80"
        "60008190555050565b60008054905090565b60003073ffffffffffffffffffffffffffffffffffffffff16636d"
        "4ce63c6040518163ffffffff167c01000000000000000000000000000000000000000000000000000000000281"
        "52600401602060405180830381600087803b15801561015857600080fd5b505af115801561016c573d6000803e"
        "3d6000fd5b505050506040513d602081101561018257600080fd5b810190808051906020019092919050505090"
        "50905600a165627a7a72305820b0ddb009a501f80430e5ec6de7e52c2a7dfa838141b3706ada9d91359e3f22c8"
        "0029");

    BOOST_CHECK(runtimeCode == createdCode);

    // set()
    bytes callDataToSet =
        fromHex(string("0x60fe47b1") +  // set(0xaa)
                string("00000000000000000000000000000000000000000000000000000000000000aa"));
    Transaction setTx(value, gasPrice, gas, newAddress, callDataToSet);  // Use message call
                                                                         // constructor
    setTx.forceSender(caller);

    Executive e1(m_mptStates, initEnvInfo());
    ExecutionResult setExeRes;
    e1.setResultRecipient(setExeRes);
    executeTransaction(e1, setTx);

    // get()
    bytes callDataToGet = fromHex(string("6d4ce63c") +  // get()
                                  string(""));

    Transaction getTx(value, gasPrice, gas, newAddress, callDataToGet);  // Use message call
                                                                         // constructor
    getTx.forceSender(caller);

    Executive e2(m_mptStates, initEnvInfo());
    ExecutionResult getExeRes;
    e2.setResultRecipient(getExeRes);
    executeTransaction(e2, getTx);

    bytes compareName = fromHex("00000000000000000000000000000000000000000000000000000000000000aa");
    cout << "get() result: " << toHex(getExeRes.output) << endl;
    BOOST_CHECK(getExeRes.output == compareName);

    // getByCall()
    bytes callDataToGetByCall = fromHex(string("b7379733") +  // getByCall()
                                        string(""));

    Transaction getByCallTx(value, gasPrice, gas, newAddress, callDataToGetByCall);  // Use message
                                                                                     // call
                                                                                     // constructor
    getByCallTx.forceSender(caller);

    Executive e3(m_mptStates, initEnvInfo());
    ExecutionResult getExeResByCall;
    e3.setResultRecipient(getExeResByCall);
    executeTransaction(e3, getByCallTx);

    cout << "getByCall() result: " << toHex(getExeResByCall.output) << endl;
    BOOST_CHECK(getExeResByCall.output == compareName);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
