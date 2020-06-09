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

#pragma once
#include "libdevcrypto/CryptoInterface.h"
#include <evmc/helpers.h>
#include <libdevcore/CommonJS.h>
#include <libdevcrypto/Common.h>
#include <libethcore/BlockHeader.h>
#include <libexecutive/EVMHostContext.h>
#include <libexecutive/EVMHostInterface.h>
#include <stdlib.h>
#include <time.h>

using namespace dev;
using namespace dev::eth;
using namespace dev::executive;
namespace dev
{
namespace test
{
class InitCallParams
{
public:
    InitCallParams() { srand(time(NULL)); }
    static void createRandomElements(Address& sender_addr, Address& contract_addr,
        Address& receive_addr, u256& transfer, u256& gas, u256& apparent_value, bytesConstRef& data)
    {
        sender_addr = toAddress(Public::random());
        contract_addr = toAddress(Public::random());
        receive_addr = toAddress(Public::random());
        transfer = u256(rand());
        gas = u256(3000000);
        apparent_value = u256(rand());
        std::string tmp_dir = "hello, evm, wevwrekqlrkjelr";
        bytes data_bytes(tmp_dir.begin(), tmp_dir.end());
        data = ref(data_bytes);
    }
    static CallParameters& createDefaultCallParams()
    {
        static CallParameters evmcall_param;
        return evmcall_param;
    }
    static CallParameters& createRandomCallParams()
    {
        Address sender_addr;
        Address contract_addr;
        Address receive_addr;
        u256 transfer;
        u256 apparent_value;
        u256 gas;
        bytesConstRef data;
        createRandomElements(
            sender_addr, contract_addr, receive_addr, transfer, gas, apparent_value, data);
        static CallParameters evmcall_param(
            sender_addr, contract_addr, receive_addr, transfer, apparent_value, gas, data);
        return evmcall_param;
    }
};

class InitEnvInfo
{
public:
    static const BlockHeader& genBlockHeader()
    {
        /// create BlockHeader
        static BlockHeader genesis;
        genesis.setParentHash(crypto::Hash("parent"));
        genesis.setRoots(crypto::Hash("transactionRoot"), crypto::Hash("receiptRoot"),
            crypto::Hash("stateRoot"));
        genesis.setLogBloom(LogBloom(0));
        genesis.setNumber(1);
        genesis.setGasLimit(u256(3000000));
        genesis.setGasUsed(u256(100000));
        int64_t current_time = utcTime();
        genesis.setTimestamp(current_time);
        genesis.appendExtraDataArray(jsToBytes("0x1020"));
        genesis.setSealer(u256("0x00"));
        h512s sealer_list;
        for (unsigned int i = 0; i < 10; i++)
        {
            sealer_list.push_back(toPublic(Secret::random()));
        }
        genesis.setSealerList(sealer_list);
        return genesis;
    }

    static dev::h256 fakeCallBack(int64_t) { return h256(); }

    static EnvInfo& createEnvInfo(u256 const gasUsed, u256 const gasLimit = u256(300000))
    {
        static EnvInfo env_info(genBlockHeader(), fakeCallBack, gasUsed, gasLimit);
        return env_info;
    }
};

class FakeExtVM : public EVMHostContext
{
public:
    evmc_result call(CallParameters& param) override
    {
        fake_result = "fake call";
        bytes fake_result_bytes(fake_result.begin(), fake_result.end());
        output = {std::move(fake_result_bytes), 0, fake_result_bytes.size()};
        param.gas -= u256(10);
        evmc_result result;
        if (account_map.count(param.codeAddress))
            result.status_code = EVMC_SUCCESS;
        else
            result.status_code = EVMC_FAILURE;
        result.gas_left = static_cast<int64_t>(param.gas);
        copyToOptionalStorage(std::move(output), &result);
        return result;
    }

