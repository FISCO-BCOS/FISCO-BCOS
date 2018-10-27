/*
    @CopyRight:
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @MPT state interface for EVM
 *
 * @file MPTState.h
 * @author jimmyshi
 * @date 2018-09-21
 */
#pragma once

#include <libexecutivecontext/StateFace.h>
#include <libdevcore/Common.h>
#include <libethcore/Exceptions.h>
#include <libmptstate/State.h>

namespace dev
{
namespace mptstate
{
class MPTState : public dev::eth::StateFace
{
public:
    explicit MPTState(u256 const& _accountStartNonce) : m_state(_accountStartNonce){};

    explicit MPTState(u256 const& _accountStartNonce, OverlayDB const& _db,
        BaseState _bs = BaseState::PreExisting)
      : m_state(_accountStartNonce, _db, _bs){};

    enum NullType
    {
        Null
    };
    MPTState(NullType) : m_state(Invalid256, OverlayDB(), BaseState::Empty){};

    /// Copy state object.
    MPTState(MPTState const& _s) : m_state(_s.m_state) {}

    /// Copy state object.
    MPTState& operator=(MPTState const& _s)
    {
        m_state = _s.m_state;
        return *this;
    }

    static OverlayDB openDB(boost::filesystem::path const& _path, h256 const& _genesisHash,
        WithExisting _we = WithExisting::Trust);

    virtual bool addressInUse(Address const& _address) const override;

    virtual bool accountNonemptyAndExisting(Address const& _address) const override;

    virtual bool addressHasCode(Address const& _address) const override;

    virtual u256 balance(Address const& _id) const override;

    virtual void addBalance(Address const& _id, u256 const& _amount) override;

    virtual void subBalance(Address const& _addr, u256 const& _value) override;

    virtual void setBalance(Address const& _addr, u256 const& _value) override;

    virtual void transferBalance(
        Address const& _from, Address const& _to, u256 const& _value) override;

    virtual h256 storageRoot(Address const& _contract) const override;

    virtual u256 storage(Address const& _contract, u256 const& _memory) const override;

    virtual void setStorage(
        Address const& _contract, u256 const& _location, u256 const& _value) override;

    virtual void clearStorage(Address const& _contract) override;

    virtual void createContract(Address const& _address) override;

    virtual void setCode(Address const& _address, bytes&& _code) override;

    virtual void kill(Address _a) override;

    // virtual std::map<h256, std::pair<u256, u256>> storage(Address const& _contract) const
    // override;

    virtual bytes const code(Address const& _addr) const override;

    virtual h256 codeHash(Address const& _contract) const override;

    virtual size_t codeSize(Address const& _contract) const override;

    virtual void incNonce(Address const& _id) override;

    virtual void setNonce(Address const& _addr, u256 const& _newNonce) override;

    virtual u256 getNonce(Address const& _addr) const override;

    virtual h256 rootHash() const override;

    virtual void commit() override;

    virtual void dbCommit(h256 const& _blockHash, int64_t _blockNumber) override;

    virtual void setRoot(h256 const& _root) override;

    virtual u256 const& accountStartNonce() const override;

    virtual u256 const& requireAccountStartNonce() const override;

    virtual void noteAccountStartNonce(u256 const& _actual) override;

    virtual size_t savepoint() const override;

    virtual void rollback(size_t _savepoint) override;

    virtual void clear() override;

    State& getState();

private:
    State m_state;
};

}  // namespace eth
}  // namespace dev
