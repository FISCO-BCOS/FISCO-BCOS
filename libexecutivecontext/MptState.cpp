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
 * @file MptState.cpp
 * @author jimmyshi
 * @date 2018-09-21
 */

#include "MptState.h"

namespace dev
{
namespace eth
{
OverlayDB MptState::openDB(
    boost::filesystem::path const& _path, h256 const& _genesisHash, WithExisting _we)
{
    std::mutex open_db_lock;
    Guard l(open_db_lock);
    return State::openDB(_path, _genesisHash, _we);
};

bool MptState::addressInUse(Address const& _address) const
{
    return m_state.addressInUse(_address);
}

bool MptState::accountNonemptyAndExisting(Address const& _address) const
{
    return m_state.accountNonemptyAndExisting(_address);
}

bool MptState::addressHasCode(Address const& _address) const
{
    return m_state.addressHasCode(_address);
}

u256 MptState::balance(Address const& _id) const
{
    return m_state.balance(_id);
}

void MptState::addBalance(Address const& _id, u256 const& _amount)
{
    m_state.addBalance(_id, _amount);
}

void MptState::subBalance(Address const& _addr, u256 const& _value)
{
    m_state.subBalance(_addr, _value);
}

void MptState::setBalance(Address const& _addr, u256 const& _value)
{
    m_state.setBalance(_addr, _value);
}

void MptState::transferBalance(Address const& _from, Address const& _to, u256 const& _value)
{
    m_state.transferBalance(_from, _to, _value);
}

h256 MptState::storageRoot(Address const& _contract) const
{
    return m_state.storageRoot(_contract);
}

u256 MptState::storage(Address const& _contract, u256 const& _memory) const
{
    return m_state.storage(_contract, _memory);
}

void MptState::setStorage(Address const& _contract, u256 const& _location, u256 const& _value)
{
    m_state.setStorage(_contract, _location, _value);
}

void MptState::clearStorage(Address const& _contract)
{
    m_state.clearStorage(_contract);
}

void MptState::createContract(Address const& _address)
{
    m_state.createContract(_address);
}

void MptState::setCode(Address const& _address, bytes&& _code)
{
    m_state.setCode(_address, std::move(_code));
}

void MptState::kill(Address _a)
{
    m_state.kill(_a);
}

bytes const& MptState::code(Address const& _addr) const
{
    return m_state.code(_addr);
}

h256 MptState::codeHash(Address const& _contract) const
{
    return m_state.codeHash(_contract);
}

size_t MptState::codeSize(Address const& _contract) const
{
    return m_state.codeSize(_contract);
}

void MptState::incNonce(Address const& _id)
{
    m_state.incNonce(_id);
}

void MptState::setNonce(Address const& _addr, u256 const& _newNonce)
{
    m_state.setNonce(_addr, _newNonce);
}

u256 MptState::getNonce(Address const& _addr) const
{
    return m_state.getNonce(_addr);
}

h256 MptState::rootHash() const
{
    return m_state.rootHash();
}

void MptState::commit(StateFace::CommitBehaviour _commitBehaviour)
{
    if (_commitBehaviour == CommitBehaviour::KeepEmptyAccounts)
        m_state.commit(State::CommitBehaviour::KeepEmptyAccounts);
    else if (_commitBehaviour == CommitBehaviour::RemoveEmptyAccounts)
        m_state.commit(State::CommitBehaviour::RemoveEmptyAccounts);
    else
        BOOST_THROW_EXCEPTION(InvalidCommitBehaviour());
}

void MptState::DbCommit()
{
    m_state.db().commit();
}

void MptState::setRoot(h256 const& _root)
{
    m_state.setRoot(_root);
}

u256 const& MptState::accountStartNonce() const
{
    return m_state.accountStartNonce();
}

u256 const& MptState::requireAccountStartNonce() const
{
    return m_state.requireAccountStartNonce();
}

void MptState::noteAccountStartNonce(u256 const& _actual)
{
    m_state.noteAccountStartNonce(_actual);
}

size_t MptState::savepoint() const
{
    return m_state.savepoint();
}

void MptState::rollback(size_t _savepoint)
{
    m_state.rollback(_savepoint);
}

void MptState::clear()
{
    m_state.cacheClear();
}

State& MptState::getState()
{
    return m_state;
}

}  // namespace eth
}  // namespace dev