    evmc_result create(u256 const&, u256& io_gas, bytesConstRef, evmc_opcode, u256) override
    {
        u256 nonce = u256(00013443);
        Address m_newAddress = right160(crypto::Hash(rlpList(myAddress(), nonce)));
        evmc_result result;
        if (!account_map.count(m_newAddress))
        {
            result.status_code = EVMC_SUCCESS;
            account_map.insert(m_newAddress);
            result.gas_left = static_cast<int64_t>(io_gas - u256(10));
            result.output_data = nullptr;
            result.output_size = 0;
            result.create_address = toEvmC(m_newAddress);
        }
        else
        {
            result.status_code = EVMC_FAILURE;
            fake_result = "fake create";
            bytes fake_result_bytes(fake_result.begin(), fake_result.end());
            output = {std::move(fake_result_bytes), 0, fake_result_bytes.size()};
            result.create_address = toEvmC(m_newAddress);
            copyToOptionalStorage(std::move(output), &result);
        }
        return result;
    }
    /// account related function
    bool exists(Address const& addr) override
    {
        if (account_map.count(addr))
            return true;
        return false;
    }
    /// Read storage location.
    u256 store(u256 const& key) override
    {
        auto it = storage_map.find(key);
        if (it != storage_map.end())
        {
            return it->second;
        }
        return u256(0);
    }
    /// Write a value in storage.
    void setStore(u256 const& _key, u256 const& _value) override
    {
        auto it = storage_map.find(_key);
        if (it != storage_map.end())
        {
            it->second = _value;
        }
        else
        {
            storage_map.insert(std::make_pair(_key, _value));
        }
        // storage_map[_key] = _value;
    }

    /// Read address's balance.
    u256 balance(Address const& addr) override
    {
        auto it = balance_map.find(addr);
        if (it != balance_map.end())
        {
            return it->second;
        }
        return 0;
    }
    /// @returns the size of the code in bytes at the given address.
    size_t codeSizeAt(Address const& contract_addr) override
    {
        if (account_map.count(contract_addr))
            return code().size();
        return 0;
    }

    h256 codeHashAt(Address const& contract_addr) override
    {
        if (account_map.count(contract_addr))
            return codeHash();
        return h256{};
    }
    /// Read address's code.
    bytes const codeAt(Address const&) override { return code(); }
    bool isPermitted() override { return true; }

    h256 blockHash(int64_t number) override { return crypto::Hash(toString(number)); }

    FakeExtVM(EnvInfo const& _envInfo, Address const& _myAddress, Address const& _caller,
        Address const& _origin, u256 const& _value, u256 const& _gasPrice, bytesConstRef _data,
        bytes _code, h256 const& _codeHash, unsigned _depth, bool _isCreate, bool _staticCall)
      : EVMHostContext(nullptr, _envInfo, _myAddress, _caller, _origin, _value, _gasPrice, _data,
            _code, _codeHash, _depth, _isCreate, _staticCall)
    {
        account_map.insert(_myAddress);
        account_map.insert(_caller);
        account_map.insert(_origin);

        auto it = balance_map.find(_myAddress);
        if (it != balance_map.end())
        {
            it->second = _value;
        }
        else
        {
            balance_map.insert(std::make_pair(_myAddress, _value));
        }
        // balance_map[_myAddress] = _value;
    }

private:
    // Pass the output to the EVM without a copy. The EVM will delete it
    // when finished with it.
    // Place a new vector of bytes containing output in result's reserved memory.
    void copyToOptionalStorage(owning_bytes_ref&& _output_own_bytes, evmc_result* o_result)
    {
        owning_bytes_ref output_own_bytes(std::move(_output_own_bytes));
        o_result->output_data = output_own_bytes.data();
        o_result->output_size = output_own_bytes.size();

        // Place a new vector of bytes containing output in result's reserved memory.
        auto* data = evmc_get_optional_storage(o_result);
        static_assert(sizeof(bytes) <= sizeof(*data), "Vector is too big");
        new (data) bytes(output_own_bytes.takeBytes());
        /// to avoid memory leak
        // Set the destructor to delete the vector.
        o_result->release = [](evmc_result const* _result) {
            auto* data = evmc_get_const_optional_storage(_result);
            auto& output = reinterpret_cast<bytes const&>(*data);
            // Explicitly call vector's destructor to release its data.
            // This is normal pattern when placement new operator is used.
            output.~bytes();
        };
    }

private:
    std::set<Address> account_map;
    std::map<u256, u256> storage_map;
    std::map<Address, u256> balance_map;
    std::string fake_result;
    owning_bytes_ref output;
};
}  // namespace test
}  // namespace dev
