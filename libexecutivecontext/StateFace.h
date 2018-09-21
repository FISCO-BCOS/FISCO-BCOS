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
 * @state base interface for EVM
 *
 * @file StateFace.h
 * @author jimmyshi
 * @date 2018-09-21
 */

#pragma once

#include <libethcore/Common.h>

namespace dev
{
namespace eth
{
class StateFace
{
public:
    /// Get the value of a storage position of an account.
    /// @returns 0 if no account exists at that address.
    virtual u256 storage(Address const& _contract, u256 const& _memory) const = 0;

    /// Set the value of a storage position of an account.
    virtual void setStorage(
        Address const& _contract, u256 const& _location, u256 const& _value) = 0;

    /// Get the code of an account.
    /// @returns bytes() if no account exists at that address.
    /// @warning The reference to the code is only valid until the access to
    ///          other account. Do not keep it.
    virtual bytes const& code(Address const& _addr) const = 0;

    /// Get the byte-size of the code of an account.
    /// @returns code(_contract).size(), but utilizes CodeSizeHash.
    virtual size_t codeSize(Address const& _contract) const = 0;

    /// Get the code hash of an account.
    /// @returns EmptySHA3 if no account exists at that address or if there is no code associated
    /// with the address.
    virtual h256 codeHash(Address const& _contract) const = 0;

    /// Sets the code of the account. Must only be called during / after contract creation.
    virtual void setCode(Address const& _address, bytes&& _code) = 0;

    /// Get an account's balance.
    /// @returns 0 if the address has never been used.
    virtual u256 balance(Address const& _id) const = 0;

    /// Check if the account exists in the state and is non empty (nonce > 0 || balance > 0 || code
    /// nonempty). These two notions are equivalent after EIP158.
    virtual bool accountNonemptyAndExisting(Address const& _address) const = 0;

    /// Check if the address is in use.
    virtual bool addressInUse(Address const& _address) const = 0;

    /// Add some amount to balance.
    /// Will initialise the address if it has never been used.
    virtual void addBalance(Address const& _id, u256 const& _amount) = 0;

    /// Subtract the @p _value amount from the balance of @p _addr account.
    /// @throws NotEnoughCash if the balance of the account is less than the
    /// amount to be subtrackted (also in case the account does not exist).
    virtual void subBalance(Address const& _addr, u256 const& _value) = 0;

    /// Set the balance of @p _addr to @p _value.
    /// Will instantiate the address if it has never been used.
    virtual void setBalance(Address const& _addr, u256 const& _value) = 0;

    /// Get the account nonce -- the number of transactions it has sent.
    /// @returns 0 if the address has never been used.
    virtual u256 getNonce(Address const& _addr) const = 0;
};

}  // namespace eth
}  // namespace dev
