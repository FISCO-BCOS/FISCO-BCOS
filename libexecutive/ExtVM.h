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
 * @file ExtVM.h
 * @author jimmyshi
 * @date 2018-09-21
 */

#pragma once

#include "ExecutionResult.h"
#include "Executive.h"
#include "StateFace.h"
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <libethcore/Common.h>
#include <libethcore/EVMSchedule.h>
#include <libevm/ExtVMFace.h>
#include <functional>
#include <map>


namespace dev
{
namespace executive
{
/// Externality interface for the Virtual Machine providing access to world state.
class ExtVM : public dev::eth::ExtVMFace
{
public:
    /// Full constructor.
    ExtVM(std::shared_ptr<StateFace> _s, dev::eth::EnvInfo const& _envInfo,
        Address const& _myAddress, Address const& _caller, Address const& _origin,
        u256 const& _value, u256 const& _gasPrice, bytesConstRef _data, bytesConstRef _code,
        h256 const& _codeHash, unsigned _depth, bool _isCreate, bool _staticCall)
      : ExtVMFace(_envInfo, _myAddress, _caller, _origin, _value, _gasPrice, _data, _code.toBytes(),
            _codeHash, _depth, _isCreate, _staticCall),
        m_s(_s)
    {
        // Contract: processing account must exist. In case of CALL, the ExtVM
        // is created only if an account has code (so exist). In case of CREATE
        // the account must be created first.
        assert(m_s->addressInUse(_myAddress));
    }

    /// Read storage location.
    u256 store(u256 const& _n) final { return m_s->storage(myAddress(), _n); }

    /// Write a value in storage.
    void setStore(u256 const& _n, u256 const& _v) final;

    /// Read address's code.
    bytes const codeAt(Address const& _a) final { return m_s->code(_a); }

    /// @returns the size of the code in  bytes at the given address.
    size_t codeSizeAt(Address const& _a) final;

    /// @returns the hash of the code at the given address.
    h256 codeHashAt(Address const& _a) final;

    /// Create a new contract.
    evmc_result create(u256 const& _endowment, u256& io_gas, bytesConstRef _code,
        dev::eth::Instruction _op, u256 _salt, dev::eth::OnOpFunc const& _onOp = {}) final;

    /// Create a new message call.
    evmc_result call(dev::eth::CallParameters& _params) final;

    /// Read address's balance.
    u256 balance(Address const& _a) final { return m_s->balance(_a); }

    /// Does the account exist?
    bool exists(Address const& _a) final
    {
        if (evmSchedule().emptinessIsNonexistence())
            return m_s->accountNonemptyAndExisting(_a);
        else
            return m_s->addressInUse(_a);
    }

    /// Suicide the associated contract to the given address.
    void suicide(Address const& _a) final;

    /// Return the EVM gas-price schedule for this execution context.
    dev::eth::EVMSchedule const& evmSchedule() const final { return g_BCOSConfig.evmSchedule(); }

    std::shared_ptr<StateFace> const& state() const { return m_s; }

    /// Hash of a block if within the last 256 blocks, or h256() otherwise.
    h256 blockHash(int64_t _number) final;

    bool isPermitted();

private:
    std::shared_ptr<StateFace> m_s;  ///< A reference to the base state.
};

}  // namespace executive
}  // namespace dev
