#include <evmc/helpers.h>
#include <libdevcore/CommonJS.h>
#include <libdevcore/SHA3.h>
#include <libdevcrypto/Common.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/LastBlockHashesFace.h>
#include <libevm/ExtVMFace.h>
#include <stdlib.h>
#include <time.h>
using namespace dev;
using namespace dev::eth;
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
            sender_addr, contract_addr, receive_addr, transfer, apparent_value, gas, data, {});
        return evmcall_param;
    }
};

class FakeLastBlockHashes : public LastBlockHashesFace
{
public:
    virtual h256s precedingHashes(h256 const& _mostRecentHash) const
    {
        h256s tmp;
        return tmp;
    }
    /// Clear any cached result
    virtual void clear() {}
};

class InitEnvInfo
{
public:
    static const BlockHeader& genBlockHeader()
    {
        /// create BlockHeader
        static BlockHeader genesis;
        genesis.setParentHash(sha3("parent"));
        genesis.setRoots(sha3("transactionRoot"), sha3("receiptRoot"), sha3("stateRoot"));
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


    static EnvInfo& createEnvInfo(u256 const gasUsed, u256 const gasLimit = u256(300000))
    {
        FakeLastBlockHashes lh;
        static EnvInfo env_info(genBlockHeader(), lh, gasUsed, gasLimit);
        return env_info;
    }
};

class FakeExtVM : public ExtVMFace
{
public:
    virtual evmc_result call(CallParameters& param)
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
        std::cout << "call result:" << (char*)result.output_data << std::endl;
        std::cout << "call size:" << result.output_size << std::endl;
        return result;
    }

    virtual evmc_result create(u256 _endowment, u256& io_gas, bytesConstRef _code, Instruction _op,
        u256 _salt, OnOpFunc const& _onOp)
    {
        u256 nonce = u256(00013443);
        Address m_newAddress = right160(sha3(rlpList(myAddress(), nonce)));
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
            std::cout << "create result:" << (char*)result.output_data << std::endl;
            std::cout << "create size:" << result.output_size << std::endl;
        }
        return result;
    }
    /// account related function
    virtual bool exists(Address addr)
    {
        if (account_map.count(addr))
            return true;
        return false;
    }
    /// Read storage location.
    virtual u256 store(u256 key)
    {
        if (storage_map.count(key))
            return storage_map[key];
        return u256(0);
    }
    /// Write a value in storage.
    virtual void setStore(u256 _key, u256 _value) { storage_map[_key] = _value; }

    /// Read address's balance.
    virtual u256 balance(Address addr)
    {
        if (balance_map.count(addr))
            return balance_map[addr];
        return 0;
    }
    /// @returns the size of the code in bytes at the given address.
    virtual size_t codeSizeAt(Address contract_addr)
    {
        if (account_map.count(contract_addr))
            return code().size();
        return 0;
    }

    virtual h256 codeHashAt(Address contract_addr)
    {
        if (account_map.count(contract_addr))
            return codeHash();
        return h256{};
    }
    /// Read address's code.
    virtual bytes const& codeAt(Address contract_addr) { return code(); }

    virtual h256 blockHash(int64_t number) { return sha3(toString(number)); }

    FakeExtVM(EnvInfo const& _envInfo, Address _myAddress, Address _caller, Address _origin,
        u256 _value, u256 _gasPrice, bytesConstRef _data, bytes _code, h256 const& _codeHash,
        unsigned _depth, bool _isCreate, bool _staticCall)
      : ExtVMFace(_envInfo, _myAddress, _caller, _origin, _value, _gasPrice, _data, _code,
            _codeHash, _depth, _isCreate, _staticCall)
    {
        account_map.insert(_myAddress);
        account_map.insert(_caller);
        account_map.insert(_origin);

        balance_map[_myAddress] = _value;
    }

private:
    // Pass the output to the EVM without a copy. The EVM will delete it
    // when finished with it.
    // Place a new vector of bytes containing output in result's reserved memory.
    void copyToOptionalStorage(owning_bytes_ref&& _output_own_bytes, evmc_result* o_result)
    {
        owning_bytes_ref output_own_bytes(std::move(_output_own_bytes));
        std::cout << "### size:" << output_own_bytes.size() << std::endl;
        o_result->output_data = output_own_bytes.data();
        o_result->output_size = output_own_bytes.size();

        // Place a new vector of bytes containing output in result's reserved memory.
        auto* data = evmc_get_optional_storage(o_result);
        static_assert(sizeof(bytes) <= sizeof(*data), "Vector is too big");
        new (data) bytes(output_own_bytes.takeBytes());

        std::cout << "###data FINAL:" << (char*)data->pointer << std::endl;
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
