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
 * @file ExtVMFace.cpp
 * @author: yujiechen
 * @date 2018-09-03
 */
#include "libdevcrypto/CryptoInterface.h"
#include <evmc/helpers.h>
#include <libexecutive/EVMHostContext.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libevm/FakeExtVMFace.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::eth;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(ExtVMFaceTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testEVMCTrans)
{
    /// test adddress transalation
    /// test toEVMC
    KeyPair key_pair = KeyPair::create();
    Address eth_addr = toAddress(key_pair.pub());
    evmc_address evm_addr = toEvmC(eth_addr);
    /// test fromEVMC
    Address transed_eth_addr = fromEvmC(evm_addr);
    // std::cout << "transed address:" << toHex(transed_eth_addr) << std::endl;
    BOOST_CHECK(eth_addr == transed_eth_addr);
    /// test u256 translation
    evmc_uint256be evm_uint = toEvmC(crypto::Hash("+++"));
    u256 eth_u256 = fromEvmC(evm_uint);
    BOOST_CHECK(eth_u256 == u256(crypto::Hash("+++")));
}

BOOST_AUTO_TEST_CASE(testFunctionTables)
{
    std::string code_str = "EVMHostInterface Test";
    bytes code(code_str.begin(), code_str.end());
    u256 gasUsed = u256(300000);
    u256 gasLimit = u256(300000);
    EnvInfo env_info = InitEnvInfo::createEnvInfo(gasUsed, gasLimit);
    CallParameters param = InitCallParams::createRandomCallParams();
    FakeExtVM fake_ext_vm(env_info, param.codeAddress, param.senderAddress, param.senderAddress,
        param.valueTransfer, param.gas, param.data, code, crypto::Hash(code_str), 0, false, true);

    ///========== TEST ACCOUNT EXISTS=====================
    /// test account_exists
    evmc_address code_addr = toEvmC(fake_ext_vm.myAddress());
    int exist = fake_ext_vm.fn_table->account_exists(&fake_ext_vm, &code_addr);
    BOOST_CHECK(exist == 1);
    evmc_address sender_addr = toEvmC(fake_ext_vm.origin());
    exist = fake_ext_vm.fn_table->account_exists(&fake_ext_vm, &sender_addr);
    BOOST_CHECK(exist == 1);
    evmc_address caller_addr = toEvmC(fake_ext_vm.caller());
    exist = fake_ext_vm.fn_table->account_exists(&fake_ext_vm, &caller_addr);
    BOOST_CHECK(exist == 1);
    /// test account doesn't exists
    code_addr = toEvmC(Address("0xa12312a"));
    exist = fake_ext_vm.fn_table->account_exists(&fake_ext_vm, &code_addr);
    BOOST_CHECK(exist == 0);

    ///========== TEST setStorage and getStorage=====================
    /// test setStorage
    code_addr = toEvmC(fake_ext_vm.myAddress());
    u256 key = u256(crypto::Hash("code"));
    // std::cout << "########code key:" << crypto::Hash("code") << std::endl;
    u256 value = u256(crypto::Hash(code_str));
    evmc_uint256be evm_key = toEvmC(key);
    evmc_uint256be evm_value = toEvmC(value);
    u256 origin_refunds = fake_ext_vm.sub().refunds;
    // std::cout << "#########ready to callback set_storage" << std::endl;
    evmc_storage_status status =
        fake_ext_vm.fn_table->set_storage(&fake_ext_vm, &code_addr, &evm_key, &evm_value);
    // std::cout << "#########callback set_storage succ" << std::endl;
    BOOST_CHECK(status == EVMC_STORAGE_ADDED);
    BOOST_CHECK(origin_refunds == fake_ext_vm.sub().refunds);
    /// test getStorage
    evmc_uint256be result;
    fake_ext_vm.fn_table->get_storage(&result, &fake_ext_vm, &code_addr, &evm_key);
    // std::cout<<"####fromEvmC(result):"<<toString(fromEvmC(result))<<std::endl;
    // std::cout<<"####fromEvmC(evm_value):"<<toString(fromEvmC(evm_value))<<std::endl;
    BOOST_CHECK(fromEvmC(result) == fromEvmC(evm_value));
    BOOST_CHECK(origin_refunds == fake_ext_vm.sub().refunds);
    /// modify storage
    evmc_uint256be evm_new_value = toEvmC(crypto::Hash("hello, modified"));
    status = fake_ext_vm.fn_table->set_storage(&fake_ext_vm, &code_addr, &evm_key, &evm_new_value);
    BOOST_CHECK(status == EVMC_STORAGE_MODIFIED);
    BOOST_CHECK(origin_refunds == fake_ext_vm.sub().refunds);
    /// callbcak getStorage to make sure value has been updated
    fake_ext_vm.fn_table->get_storage(&result, &fake_ext_vm, &code_addr, &evm_key);
    BOOST_CHECK(fromEvmC(result) == fromEvmC(evm_new_value));
    BOOST_CHECK(origin_refunds == fake_ext_vm.sub().refunds);
    /// no modify, but callback SetStorage
    status = fake_ext_vm.fn_table->set_storage(&fake_ext_vm, &code_addr, &evm_key, &evm_new_value);
    BOOST_CHECK(status == EVMC_STORAGE_UNCHANGED);
    /// check has not been changed
    fake_ext_vm.fn_table->get_storage(&result, &fake_ext_vm, &code_addr, &evm_key);
    BOOST_CHECK(fromEvmC(result) == fromEvmC(evm_new_value));
    BOOST_CHECK(origin_refunds == fake_ext_vm.sub().refunds);
    /// test EVMC_STORAGE_DELETED(set value to 0)
    evm_new_value = toEvmC(u256(0));
    status = fake_ext_vm.fn_table->set_storage(&fake_ext_vm, &code_addr, &evm_key, &evm_new_value);
    BOOST_CHECK(status == EVMC_STORAGE_DELETED);
    fake_ext_vm.fn_table->get_storage(&result, &fake_ext_vm, &code_addr, &evm_key);
    BOOST_CHECK(fromEvmC(result) == fromEvmC(evm_new_value));
    /// check refunds
    BOOST_CHECK(fake_ext_vm.sub().refunds == (origin_refunds + u256(15000)));

    ///========== TEST call contract=====================
    evmc_message message;
    message.destination = toEvmC(fake_ext_vm.myAddress());
    message.sender = toEvmC(fake_ext_vm.caller());
    message.input_data = fake_ext_vm.data().data();
    message.input_size = sizeof(fake_ext_vm.data().size()) / sizeof(char);
    message.code_hash = toEvmC(fake_ext_vm.codeHash());
    message.gas = (uint64_t)(fake_ext_vm.gasPrice());
    message.depth = fake_ext_vm.depth();
    message.kind = fake_ext_vm.isCreate() ?
                       (EVMC_CREATE) :
                       (fake_ext_vm.staticCall() ? EVMC_CALL : EVMC_DELEGATECALL);
    message.flags = EVMC_STATIC;
    /// test call function
    evmc_result call_result;
    fake_ext_vm.fn_table->call(&call_result, &fake_ext_vm, &message);
    BOOST_CHECK(call_result.gas_left == (message.gas - 10));
    BOOST_CHECK(call_result.status_code == EVMC_SUCCESS);
    std::string origin_str = "fake call";
    BOOST_CHECK(memcmp(call_result.output_data, origin_str.c_str(), call_result.output_size) == 0);
    /// test evmc_get_optional_storage
    auto* data = evmc_get_optional_storage(&call_result);
    BOOST_CHECK(memcmp(data->pointer, origin_str.c_str(), call_result.output_size) == 0);
    ///========== TEST emit_log(eth::log)=====================
    evmc_uint256be topics[2];
    topics[0] = toEvmC(crypto::Hash("0"));
    topics[1] = toEvmC(crypto::Hash("1"));
    fake_ext_vm.fn_table->emit_log(
        &fake_ext_vm, &code_addr, call_result.output_data, call_result.output_size, topics, 2);
    BOOST_CHECK(fake_ext_vm.sub().logs.size() == 1);
    BOOST_CHECK(fake_ext_vm.sub().logs[0].topics.size() == 2);
    BOOST_CHECK(fake_ext_vm.sub().logs[0].topics[0] == crypto::Hash("0"));
    BOOST_CHECK(fake_ext_vm.sub().logs[0].topics[1] == crypto::Hash("1"));
    BOOST_CHECK(memcpy(
        fake_ext_vm.sub().logs[0].data.data(), call_result.output_data, call_result.output_size));
    BOOST_CHECK(fake_ext_vm.sub().logs[0].data.size() == call_result.output_size);
    /// test release
    call_result.release(&call_result);
    ///========== TEST create contract=====================
    /// test success
    message.kind = EVMC_CREATE;
    evmc_result create_result;
    fake_ext_vm.setMyAddress(fromEvmC(message.sender));
    fake_ext_vm.fn_table->call(&create_result, &fake_ext_vm, &message);
    BOOST_CHECK(create_result.status_code == EVMC_SUCCESS);
    BOOST_CHECK(create_result.output_data == nullptr);
    BOOST_CHECK(create_result.output_size == 0);
    /// test failed
    fake_ext_vm.fn_table->call(&create_result, &fake_ext_vm, &message);
    BOOST_CHECK(create_result.status_code == EVMC_FAILURE);
    std::string create_str = "fake create";
    BOOST_CHECK(strncmp((const char*)create_result.output_data, create_str.c_str(),
                    create_result.output_size) == 0);
    data = evmc_get_optional_storage(&create_result);
    BOOST_CHECK(memcmp(data->pointer, create_str.c_str(), create_result.output_size) == 0);
    create_result.release(&create_result);
}

