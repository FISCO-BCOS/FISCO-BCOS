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
#include <libexecutive/EVMHostContext.h>
#include <libexecutive/Executive.h>
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

    EnvInfo initEnvInfo(uint64_t _gasLimit)
    {
        EnvInfo envInfo{fakeBlockHeader(), fakeCallBack, 0};
        blockverifier::ExecutiveContext::Ptr executiveContext =
            make_shared<blockverifier::ExecutiveContext>();
        executiveContext->setTxGasLimit(_gasLimit);
        executiveContext->setMemoryTableFactory(make_shared<storage::MemoryTableFactory>());
        envInfo.setPrecompiledEngine(executiveContext);
        return envInfo;
    }

    void executeTransaction(Executive& _e, Transaction const& _transaction)
    {
        std::shared_ptr<Transaction> tx = std::make_shared<Transaction>(_transaction);
        cout << "init" << endl;
        _e.initialize(tx);
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
        fakeHeader.setParentHash(crypto::Hash("parent"));
        fakeHeader.setRoots(crypto::Hash("transactionRoot"), crypto::Hash("receiptRoot"),
            crypto::Hash("stateRoot"));
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
    function destroy()
    {
        selfdestruct(0x0);
    }
}
    */

    // Deploy a contract
    u256 value = 0;
    u256 gasPrice = 0;
    u256 gas = 100000000;
    Address caller = Address("1000000000000000000000000000000000000000");
    bytes code = fromHex(
        string("608060405234801561001057600080fd5b50607b6000819055506101ea806100286000396000f300608"
               "060405260043610610062576000357c0100000000000000000000000000000000000000000000000000"
               "000000900463ffffffff16806360fe47b1146100675780636d4ce63c1461009457806383197ef014610"
               "0bf578063b7379733146100d6575b600080fd5b34801561007357600080fd5b50610092600480360381"
               "01908080359060200190929190505050610101565b005b3480156100a057600080fd5b506100a961010"
               "b565b6040518082815260200191505060405180910390f35b3480156100cb57600080fd5b506100d461"
               "0114565b005b3480156100e257600080fd5b506100eb610118565b60405180828152602001915050604"
               "05180910390f35b8060008190555050565b60008054905090565b6000ff5b60003073ffffffffffffff"
               "ffffffffffffffffffffffffff16636d4ce63c6040518163ffffffff167c01000000000000000000000"
               "00000000000000000000000000000000000028152600401602060405180830381600087803b15801561"
               "017e57600080fd5b505af1158015610192573d6000803e3d6000fd5b505050506040513d60208110156"
               "101a857600080fd5b81019080805190602001909291905050509050905600a165627a7a723058207eb7"
               "0dc4a752ca8296fc60a702b8fba26d66ed4ac38c8801fe7eff92664b671e0029") +
        string(""));

    Executive e0(m_mptStates, initEnvInfo());
    Transaction tx(value, gasPrice, gas, code);  // Use contract creation constructor
    auto keyPair = KeyPair::create();
    auto sig = dev::crypto::Sign(keyPair, tx.sha3(WithoutSignature));
    tx.updateSignature(sig);
    tx.forceSender(caller);
    executeTransaction(e0, tx);

    Address newAddress = e0.newAddress();
    cout << "Contract created at: " << newAddress << endl;

    bytes createdCode = m_mptStates->code(newAddress);
    cout << "Created code: " << toHex(createdCode) << endl;

    bytes runtimeCode = fromHex(
        "608060405260043610610062576000357c01000000000000000000000000000000000000000000000000000000"
        "00900463ffffffff16806360fe47b1146100675780636d4ce63c1461009457806383197ef0146100bf578063b7"
        "379733146100d6575b600080fd5b34801561007357600080fd5b50610092600480360381019080803590602001"
        "90929190505050610101565b005b3480156100a057600080fd5b506100a961010b565b60405180828152602001"
        "91505060405180910390f35b3480156100cb57600080fd5b506100d4610114565b005b3480156100e257600080"
        "fd5b506100eb610118565b6040518082815260200191505060405180910390f35b8060008190555050565b6000"
        "8054905090565b6000ff5b60003073ffffffffffffffffffffffffffffffffffffffff16636d4ce63c60405181"
        "63ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401602060"
        "405180830381600087803b15801561017e57600080fd5b505af1158015610192573d6000803e3d6000fd5b5050"
        "50506040513d60208110156101a857600080fd5b81019080805190602001909291905050509050905600a16562"
        "7a7a723058207eb70dc4a752ca8296fc60a702b8fba26d66ed4ac38c8801fe7eff92664b671e0029");

    BOOST_CHECK(runtimeCode == createdCode);

    // set()
    bytes callDataToSet =
        fromHex(string("0x60fe47b1") +  // set(0xaa)
                string("00000000000000000000000000000000000000000000000000000000000000aa"));
    Transaction setTx(value, gasPrice, gas, newAddress, callDataToSet);
    sig = dev::crypto::Sign(keyPair, setTx.sha3(WithoutSignature));
    setTx.updateSignature(sig);
    setTx.forceSender(caller);

    Executive e1(m_mptStates, initEnvInfo());
    executeTransaction(e1, setTx);

    // get()
    bytes callDataToGet = fromHex(string("6d4ce63c") +  // get()
                                  string(""));

    Transaction getTx(value, gasPrice, gas, newAddress, callDataToGet);
    sig = dev::crypto::Sign(keyPair, getTx.sha3(WithoutSignature));
    getTx.updateSignature(sig);
    getTx.forceSender(caller);

    Executive e2(m_mptStates, initEnvInfo());
    executeTransaction(e2, getTx);

    bytes compareName = fromHex("00000000000000000000000000000000000000000000000000000000000000aa");
    // cout << "get() result: " << toHex(getExeRes.output) << endl;
    // BOOST_CHECK(getExeRes.output == compareName);

    // getByCall()
    bytes callDataToGetByCall = fromHex(string("b7379733") +  // getByCall()
                                        string(""));

    Transaction getByCallTx(value, gasPrice, gas, newAddress, callDataToGetByCall);
    sig = dev::crypto::Sign(keyPair, getByCallTx.sha3(WithoutSignature));
    getByCallTx.updateSignature(sig);
    getByCallTx.forceSender(caller);

    Executive e3(m_mptStates, initEnvInfo());
    executeTransaction(e3, getByCallTx);

    // cout << "getByCall() result: " << toHex(getExeResByCall.output) << endl;
    // BOOST_CHECK(getExeResByCall.output == compareName);

    // destroy
    bytes callDestroy = fromHex(string("83197ef0") +  // destroy()
                                string(""));

    Transaction destroyTx(value, gasPrice, gas, newAddress, callDestroy);
    sig = dev::crypto::Sign(keyPair, destroyTx.sha3(WithoutSignature));
    destroyTx.updateSignature(sig);
    destroyTx.forceSender(caller);

    Executive e4(m_mptStates, initEnvInfo());
    executeTransaction(e4, destroyTx);
    BOOST_CHECK(!m_mptStates->addressHasCode(newAddress));
}

