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
    return State::openDB(_path, _genesisHash, _we);
};

u256 MptState::storage(Address const& _contract, u256 const& _memory) const
{
    return m_state.storage(_contract, _memory);
}

void MptState::setStorage(Address const& _contract, u256 const& _location, u256 const& _value)
{
    m_state.setStorage(_contract, _location, _value);
}

bytes const& MptState::code(Address const& _addr) const
{
    return m_state.code(_addr);
}

size_t MptState::codeSize(Address const& _contract) const
{
    return m_state.codeSize(_contract);
}

h256 MptState::codeHash(Address const& _contract) const
{
    return m_state.codeHash(_contract);
}

void MptState::setCode(Address const& _address, bytes&& _code)
{
    m_state.setCode(_address, std::move(_code));
}

u256 MptState::balance(Address const& _id) const
{
    return m_state.balance(_id);
}

bool MptState::accountNonemptyAndExisting(Address const& _address) const
{
    return m_state.accountNonemptyAndExisting(_address);
}

bool MptState::addressInUse(Address const& _address) const
{
    return m_state.addressInUse(_address);
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

u256 MptState::getNonce(Address const& _addr) const
{
    return m_state.getNonce(_addr);
}

State& MptState::getState()
{
    return m_state;
}

}  // namespace eth
}  // namespace dev