BOOST_AUTO_TEST_CASE(testCodeRelated)
{
    std::string code_str = "EVMHostInterface Test";
    bytes code(code_str.begin(), code_str.end());
    u256 gasUsed = u256(300000);
    u256 gasLimit = u256(300000);
    EnvInfo env_info = InitEnvInfo::createEnvInfo(gasUsed, gasLimit);
    CallParameters param = InitCallParams::createRandomCallParams();
    FakeExtVM fake_ext_vm(env_info, param.codeAddress, param.senderAddress, param.senderAddress,
        param.valueTransfer, param.gas, param.data, code, crypto::Hash(code_str), 0, false, true);

    ///========== TEST getBalance=====================
    evmc_uint256be balance_result;
    evmc_address balance_addr = toEvmC(fake_ext_vm.myAddress());
    fake_ext_vm.fn_table->get_balance(&balance_result, &fake_ext_vm, &balance_addr);
    BOOST_CHECK(fromEvmC(balance_result) == param.valueTransfer);

    ///========== TEST CodeSize=====================
    evmc_address code_addr = toEvmC(Address(0));
    size_t code_size = fake_ext_vm.fn_table->get_code_size(&fake_ext_vm, &code_addr);
    BOOST_CHECK(code_size == 0);
    code_addr = toEvmC(fake_ext_vm.myAddress());
    code_size = fake_ext_vm.fn_table->get_code_size(&fake_ext_vm, &code_addr);
    BOOST_CHECK(code_size == fake_ext_vm.code().size());
    ///========== TEST CodeHash=====================
    evmc_uint256be hash_result;
    fake_ext_vm.fn_table->get_code_hash(&hash_result, &fake_ext_vm, &code_addr);
    BOOST_CHECK(fromEvmC(hash_result) != h256{});
    BOOST_CHECK(fromEvmC(hash_result) == crypto::Hash(code_str));
    code_addr = toEvmC(Address(0));
    fake_ext_vm.fn_table->get_code_hash(&hash_result, &fake_ext_vm, &code_addr);
    BOOST_CHECK(fromEvmC(hash_result) == h256{});

    ///========== TEST copyCode=====================
    unsigned int offset = 5;
    bytes copied_buf(offset, 0);
    code_addr = toEvmC(fake_ext_vm.myAddress());
    size_t copied_size =
        fake_ext_vm.fn_table->copy_code(&fake_ext_vm, &code_addr, offset, &(copied_buf[0]), offset);
    BOOST_CHECK(copied_buf.size() == offset);
    BOOST_CHECK(copied_size == offset);
    for (unsigned int i = offset; i < 2 * offset; i++)
        BOOST_CHECK(copied_buf[i - offset] == fake_ext_vm.code()[i]);
    /// test offset too large
    offset = 100;
    size_t copy_len = 10;
    copied_buf.resize(copy_len);
    copied_size = fake_ext_vm.fn_table->copy_code(
        &fake_ext_vm, &code_addr, offset, &(copied_buf[0]), copy_len);
    BOOST_CHECK(copied_size == 0);
    /// test len too large
    copy_len = 100;
    offset = 5;
    copied_buf.resize(copy_len);
    copied_size = fake_ext_vm.fn_table->copy_code(
        &fake_ext_vm, &code_addr, offset, &(copied_buf[0]), copy_len);
    BOOST_CHECK(copied_size == fake_ext_vm.code().size() - offset);
    for (unsigned int i = offset; i < offset + copied_size; i++)
        BOOST_CHECK(copied_buf[i - offset] == fake_ext_vm.code()[i]);
    ///========== TEST selfdestruct=====================
    fake_ext_vm.fn_table->selfdestruct(&fake_ext_vm, &code_addr, &code_addr);
    BOOST_CHECK(fake_ext_vm.sub().suicides.count(fromEvmC(code_addr)) == 1);

    ///========== test getTxContext =====================
    evmc_tx_context result;
    fake_ext_vm.fn_table->get_tx_context(&result, &fake_ext_vm);
    BOOST_CHECK(fromEvmC(result.tx_gas_price) == fake_ext_vm.gasPrice());
    BOOST_CHECK(fromEvmC(result.tx_origin) == fake_ext_vm.origin());
    BOOST_CHECK(result.block_number == fake_ext_vm.envInfo().number());
    BOOST_CHECK((uint64_t)result.block_timestamp == fake_ext_vm.envInfo().timestamp());
    BOOST_CHECK(result.block_gas_limit == static_cast<int64_t>(fake_ext_vm.envInfo().gasLimit()));
    ///========== test getBlockHash =====================
    evmc_uint256be block_hash;
    fake_ext_vm.fn_table->get_block_hash(&block_hash, &fake_ext_vm, 20);
    BOOST_CHECK(fromEvmC(block_hash) == crypto::Hash(toString(20)));
    fake_ext_vm.fn_table->get_block_hash(&block_hash, &fake_ext_vm, fake_ext_vm.envInfo().number());
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