BOOST_AUTO_TEST_CASE(OutOfGasIntrinsicTest)
{
    u256 value = 0;
    u256 gasPrice = 0;
    u256 gas = 10;  // not enough gas
    Address caller = Address("1000000000000000000000000000000000000000");
    bytes code(692, 1);  // 100000 =?= 53000 + non-zero * 68 + zero * 4

    Executive e(m_mptStates, initEnvInfo(100000));  // set gas limit to little
    Transaction tx(value, gasPrice, gas, code);     // Use contract creation constructor
    tx.forceSender(caller);
    BOOST_CHECK_THROW(executeTransaction(e, tx), OutOfGasIntrinsic);  // Throw in rc2 and later

    BOOST_CHECK_EQUAL(e.getException(), TransactionException::OutOfGasIntrinsic);
}

BOOST_AUTO_TEST_CASE(OutOfGasBaseTest)
{
    u256 value = 0;
    u256 gasPrice = 0;
    u256 gas = 10;  // given not enough gas
    Address caller = Address("1000000000000000000000000000000000000000");
    bytes code = fromHex(
        string("608060405234801561001057600080fd5b50607b6000819055506101ea806100286000396000f300608"
               "060405260043610610062576000357c0100000000000000000000000000000000000000000000000000"
               "000000900463ffffffff16806360fe47b1146100675780636d4ce63c1461009457806383197ef014610"
               "0bf578063b7379733146100d6575b600080fd5b34801561007357600080fd5b50610092600480360381"
               "01908080359060200190929190505050610101565b005b3480156100a057600080fd5b506100a961010"
               "b565b6040518082815260200191505060405180910390f35b3480156100cb57600080fd5b506100d461"
               "0114565b005b3480156100e257600080fd5b506100eb610118565b60405180828152602001915050604"
               "05180910390f35b8060008190555050565b60008054905090565b6000ff5b60003073ffffffffffffff"
               "ffffffffffffffffffffffffff16636d4ce63c6040518163ffffffff167c01000000000000000000000"
               "00000000000000000000000000000000000028152600401602060405180830381600087803b15801561"
               "017e57600080fd5b505af1158015610192573d6000803e3d6000fd5b505050506040513d60208110156"
               "101a857600080fd5b81019080805190602001909291905050509050905600a165627a7a723058207eb7"
               "0dc4a752ca8296fc60a702b8fba26d66ed4ac38c8801fe7eff92664b671e0029") +
        string(""));

    Executive e(m_mptStates, initEnvInfo());
    Transaction tx(value, gasPrice, gas, code);  // Use contract creation constructor
    tx.forceSender(caller);
    if (g_BCOSConfig.version() <= RC3_VERSION)
    {
        BOOST_CHECK_THROW(executeTransaction(e, tx), OutOfGasBase);
        BOOST_CHECK_EQUAL(e.getException(), TransactionException::OutOfGasBase);
    }
    else
    {
        BOOST_CHECK_NO_THROW(executeTransaction(e, tx));
    }
}

