/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file ExtVMFace.h
 * @author wheatli
 * @date 2018.8.28
 * @record copy from aleth, this is a vm interface
 */

#pragma once

#include <libconfig/GlobalConfigure.h>
#include <libdevcore/Common.h>
#include <libdevcore/CommonData.h>
#include <libdevcrypto/Hash.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/Common.h>
#include <libethcore/EVMSchedule.h>
#include <libethcore/Instruction.h>
#include <libethcore/LastBlockHashesFace.h>
#include <libethcore/LogEntry.h>

#include <evmc/evmc.h>

#include <boost/optional.hpp>
#include <functional>
#include <set>

namespace dev
{
namespace blockverifier
{
class ExecutiveContext;
}

namespace eth
{
struct SubState
{
    std::set<Address> suicides;  ///< Any accounts that have suicided.
    LogEntries logs;             ///< Any logs.
    u256 refunds;                ///< Refund counter of SSTORE nonzero->zero.

    SubState& operator+=(SubState const& _s)
    {
        suicides += _s.suicides;
        refunds += _s.refunds;
        logs += _s.logs;
        return *this;
    }

    void clear()
    {
        suicides.clear();
        logs.clear();
        refunds = 0;
    }
};

class ExtVMFace;
class VMFace;

/**
 * @brief : execute the opcode of evm
 *
 */
using OnOpFunc = std::function<void(uint64_t /*steps*/, uint64_t /* PC */, Instruction /*instr*/,
    bigint /*newMemSize*/, bigint /*gasCost*/, bigint /*gas*/, VMFace const*, ExtVMFace const*)>;

/// set parameters and functions for the evm call
struct CallParameters
{
    CallParameters() = default;
    CallParameters(Address _senderAddress, Address _codeAddress, Address _receiveAddress,
        u256 _valueTransfer, u256 _apparentValue, u256 _gas, bytesConstRef _data,
        OnOpFunc _onOpFunc)
      : senderAddress(_senderAddress),
        codeAddress(_codeAddress),
        receiveAddress(_receiveAddress),
        valueTransfer(_valueTransfer),
        apparentValue(_apparentValue),
        gas(_gas),
        data(_data),
        onOp(_onOpFunc)
    {}
    Address senderAddress;   /// address of the transaction sender
    Address codeAddress;     /// address of the contract
    Address receiveAddress;  /// address of the transaction receiver
    u256 valueTransfer;      /// transferred wei between the sender and receiver
    u256 apparentValue;
    u256 gas;
    bytesConstRef data;       /// transaction data
    bool staticCall = false;  /// only true when the transaction is a message call
    OnOpFunc onOp;
};

/// the information related to the EVM
class EnvInfo
{
public:
    typedef boost::function<dev::h256(int64_t x)> CallBackFunction;

    // Constructor with custom gasLimit - used in some synthetic scenarios like eth_estimateGas RPC
    // method
    EnvInfo(BlockHeader const& _current, CallBackFunction _callback, u256 const& _gasUsed,
        u256 const& _gasLimit)
      : EnvInfo(_current, _callback, _gasUsed)
    {
        /// modify the gasLimit of current blockheader with given gasLimit
        m_headerInfo.setGasLimit(_gasLimit);
    }

    EnvInfo(BlockHeader const& _current, CallBackFunction _callback, u256 const& _gasUsed)
      : m_headerInfo(_current), m_numberHash(_callback), m_gasUsed(_gasUsed)
    {}

    EnvInfo() {}


    /// @return block header
    BlockHeader const& header() const { return m_headerInfo; }

    /// @return block number
    int64_t number() const { return m_headerInfo.number(); }

    /// @return timestamp
    uint64_t timestamp() const { return m_headerInfo.timestamp(); }

    /// @return gasLimit of the block header
    u256 const& gasLimit() const { return m_headerInfo.gasLimit(); }


    /// @return used gas of the evm
    u256 const& gasUsed() const { return m_gasUsed; }

    dev::h256 numberHash(int64_t x) const { return m_numberHash(x); }

    std::shared_ptr<dev::blockverifier::ExecutiveContext> precompiledEngine() const;

    void setPrecompiledEngine(
        std::shared_ptr<dev::blockverifier::ExecutiveContext> executiveEngine);


private:
    BlockHeader m_headerInfo;
    CallBackFunction m_numberHash;
    u256 m_gasUsed;
    std::shared_ptr<dev::blockverifier::ExecutiveContext> m_executiveEngine;
};

/// Represents a call result.
///
/// @todo: Replace with evmc_result in future.
/*
struct CallResult
{
    evmc_status_code status;
    owning_bytes_ref output;

    CallResult(evmc_status_code status, owning_bytes_ref&& output)
      : status{status}, output{std::move(output)}
    {}
};*/

/// Represents a CREATE result.
///
/// @todo: Replace with evmc_result in future.
/*
struct CreateResult
{
    evmc_status_code status;
    owning_bytes_ref output;
    h160 address;

    CreateResult(evmc_status_code status, owning_bytes_ref&& output, h160 const& address)
      : status{status}, output{std::move(output)}, address{address}
    {}
};*/

/**
 * @brief Interface and null implementation of the class for specifying VM externalities.
 */
class ExtVMFace : public evmc_context
{
public:
    /// Full constructor.
    ExtVMFace(EnvInfo const& _envInfo, Address const& _myAddress, Address const& _caller,
        Address const& _origin, u256 const& _value, u256 const& _gasPrice, bytesConstRef _data,
        bytes _code, h256 const& _codeHash, unsigned _depth, bool _isCreate, bool _staticCall);

    virtual ~ExtVMFace() = default;

