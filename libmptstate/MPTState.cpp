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
 * @Mpt state interface for EVM
 *
 * @file MPTState.cpp
 * @author jimmyshi
 * @date 2018-09-21
 */

#include "MPTState.h"

namespace dev
{
namespace mptstate
{
OverlayDB MPTState::openDB(
    boost::filesystem::path const& _path, h256 const& _genesisHash, WithExisting _we)
{
    return State::openDB(_path, _genesisHash, _we);
}

bool MPTState::addressInUse(Address const& _address) const
{
    return m_state.addressInUse(_address);
}

bool MPTState::accountNonemptyAndExisting(Address const& _address) const
{
    return m_state.accountNonemptyAndExisting(_address);
}

bool MPTState::addressHasCode(Address const& _address) const
{
    return m_state.addressHasCode(_address);
}

u256 MPTState::balance(Address const& _id) const
{
    return m_state.balance(_id);
}

void MPTState::addBalance(Address const& _id, u256 const& _amount)
{
    m_state.addBalance(_id, _amount);
}

void MPTState::subBalance(Address const& _addr, u256 const& _value)
{
    m_state.subBalance(_addr, _value);
}

void MPTState::setBalance(Address const& _addr, u256 const& _value)
{
    m_state.setBalance(_addr, _value);
}

void MPTState::transferBalance(Address const& _from, Address const& _to, u256 const& _value)
{
    m_state.transferBalance(_from, _to, _value);
}

h256 MPTState::storageRoot(Address const& _contract) const
{
    return m_state.storageRoot(_contract);
}

std::string MPTState::get(Address const&, const std::string&)
{
    // FIXME: not support mpt state, abandon mpt state
    abort();
    return 0;
}

void MPTState::set(Address const&, const std::string&, const std::string&)
{
    // FIXME: not support mpt state, abandon mpt state
    abort();
}

u256 MPTState::storage(Address const& _contract, u256 const& _memory)
{
    return m_state.storage(_contract, _memory);
}

void MPTState::setStorage(Address const& _contract, u256 const& _location, u256 const& _value)
{
    m_state.setStorage(_contract, _location, _value);
}

void MPTState::clearStorage(Address const& _contract)
{
    m_state.clearStorage(_contract);
}

void MPTState::createContract(Address const& _address)
{
    m_state.createContract(_address);
}

void MPTState::setCode(Address const& _address, bytes&& _code)
{
    m_state.setCode(_address, std::move(_code));
}

void MPTState::kill(Address _a)
{
    m_state.kill(_a);
}

bytes const MPTState::code(Address const& _addr) const
{
    return m_state.code(_addr);
}

h256 MPTState::codeHash(Address const& _contract) const
{
    return m_state.codeHash(_contract);
}

bool MPTState::frozen(Address const& _contract) const
{
    (void)_contract;
    return false;
}

size_t MPTState::codeSize(Address const& _contract) const
{
    return m_state.codeSize(_contract);
}

void MPTState::incNonce(Address const& _id)
{
    m_state.incNonce(_id);
}

void MPTState::setNonce(Address const& _addr, u256 const& _newNonce)
{
    m_state.setNonce(_addr, _newNonce);
}

u256 MPTState::getNonce(Address const& _addr) const
{
    return m_state.getNonce(_addr);
}

h256 MPTState::rootHash(bool) const
{
    return m_state.rootHash();
}

void MPTState::commit()
{
    m_state.commit();
}

void MPTState::dbCommit(h256 const&, int64_t)
{
    m_state.db().commit();
}

void MPTState::setRoot(h256 const& _root)
{
    m_state.setRoot(_root);
}

u256 const& MPTState::accountStartNonce() const
{
    return m_state.accountStartNonce();
}

u256 const& MPTState::requireAccountStartNonce() const
{
    return m_state.requireAccountStartNonce();
}

void MPTState::noteAccountStartNonce(u256 const& _actual)
{
    m_state.noteAccountStartNonce(_actual);
}

size_t MPTState::savepoint() const
{
    return m_state.savepoint();
}

void MPTState::rollback(size_t _savepoint)
{
    m_state.rollback(_savepoint);
}

void MPTState::clear()
{
    m_state.cacheClear();
}

bool MPTState::checkAuthority(Address const&, Address const&) const
{
    return true;
}

State& MPTState::getState()
{
    return m_state;
}

}  // namespace mptstate
}  // namespace dev