BOOST_AUTO_TEST_CASE(CallAddressErrorTest)
{
    // RC2+ only
    u256 value = 0;
    u256 gasPrice = 0;
    u256 gas = 100000000;
    Address caller = Address("1000000000000000000000000000000000000000");
    Address addr = Address(0x4fff);  // non exist address
    bytes inputs = fromHex("0x0");
    Transaction setTx(value, gasPrice, gas, addr, inputs);
    setTx.forceSender(caller);

    Executive e(m_mptStates, initEnvInfo());
    executeTransaction(e, setTx);

    if (g_BCOSConfig.version() >= RC2_VERSION)
    {
        BOOST_CHECK_EQUAL(e.getException(), TransactionException::CallAddressError);
    }
}

BOOST_AUTO_TEST_CASE(DeployGetSetContractTestRC1)
{
    auto version = g_BCOSConfig.version();
    auto supportedVersion = g_BCOSConfig.supportedVersion();
    g_BCOSConfig.setSupportedVersion("2.0.0-rc1", RC1_VERSION);
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
    function destroy()
    {
        selfdestruct(0x0);
    }
}
    */

    // Deploy a contract
    u256 value = 0;
    u256 gasPrice = 0;
    u256 gas = 100000000;
    Address caller = Address("1000000000000000000000000000000000000000");
    bytes code = fromHex(
        string("608060405234801561001057600080fd5b50607b6000819055506101ea806100286000396000f300608"
               "060405260043610610062576000357c0100000000000000000000000000000000000000000000000000"
               "000000900463ffffffff16806360fe47b1146100675780636d4ce63c1461009457806383197ef014610"
               "0bf578063b7379733146100d6575b600080fd5b34801561007357600080fd5b50610092600480360381"
               "01908080359060200190929190505050610101565b005b3480156100a057600080fd5b506100a961010"
               "b565b6040518082815260200191505060405180910390f35b3480156100cb57600080fd5b506100d461"
               "0114565b005b3480156100e257600080fd5b506100eb610118565b60405180828152602001915050604"
               "05180910390f35b8060008190555050565b60008054905090565b6000ff5b60003073ffffffffffffff"
               "ffffffffffffffffffffffffff16636d4ce63c6040518163ffffffff167c01000000000000000000000"
               "00000000000000000000000000000000000028152600401602060405180830381600087803b15801561"
               "017e57600080fd5b505af1158015610192573d6000803e3d6000fd5b505050506040513d60208110156"
               "101a857600080fd5b81019080805190602001909291905050509050905600a165627a7a723058207eb7"
               "0dc4a752ca8296fc60a702b8fba26d66ed4ac38c8801fe7eff92664b671e0029") +
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
        "608060405260043610610062576000357c01000000000000000000000000000000000000000000000000000000"
        "00900463ffffffff16806360fe47b1146100675780636d4ce63c1461009457806383197ef0146100bf578063b7"
        "379733146100d6575b600080fd5b34801561007357600080fd5b50610092600480360381019080803590602001"
        "90929190505050610101565b005b3480156100a057600080fd5b506100a961010b565b60405180828152602001"
        "91505060405180910390f35b3480156100cb57600080fd5b506100d4610114565b005b3480156100e257600080"
        "fd5b506100eb610118565b6040518082815260200191505060405180910390f35b8060008190555050565b6000"
        "8054905090565b6000ff5b60003073ffffffffffffffffffffffffffffffffffffffff16636d4ce63c60405181"
        "63ffffffff167c0100000000000000000000000000000000000000000000000000000000028152600401602060"
        "405180830381600087803b15801561017e57600080fd5b505af1158015610192573d6000803e3d6000fd5b5050"
        "50506040513d60208110156101a857600080fd5b81019080805190602001909291905050509050905600a16562"
        "7a7a723058207eb70dc4a752ca8296fc60a702b8fba26d66ed4ac38c8801fe7eff92664b671e0029");

    BOOST_CHECK(runtimeCode == createdCode);

    // set()
    bytes callDataToSet =
        fromHex(string("0x60fe47b1") +  // set(0xaa)
                string("00000000000000000000000000000000000000000000000000000000000000aa"));
    Transaction setTx(value, gasPrice, gas, newAddress, callDataToSet);
    setTx.forceSender(caller);

    Executive e1(m_mptStates, initEnvInfo());
    executeTransaction(e1, setTx);

    // get()
    bytes callDataToGet = fromHex(string("6d4ce63c") +  // get()
                                  string(""));

    Transaction getTx(value, gasPrice, gas, newAddress, callDataToGet);
    getTx.forceSender(caller);

    Executive e2(m_mptStates, initEnvInfo());
    executeTransaction(e2, getTx);

    bytes compareName = fromHex("00000000000000000000000000000000000000000000000000000000000000aa");
    // cout << "get() result: " << toHex(getExeRes.output) << endl;
    // BOOST_CHECK(getExeRes.output == compareName);

    // getByCall()
    bytes callDataToGetByCall = fromHex(string("b7379733") +  // getByCall()
                                        string(""));

    Transaction getByCallTx(value, gasPrice, gas, newAddress, callDataToGetByCall);
    auto keyPair = KeyPair::create();
    auto sig = dev::crypto::Sign(keyPair, tx.sha3(WithoutSignature));
    tx.updateSignature(sig);
    getByCallTx.forceSender(caller);

    Executive e3(m_mptStates, initEnvInfo());
    executeTransaction(e3, getByCallTx);

    // cout << "getByCall() result: " << toHex(getExeResByCall.output) << endl;
    // BOOST_CHECK(getExeResByCall.output == compareName);

    // destroy
    bytes callDestroy = fromHex(string("83197ef0") +  // destroy()
                                string(""));

    Transaction destroyTx(value, gasPrice, gas, newAddress, callDestroy);
    sig = dev::crypto::Sign(keyPair, destroyTx.sha3(WithoutSignature));
    destroyTx.updateSignature(sig);
    destroyTx.forceSender(caller);

    Executive e4(m_mptStates, initEnvInfo());
    executeTransaction(e4, destroyTx);
    BOOST_CHECK(!m_mptStates->addressHasCode(newAddress));
    g_BCOSConfig.setSupportedVersion(supportedVersion, version);
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
