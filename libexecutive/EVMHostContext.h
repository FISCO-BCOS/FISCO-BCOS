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
/**
 * @Legacy EVM context
 *
 * @file EVMHostContext.h
 * @author jimmyshi
 * @date 2018-09-21
 */

#pragma once

#include "Common.h"
#include "libethcore/EVMSchedule.h"
#include "libexecutive/Executive.h"
#include "libexecutive/StateFace.h"
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <evmc/instructions.h>
#include <libethcore/Common.h>
#include <functional>
#include <map>

namespace dev
{
namespace executive
{
/// Externality interface for the Virtual Machine providing access to world state.
class EVMHostContext : public evmc_context
{
public:
    /// Full constructor.
    EVMHostContext(std::shared_ptr<executive::StateFace> _s,
        dev::executive::EnvInfo const& _envInfo, Address const& _myAddress, Address const& _caller,
        Address const& _origin, u256 const& _value, u256 const& _gasPrice, bytesConstRef _data,
        const bytes& _code, h256 const& _codeHash, unsigned _depth, bool _isCreate,
        bool _staticCall);
    virtual ~EVMHostContext() = default;

    EVMHostContext(EVMHostContext const&) = delete;
    EVMHostContext& operator=(EVMHostContext const&) = delete;

    /// Read storage location.
    virtual u256 store(u256 const& _n) { return m_s->storage(myAddress(), _n); }

    /// Write a value in storage.
    virtual void setStore(u256 const& _n, u256 const& _v);

    /// Read address's code.
    virtual bytes const codeAt(Address const& _a) { return m_s->code(_a); }

    /// @returns the size of the code in  bytes at the given address.
    virtual size_t codeSizeAt(Address const& _a);

    /// @returns the hash of the code at the given address.
    virtual h256 codeHashAt(Address const& _a);

    /// Create a new contract.
    virtual evmc_result create(
        u256 const& _endowment, u256& io_gas, bytesConstRef _code, evmc_opcode _op, u256 _salt);

    /// Create a new message call.
    virtual evmc_result call(dev::executive::CallParameters& _params);

    /// Read address's balance.
    virtual u256 balance(Address const& _a) { return m_s->balance(_a); }

    /// Does the account exist?
    virtual bool exists(Address const& _a)
    {
        if (evmSchedule().emptinessIsNonexistence())
            return m_s->accountNonemptyAndExisting(_a);
        else
            return m_s->addressInUse(_a);
    }

    /// Suicide the associated contract to the given address.
    virtual void suicide(Address const& _a);

    /// Return the EVM gas-price schedule for this execution context.
    virtual dev::eth::EVMSchedule const& evmSchedule() const { return g_BCOSConfig.evmSchedule(); }

    virtual std::shared_ptr<executive::StateFace> const& state() const { return m_s; }

    /// Hash of a block if within the last 256 blocks, or h256() otherwise.
    virtual h256 blockHash(int64_t _number);

    virtual bool isPermitted();

    /// Get the execution environment information.
    virtual EnvInfo const& envInfo() const { return m_envInfo; }

    /// Revert any changes made (by any of the other calls).
    virtual void log(h256s&& _topics, bytesConstRef _data)
    {
        m_sub.logs.push_back(eth::LogEntry(m_myAddress, std::move(_topics), _data.toBytes()));
    }
    /// ------ get interfaces related to EVMHostContext------
    virtual Address const& myAddress() { return m_myAddress; }
    virtual Address const& caller() { return m_caller; }
    virtual Address const& origin() { return m_origin; }
    virtual u256 const& value() { return m_value; }
    virtual u256 const& gasPrice() { return m_gasPrice; }
    virtual bytesConstRef const& data() { return m_data; }
    virtual bytes const& code() { return m_code; }
    virtual h256 const& codeHash() { return m_codeHash; }
    virtual u256 const& salt() { return m_salt; }
    virtual SubState& sub() { return m_sub; }
    virtual unsigned const& depth() { return m_depth; }
    virtual bool const& isCreate() { return m_isCreate; }
    virtual bool const& staticCall() { return m_staticCall; }
    /// ------ set interfaces related to EVMHostContext------
    virtual void setMyAddress(Address const& _contractAddr) { m_myAddress = _contractAddr; }
    virtual void setCaller(Address const& _senderAddr) { m_caller = _senderAddr; }
    virtual void setOrigin(Address const& _origin) { m_origin = _origin; }
    virtual void setValue(u256 const& _value) { m_value = _value; }
    virtual void setGasePrice(u256 const& _gasPrice) { m_gasPrice = _gasPrice; }
    virtual void setData(bytesConstRef _data) { m_data = _data; }
    virtual void setCode(bytes& _code) { m_code = _code; }
    virtual void setCodeHash(h256 const& _codeHash) { m_codeHash = _codeHash; }
    virtual void setSalt(u256 const& _salt) { m_salt = _salt; }
    virtual void setSub(SubState _sub) { m_sub = _sub; }
    virtual void setDepth(unsigned _depth) { m_depth = _depth; }
    virtual void setCreate(bool _isCreate) { m_isCreate = _isCreate; }
    virtual void setStaticCall(bool _staticCall) { m_staticCall = _staticCall; }

    virtual VMFlagType evmFlags() const { return flags; }
    virtual void setEvmFlags(VMFlagType const& _evmFlags) { flags = _evmFlags; }

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

private:
    std::shared_ptr<executive::StateFace> m_s;  ///< A reference to the base state.
};

}  // namespace executive
}  // namespace dev
