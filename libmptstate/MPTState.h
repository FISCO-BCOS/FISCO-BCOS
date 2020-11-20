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

#include <libethcore/Exceptions.h>
#include <libexecutive/StateFace.h>
#include <libmptstate/State.h>
#include <libutilities/Common.h>

namespace bcos
{
namespace mptstate
{
class MPTState : public bcos::executive::StateFace
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
    virtual ~MPTState() = default;
    /// Copy state object.
    MPTState& operator=(MPTState const& _s)
    {
        m_state = _s.m_state;
        return *this;
    }

    static OverlayDB openDB(boost::filesystem::path const& _path, h256 const& _genesisHash,
        WithExisting _we = WithExisting::Trust);

    bool addressInUse(Address const& _address) const override;

    bool accountNonemptyAndExisting(Address const& _address) const override;

    bool addressHasCode(Address const& _address) const override;

    u256 balance(Address const& _id) const override;

    void addBalance(Address const& _id, u256 const& _amount) override;

    void subBalance(Address const& _addr, u256 const& _value) override;

    void setBalance(Address const& _addr, u256 const& _value) override;

    void transferBalance(Address const& _from, Address const& _to, u256 const& _value) override;

    h256 storageRoot(Address const& _contract) const override;

    u256 storage(Address const& _contract, u256 const& _memory) override;

    void setStorage(Address const& _contract, u256 const& _location, u256 const& _value) override;

    void clearStorage(Address const& _contract) override;

    void createContract(Address const& _address) override;

    void setCode(Address const& _address, bytes&& _code) override;

    void kill(Address _a) override;

    bytes const code(Address const& _addr) const override;

    h256 codeHash(Address const& _contract) const override;

    bool frozen(Address const& _contract) const override;

    size_t codeSize(Address const& _contract) const override;

    void incNonce(Address const& _id) override;

    void setNonce(Address const& _addr, u256 const& _newNonce) override;

    u256 getNonce(Address const& _addr) const override;

    h256 rootHash(bool _needCal = true) const override;
    void commit() override;

    void dbCommit(h256 const& _blockHash, int64_t _blockNumber) override;

    void setRoot(h256 const& _root) override;

    u256 const& accountStartNonce() const override;

    u256 const& requireAccountStartNonce() const override;

    void noteAccountStartNonce(u256 const& _actual) override;

    size_t savepoint() const override;

    void rollback(size_t _savepoint) override;

    void clear() override;

    bool checkAuthority(Address const& _origin, Address const& _contract) const override;

    State& getState();

private:
    State m_state;
};

}  // namespace mptstate
}  // namespace bcos
