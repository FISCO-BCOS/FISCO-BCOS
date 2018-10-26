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
 * @AMDB Storage interface for EVM
 *
 * @file StorageState.h
 * @author xingqiangbai
 * @date 2018-10-22
 */

#pragma once
#include "libexecutivecontext/StateFace.h"

namespace dev
{
namespace storage
{
class Table;
class MemoryTableFactory;
const char* const STORAGE_KEY = "key";
const char* const STORAGE_VALUE = "value";
const char* const ACCOUNT_BALANCE = "balance";
const char* const ACCOUNT_CODE_HASH = "codeHash";
const char* const ACCOUNT_CODE = "code";
const char* const ACCOUNT_NONCE = "nonce";
const char* const ACCOUNT_ALIVE = "alive";
}  // namespace storage
namespace eth
{
class StorageState : public StateFace
{
public:
    explicit StorageState(u256 const& _accountStartNonce)
      : m_accountStartNonce(_accountStartNonce), m_memoryTableFactory(nullptr){};

    /// Check if the address is in use.
    virtual bool addressInUse(Address const& _address) const override;

    /// Check if the account exists in the state and is non empty (nonce > 0 || balance > 0 || code
    /// nonempty and suiside != 1). These two notions are equivalent after EIP158.
    virtual bool accountNonemptyAndExisting(Address const& _address) const override;

    /// Check if the address contains executable code.
    virtual bool addressHasCode(Address const& _address) const override;

    /// Get an account's balance.
    /// @returns 0 if the address has never been used.
    virtual u256 balance(Address const& _address) const override;

    /// Add some amount to balance.
    /// Will initialise the address if it has never been used.
    virtual void addBalance(Address const& _address, u256 const& _amount) override;

    /// Subtract the @p _value amount from the balance of @p _address account.
    /// @throws NotEnoughCash if the balance of the account is less than the
    /// amount to be subtrackted (also in case the account does not exist).
    virtual void subBalance(Address const& _address, u256 const& _value) override;

    /// Set the balance of @p _address to @p _value.
    /// Will instantiate the address if it has never been used.
    virtual void setBalance(Address const& _address, u256 const& _value) override;

    /**
     * @brief Transfers "the balance @a _value between two accounts.
     * @param _from Account from which @a _value will be deducted.
     * @param _to Account to which @a _value will be added.
     * @param _value Amount to be transferred.
     */
    virtual void transferBalance(
        Address const& _from, Address const& _to, u256 const& _value) override;

    /// Get the root of the storage of an account.
    virtual h256 storageRoot(Address const& _contract) const override;

    /// Get the value of a storage position of an account.
    /// @returns 0 if no account exists at that address.
    virtual u256 storage(Address const& _contract, u256 const& _memory) const override;

    /// Set the value of a storage position of an account.
    virtual void setStorage(
        Address const& _contract, u256 const& _location, u256 const& _value) override;

    /// Clear the storage root hash of an account to the hash of the empty trie.
    virtual void clearStorage(Address const& _contract) override;

    /// Create a contract at the given address (with unset code and unchanged balance).
    virtual void createContract(Address const& _address) override;

    /// Sets the code of the account. Must only be called during / after contract creation.
    virtual void setCode(Address const& _address, bytes&& _code) override;

    /// Delete an account (used for processing suicides). (set suicides key = 1 when use AMDB)
    virtual void kill(Address _a) override;

    /// Get the storage of an account.
    /// @note This is expensive. Don't use it unless you need to.
    /// @returns map of hashed keys to key-value pairs or empty map if no account exists at that
    /// address.
    // virtual std::map<h256, std::pair<u256, u256>> storage(Address const& _contract) const
    // override;

    /// Get the code of an account.
    /// @returns bytes() if no account exists at that address.
    /// @warning The reference to the code is only valid until the access to
    ///          other account. Do not keep it.
    virtual bytes const code(Address const& _address) const override;

    /// Get the code hash of an account.
    /// @returns EmptySHA3 if no account exists at that address or if there is no code associated
    /// with the address.
    virtual h256 codeHash(Address const& _contract) const override;

    /// Get the byte-size of the code of an account.
    /// @returns code(_contract).size(), but utilizes CodeSizeHash.
    virtual size_t codeSize(Address const& _contract) const override;

    /// Increament the account nonce.
    virtual void incNonce(Address const& _address) override;

    /// Set the account nonce.
    virtual void setNonce(Address const& _address, u256 const& _newNonce) override;

    /// Get the account nonce -- the number of transactions it has sent.
    /// @returns 0 if the address has never been used.
    virtual u256 getNonce(Address const& _address) const override;

    /// The hash of the root of our state tree.
    virtual h256 rootHash() const override;

    /// Commit all changes waiting in the address cache to the DB.
    /// @param _commitBehaviour whether or not to remove empty accounts during commit.
    virtual void commit() override;

    /// Commit levelDB data into hardisk or commit AMDB data into database (Called after commit())
    /// @param _commitBehaviour whether or not to remove empty accounts during commit.
    virtual void dbCommit(h256 const& _blockHash, int64_t _blockNumber) override;

    /// Resets any uncommitted changes to the cache. Return a new root in params &root
    virtual void setRoot(h256 const& _root) override;

    /// Get the account start nonce. May be required.
    virtual u256 const& accountStartNonce() const override;
    virtual u256 const& requireAccountStartNonce() const override;
    virtual void noteAccountStartNonce(u256 const& _actual) override;

    /// Create a savepoint in the state changelog.	///
    /// @return The savepoint index that can be used in rollback() function.
    virtual size_t savepoint() const override;

    /// Revert all recent changes up to the given @p _savepoint savepoint.
    virtual void rollback(size_t _savepoint) override;

    /// Clear state's cache
    virtual void clear() override;
    void setMemoryTableFactory(
        std::shared_ptr<dev::storage::MemoryTableFactory> _memoryTableFactory)
    {
        m_memoryTableFactory = _memoryTableFactory;
    }

private:
    void createAccount(Address const& _address, u256 const& _nonce, u256 const& _amount = u256());
    std::shared_ptr<dev::storage::Table> getTable(Address const& _address) const;
    u256 m_accountStartNonce;
    std::shared_ptr<dev::storage::MemoryTableFactory> m_memoryTableFactory;
};

}  // namespace eth
}  // namespace dev
