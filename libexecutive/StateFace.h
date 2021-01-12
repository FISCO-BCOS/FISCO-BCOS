/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
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
namespace executive
{
enum class StateType
{
    MptState,
    StorageState
};

class StateFace
{
public:
    /// Check if the address is in use.
    virtual bool addressInUse(Address const& _address) const = 0;

    /// Check if the account exists in the state and is non empty (nonce > 0 || balance > 0 || code
    /// nonempty and suiside != 1). These two notions are equivalent after EIP158.
    virtual bool accountNonemptyAndExisting(Address const& _address) const = 0;

    /// Check if the address contains executable code.
    virtual bool addressHasCode(Address const& _address) const = 0;

    /// Get an account's balance.
    /// @returns 0 if the address has never been used.
    virtual u256 balance(Address const& _id) const = 0;

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

    /**
     * @brief Transfers "the balance @a _value between two accounts.
     * @param _from Account from which @a _value will be deducted.
     * @param _to Account to which @a _value will be added.
     * @param _value Amount to be transferred.
     */
    virtual void transferBalance(Address const& _from, Address const& _to, u256 const& _value) = 0;

    /// Get the root of the storage of an account.
    virtual h256 storageRoot(Address const& _contract) const = 0;

    /// Get the value of a storage position of an account.
    /// @returns 0 if no account exists at that address.
    virtual u256 storage(Address const& _contract, u256 const& _memory) = 0;

    /// Set the value of a storage position of an account.
    virtual void setStorage(
        Address const& _contract, u256 const& _location, u256 const& _value) = 0;

    /// Clear the storage root hash of an account to the hash of the empty trie.
    virtual void clearStorage(Address const& _contract) = 0;

    /// Create a contract at the given address (with unset code and unchanged balance).
    virtual void createContract(Address const& _address) = 0;

    /// Sets the code of the account. Must only be called during / after contract creation.
    virtual void setCode(Address const& _address, bytes&& _code) = 0;

    /// Delete an account (used for processing suicides). (set suicides key = 1 when use AMDB)
    virtual void kill(Address _a) = 0;

    /// Get the storage of an account.
    /// @note This is expensive. Don't use it unless you need to.
    /// @returns map of hashed keys to key-value pairs or empty map if no account exists at that
    /// address.
    // virtual std::map<h256, std::pair<u256, u256>> storage(Address const& _contract) const = 0;

    /// Get the code of an account.
    /// @returns bytes() if no account exists at that address.
    /// @warning The reference to the code is only valid until the access to
    ///          other account. Do not keep it.
    virtual bytes const code(Address const& _addr) const = 0;

    /// Get the code hash of an account.
    /// @returns EmptyHash if no account exists at that address or if there is no code
    /// associated with the address.
    virtual h256 codeHash(Address const& _contract) const = 0;

    /// Get the frozen status of an account.
    /// @returns ture if the account is frozen.
    virtual bool frozen(Address const& _contract) const = 0;

    /// Get the byte-size of the code of an account.
    /// @returns code(_contract).size(), but utilizes CodeSizeHash.
    virtual size_t codeSize(Address const& _contract) const = 0;

    /// Increament the account nonce.
    virtual void incNonce(Address const& _id) = 0;

    /// Set the account nonce.
    virtual void setNonce(Address const& _addr, u256 const& _newNonce) = 0;

    /// Get the account nonce -- the number of transactions it has sent.
    /// @returns 0 if the address has never been used.
    virtual u256 getNonce(Address const& _addr) const = 0;

    /// The hash of the root of our state tree.
    virtual h256 rootHash(bool _needCal = true) const = 0;
    /// Commit all changes waiting in the address cache to the DB.
    /// @param _commitBehaviour whether or not to remove empty accounts during commit.
    virtual void commit() = 0;

    /// Commit levelDB data into hardisk or commit AMDB data into database (Called after commit())
    /// @param _commitBehaviour whether or not to remove empty accounts during commit.
    virtual void dbCommit(h256 const& _blockHash, int64_t _blockNumber) = 0;

    /// Resets any uncommitted changes to the cache. Return a new root in params &root
    virtual void setRoot(h256 const& _root) = 0;

    /// Get the account start nonce. May be required.
    virtual u256 const& accountStartNonce() const = 0;
    virtual u256 const& requireAccountStartNonce() const = 0;
    virtual void noteAccountStartNonce(u256 const& _actual) = 0;

    /// Create a savepoint in the state changelog.	///
    /// @return The savepoint index that can be used in rollback() function.
    virtual size_t savepoint() const = 0;

    /// Revert all recent changes up to the given @p _savepoint savepoint.
    virtual void rollback(size_t _savepoint) = 0;

    /// Clear state's cache
    virtual void clear() = 0;
    /// Check authority
    virtual bool checkAuthority(Address const& _origin, Address const& _contract) const = 0;

    // get the remainGas of the given account
    virtual std::pair<bool, u256> remainGas(Address const&) { return std::make_pair(false, 0); };

    virtual bool updateRemainGas(Address const&, u256 const&, Address const&) { return false; };
    // get the stateType
    virtual StateType getStateType() = 0;
};

}  // namespace executive
}  // namespace dev
