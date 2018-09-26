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
 * @file MptState.h
 * @author jimmyshi
 * @date 2018-09-21
 */

#include "StateFace.h"
#include <libdevcore/Common.h>
#include <libdevcore/Guards.h>
#include <libethcore/Exceptions.h>
#include <libmptstate/State.h>

namespace dev
{
namespace eth
{
class MptState : public StateFace
{
public:
    explicit MptState(u256 const& _accountStartNonce) : m_state(_accountStartNonce){};

    explicit MptState(u256 const& _accountStartNonce, OverlayDB const& _db,
        BaseState _bs = BaseState::PreExisting)
      : m_state(_accountStartNonce, _db, _bs){};

    enum NullType
    {
        Null
    };
    MptState(NullType) : m_state(Invalid256, OverlayDB(), BaseState::Empty){};

    /// Copy state object.
    MptState(MptState const& _s) : m_state(_s.m_state) {}

    /// Copy state object.
    MptState& operator=(MptState const& _s)
    {
        m_state = _s.m_state;
        return *this;
    }

    static OverlayDB openDB(boost::filesystem::path const& _path, h256 const& _genesisHash,
        WithExisting _we = WithExisting::Trust);

    virtual bool addressInUse(Address const& _address) const;

    virtual bool accountNonemptyAndExisting(Address const& _address) const;

    virtual bool addressHasCode(Address const& _address) const;

    virtual u256 balance(Address const& _id) const;

    virtual void addBalance(Address const& _id, u256 const& _amount);

    virtual void subBalance(Address const& _addr, u256 const& _value);

    virtual void setBalance(Address const& _addr, u256 const& _value);

    virtual void transferBalance(Address const& _from, Address const& _to, u256 const& _value);

    virtual h256 storageRoot(Address const& _contract) const;

    virtual u256 storage(Address const& _contract, u256 const& _memory) const;

    virtual void setStorage(Address const& _contract, u256 const& _location, u256 const& _value);

    virtual void clearStorage(Address const& _contract);

    virtual void createContract(Address const& _address);

    virtual void setCode(Address const& _address, bytes&& _code);

    virtual void kill(Address _a);

    // virtual std::map<h256, std::pair<u256, u256>> storage(Address const& _contract) const;

    virtual bytes const& code(Address const& _addr) const;

    virtual h256 codeHash(Address const& _contract) const;

    virtual size_t codeSize(Address const& _contract) const;

    virtual void incNonce(Address const& _id);

    virtual void setNonce(Address const& _addr, u256 const& _newNonce);

    virtual u256 getNonce(Address const& _addr) const;

    virtual h256 rootHash() const;

    virtual void commit(StateFace::CommitBehaviour _commitBehaviour);

    virtual void DbCommit();

    virtual void setRoot(h256 const& _root);

    virtual u256 const& accountStartNonce() const;

    virtual u256 const& requireAccountStartNonce() const;

    virtual void noteAccountStartNonce(u256 const& _actual);

    virtual size_t savepoint() const;

    virtual void rollback(size_t _savepoint);

    virtual void clear();

    State& getState();

private:
    State m_state;
};

}  // namespace eth
}  // namespace dev