    ExtVMFace(ExtVMFace const&) = delete;
    ExtVMFace& operator=(ExtVMFace const&) = delete;

    /// Read storage location.
    virtual u256 store(u256 const&) = 0;

    /// Write a value in storage.
    virtual void setStore(u256 const&, u256 const&) = 0;

    /// Read address's balance.
    virtual u256 balance(Address const&) = 0;

    /// Read address's code.
    virtual bytes const codeAt(Address const&) { return NullBytes; }

    /// @returns the size of the code in bytes at the given address.
    virtual size_t codeSizeAt(Address const&) { return 0; }

    /// @returns the hash of the code at the given address.
    virtual h256 codeHashAt(Address const&) { return h256{}; }

    /// Does the account exist?
    virtual bool exists(Address const&) { return false; }

    /// Suicide the associated contract and give proceeds to the given address.
    virtual void suicide(Address const&) { m_sub.suicides.insert(m_myAddress); }

    /// Create a new (contract) account.
    virtual evmc_result create(
        u256 const&, u256&, bytesConstRef, Instruction, u256, OnOpFunc const&) = 0;

    /// Make a new message call.
    virtual evmc_result call(CallParameters&) = 0;

    /// Revert any changes made (by any of the other calls).
    virtual void log(h256s&& _topics, bytesConstRef _data)
    {
        m_sub.logs.push_back(LogEntry(m_myAddress, std::move(_topics), _data.toBytes()));
    }

    /// Hash of a block if within the last 256 blocks, or h256() otherwise.
    virtual h256 blockHash(int64_t _number) = 0;

    /// Get the execution environment information.
    EnvInfo const& envInfo() const { return m_envInfo; }

    /// Return the EVM gas-price schedule for this execution context.
    /// default is FiscoBcosSchedule
    virtual EVMSchedule const& evmSchedule() const { return g_BCOSConfig.evmSchedule(); }

public:
    /// ------ get interfaces related to ExtVMFace------
    Address const& myAddress() { return m_myAddress; }
    Address const& caller() { return m_caller; }
    Address const& origin() { return m_origin; }
    u256 const& value() { return m_value; }
    u256 const& gasPrice() { return m_gasPrice; }
    bytesConstRef const& data() { return m_data; }
    bytes const& code() { return m_code; }
    h256 const& codeHash() { return m_codeHash; }
    u256 const& salt() { return m_salt; }
    SubState& sub() { return m_sub; }
    unsigned const& depth() { return m_depth; }
    bool const& isCreate() { return m_isCreate; }
    bool const& staticCall() { return m_staticCall; }
    /// ------ set interfaces related to ExtVMFace------
    void setMyAddress(Address const& _contractAddr) { m_myAddress = _contractAddr; }
    void setCaller(Address const& _senderAddr) { m_caller = _senderAddr; }
    void setOrigin(Address const& _origin) { m_origin = _origin; }
    void setValue(u256 const& _value) { m_value = _value; }
    void setGasePrice(u256 const& _gasPrice) { m_gasPrice = _gasPrice; }
    void setData(bytesConstRef _data) { m_data = _data; }
    void setCode(bytes& _code) { m_code = _code; }
    void setCodeHash(h256 const& _codeHash) { m_codeHash = _codeHash; }
    void setSalt(u256 const& _salt) { m_salt = _salt; }
    void setSub(SubState _sub) { m_sub = _sub; }
    void setDepth(unsigned _depth) { m_depth = _depth; }
    void setCreate(bool _isCreate) { m_isCreate = _isCreate; }
    void setStaticCall(bool _staticCall) { m_staticCall = _staticCall; }

protected:
    EnvInfo const& m_envInfo;

private:
    Address m_myAddress;  ///< Address associated with executing code (a contract, or
                          ///< contract-to-be).
    Address m_caller;  ///< Address which sent the message (either equal to origin or a contract).
    Address m_origin;  ///< Original transactor.
    u256 m_value;      ///< Value (in Wei) that was passed to this address.
    u256 m_gasPrice;   ///< Price of gas (that we already paid).
    bytesConstRef m_data;       ///< Current input data.
    bytes m_code;               ///< Current code that is executing.
    h256 m_codeHash;            ///< SHA3 hash of the executing code
    u256 m_salt;                ///< Values used in new address construction by CREATE2
    SubState m_sub;             ///< Sub-band VM state (suicides, refund counter, logs).
    unsigned m_depth = 0;       ///< Depth of the present call.
    bool m_isCreate = false;    ///< Is this a CREATE call?
    bool m_staticCall = false;  ///< Throw on state changing.
};

/**
 * @brief : trans ethereum addess to evm address
 * @param _addr : the ehereum address
 * @return evmc_address : the transformed evm address
 */
inline evmc_address toEvmC(Address const& _addr)
{
    return reinterpret_cast<evmc_address const&>(_addr);
}

/**
 * @brief : trans ethereum hash to evm hash
 * @param _h : hash value
 * @return evmc_uint256be : transformed hash
 */
inline evmc_uint256be toEvmC(h256 const& _h)
{
    return reinterpret_cast<evmc_uint256be const&>(_h);
}
/**
 * @brief : trans uint256 number of evm-represented to u256
 * @param _n : the uint256 number that can parsed by evm
 * @return u256 : transformed u256 number
 */
inline u256 fromEvmC(evmc_uint256be const& _n)
{
    return fromBigEndian<u256>(_n.bytes);
}

/**
 * @brief : trans evm address to ethereum address
 * @param _addr : the address that can parsed by evm
 * @return Address : the transformed ethereum address
 */
inline Address fromEvmC(evmc_address const& _addr)
{
    return reinterpret_cast<Address const&>(_addr);
}
}  // namespace eth
}  // namespace dev
