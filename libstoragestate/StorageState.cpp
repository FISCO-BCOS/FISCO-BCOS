/*7
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
 * @Mpt state interface for EVM
 *
 * @file StorageState.cpp
 * @author xingqiangbai
 * @date 2018-10-22
 */

#include "StorageState.h"
#include "libdevcrypto/Hash.h"
#include "libethcore/Exceptions.h"
#include "libstorage/Table.h"

using namespace dev;
using namespace dev::eth;
using namespace dev::storagestate;
using namespace dev::storage;
using namespace dev::executive;

bool StorageState::addressInUse(Address const& _address) const
{
    auto table = getTable(_address);
    // if (table && !table->empty())
    if (table)
    {
        return true;
    }
    return false;
}

bool StorageState::accountNonemptyAndExisting(Address const& _address) const
{
    auto table = getTable(_address);
    if (table)
    {
        if (balance(_address) > u256(0) || codeHash(_address) != EmptySHA3 ||
            getNonce(_address) != m_accountStartNonce)
            return true;
    }
    return false;
}

bool StorageState::addressHasCode(Address const& _address) const
{
    auto table = getTable(_address);
    if (table)
    {
        auto entries = table->select(ACCOUNT_CODE_HASH, table->newCondition());
        if (entries->size() != 0u)
        {
            auto codeHash = h256(fromHex(entries->get(0)->getField(STORAGE_VALUE)));
            return codeHash != EmptySHA3;
        }
    }
    return false;
}

u256 StorageState::balance(Address const& _address) const
{
    auto table = getTable(_address);
    if (table)
    {
        auto entries = table->select(ACCOUNT_BALANCE, table->newCondition());
        if (entries->size() != 0u)
        {
            return u256(entries->get(0)->getField(STORAGE_VALUE));
        }
    }
    return 0;
}

void StorageState::addBalance(Address const& _address, u256 const& _amount)
{
    if (_amount == 0)
    {
        return;
    }
    auto table = getTable(_address);
    if (table)
    {
        auto entries = table->select(ACCOUNT_BALANCE, table->newCondition());
        if (entries->size() != 0u)
        {
            auto option = std::make_shared<AccessOptions>(Address(), false);
            auto entry = entries->get(0);
            auto balance = u256(entry->getField(STORAGE_VALUE));
            balance += _amount;
            Entry::Ptr updateEntry = table->newEntry();
            updateEntry->setField(STORAGE_VALUE, balance.str());
            table->update(ACCOUNT_BALANCE, updateEntry, table->newCondition(), option);
        }
    }
    else
    {
        createAccount(_address, requireAccountStartNonce(), _amount);
    }
}

void StorageState::subBalance(Address const& _address, u256 const& _amount)
{
    auto table = getTable(_address);
    if (table)
    {
        auto entries = table->select(ACCOUNT_BALANCE, table->newCondition());
        if (entries->size() != 0u)
        {
            auto entry = entries->get(0);
            auto balance = u256(entry->getField(STORAGE_VALUE));
            if (balance < _amount)
                BOOST_THROW_EXCEPTION(NotEnoughCash());
            balance -= _amount;
            Entry::Ptr updateEntry = table->newEntry();
            auto option = std::make_shared<AccessOptions>(Address(), false);
            updateEntry->setField(STORAGE_VALUE, balance.str());
            table->update(ACCOUNT_BALANCE, updateEntry, table->newCondition(), option);
        }
    }
    else
    {
        BOOST_THROW_EXCEPTION(NotEnoughCash());
    }
}

void StorageState::setBalance(Address const& _address, u256 const& _amount)
{
    auto table = getTable(_address);
    if (table)
    {
        auto entries = table->select(ACCOUNT_BALANCE, table->newCondition());
        if (entries->size() != 0u)
        {
            auto entry = entries->get(0);
            auto balance = u256(entry->getField(STORAGE_VALUE));
            balance = _amount;
            auto option = std::make_shared<AccessOptions>(Address(), false);
            Entry::Ptr updateEntry = table->newEntry();
            updateEntry->setField(STORAGE_VALUE, balance.str());
            table->update(ACCOUNT_BALANCE, updateEntry, table->newCondition(), option);
        }
    }
    else
    {
        createAccount(_address, requireAccountStartNonce(), _amount);
    }
}

void StorageState::transferBalance(Address const& _from, Address const& _to, u256 const& _value)
{
    subBalance(_from, _value);
    addBalance(_to, _value);
}

h256 StorageState::storageRoot(Address const& _address) const
{
    auto table = getTable(_address);
    if (table)
    {
        return table->hash();
    }
    return h256();
}

u256 StorageState::storage(Address const& _address, u256 const& _key)
{
    auto table = getTable(_address);
    if (table)
    {
        auto entries = table->select(_key.str(), table->newCondition());
        if (entries->size() != 0u)
        {
            return u256(entries->get(0)->getField(STORAGE_VALUE));
        }
    }
    return u256(0);
}

void StorageState::setStorage(Address const& _address, u256 const& _location, u256 const& _value)
{
    auto table = getTable(_address);
    if (table)
    {
        auto option = std::make_shared<AccessOptions>(Address(), false);
        auto entries = table->select(_location.str(), table->newCondition());
        if (entries->size() == 0u)
        {
            auto entry = table->newEntry();
            entry->setField(STORAGE_KEY, _location.str());
            entry->setField(STORAGE_VALUE, _value.str());
            table->insert(_location.str(), entry, option);
        }
        else
        {
            auto entry = table->newEntry();
            entry->setField(STORAGE_KEY, _location.str());
            entry->setField(STORAGE_VALUE, _value.str());
            table->update(_location.str(), entry, table->newCondition(), option);
        }
    }
}

void StorageState::clearStorage(Address const&) {}

void StorageState::setCode(Address const& _address, bytes&& _code)
{
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->newEntry();
        auto option = std::make_shared<AccessOptions>(Address(), false);
        if (m_enableBinary)
        {  // >= v2.4.0 and rocksdb
            entry->setField(STORAGE_VALUE, _code.data(), _code.size());
        }
        else
        {
            entry->setField(STORAGE_VALUE, toHex(_code));
        }
        table->update(ACCOUNT_CODE, entry, table->newCondition(), option);
        entry = table->newEntry();
        entry->setField(STORAGE_VALUE, toHex(sha3(_code)));
        table->update(ACCOUNT_CODE_HASH, entry, table->newCondition(), option);
    }
}

void StorageState::kill(Address _address)
{
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->newEntry();
        auto option = std::make_shared<AccessOptions>(Address(), false);
        entry->setField(STORAGE_VALUE, m_accountStartNonce.str());
        table->update(ACCOUNT_NONCE, entry, table->newCondition(), option);
        entry = table->newEntry();
        entry->setField(STORAGE_VALUE, u256(0).str());
        table->update(ACCOUNT_BALANCE, entry, table->newCondition(), option);
        entry = table->newEntry();
        entry->setField(STORAGE_VALUE, "");
        table->update(ACCOUNT_CODE, entry, table->newCondition(), option);
        entry = table->newEntry();
        entry->setField(STORAGE_VALUE, toHex(EmptySHA3));
        table->update(ACCOUNT_CODE_HASH, entry, table->newCondition(), option);
        entry = table->newEntry();
        entry->setField(STORAGE_VALUE, "false");
        table->update(ACCOUNT_ALIVE, entry, table->newCondition(), option);
    }
    clear();
}

bytes const StorageState::code(Address const& _address) const
{
    if (codeHash(_address) == EmptySHA3)
        return NullBytes;
    auto table = getTable(_address);
    if (table)
    {
        auto entries = table->select(ACCOUNT_CODE, table->newCondition());
        if (entries->size() != 0u)
        {
            if (m_enableBinary)
            {  // >= v2.4.0 and rocksdb
                return entries->get(0)->getFieldBytes(STORAGE_VALUE);
            }
            else
            {
                return fromHex(entries->get(0)->getField(STORAGE_VALUE));
            }
        }
    }
    return NullBytes;
}

h256 StorageState::codeHash(Address const& _address) const
{
    auto table = getTable(_address);
    if (table)
    {
        auto entries = table->select(ACCOUNT_CODE_HASH, table->newCondition());
        if (entries->size() != 0u)
        {
            return h256(fromHex(entries->get(0)->getField(STORAGE_VALUE)));
        }
    }
    return EmptySHA3;
}

bool StorageState::frozen(Address const& _contract) const
{
    auto table = getTable(_contract);
    if (table)
    {
        auto entries = table->select(ACCOUNT_FROZEN, table->newCondition());
        if (entries->size() != 0u)
        {
            return (entries->get(0)->getField(STORAGE_VALUE) == "true");
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
}

size_t StorageState::codeSize(Address const& _address) const
{
    return code(_address).size();
}

void StorageState::createContract(Address const& _address)
{
    createAccount(_address, requireAccountStartNonce());
}

void StorageState::incNonce(Address const& _address)
{
    auto table = getTable(_address);
    if (table)
    {
        auto entries = table->select(ACCOUNT_NONCE, table->newCondition());
        if (entries->size() != 0u)
        {
            auto entry = entries->get(0);
            auto nonce = u256(entry->getField(STORAGE_VALUE));
            ++nonce;
            Entry::Ptr updateEntry = table->newEntry();
            auto option = std::make_shared<AccessOptions>(Address(), false);
            updateEntry->setField(STORAGE_VALUE, nonce.str());
            table->update(ACCOUNT_NONCE, updateEntry, table->newCondition(), option);
        }
    }
    else
        createAccount(_address, requireAccountStartNonce() + 1);
}

void StorageState::setNonce(Address const& _address, u256 const& _newNonce)
{
    auto table = getTable(_address);
    if (table)
    {
        auto entry = table->newEntry();
        auto option = std::make_shared<AccessOptions>(Address(), false);
        entry->setField(STORAGE_VALUE, _newNonce.str());
        table->update(ACCOUNT_NONCE, entry, table->newCondition(), option);
    }
    else
        createAccount(_address, _newNonce);
}

u256 StorageState::getNonce(Address const& _address) const
{
    auto table = getTable(_address);
    if (table)
    {
        auto entries = table->select(ACCOUNT_NONCE, table->newCondition());
        if (entries->size() != 0u)
        {
            auto entry = entries->get(0);
            return u256(entry->getField(STORAGE_VALUE));
        }
    }
    return m_accountStartNonce;
}


h256 StorageState::rootHash(bool needCalculate) const
{
    if (needCalculate)
    {
        return m_memoryTableFactory->hash();
    }
    return h256();
}

void StorageState::commit()
{
    m_memoryTableFactory->commit();
}

void StorageState::dbCommit(h256 const&, int64_t)
{
    // ExecutiveContext will commit
    // m_memoryTableFactory->commitDB(_blockHash, _blockNumber);
}

void StorageState::setRoot(h256 const&) {}

u256 const& StorageState::accountStartNonce() const
{
    return m_accountStartNonce;
}

u256 const& StorageState::requireAccountStartNonce() const
{
    if (m_accountStartNonce == Invalid256)
        BOOST_THROW_EXCEPTION(InvalidAccountStartNonceInState());
    return m_accountStartNonce;
}

void StorageState::noteAccountStartNonce(u256 const& _actual)
{
    if (m_accountStartNonce == Invalid256)
        m_accountStartNonce = _actual;
    else if (m_accountStartNonce != _actual)
        BOOST_THROW_EXCEPTION(IncorrectAccountStartNonceInState());
}

size_t StorageState::savepoint() const
{
    return m_memoryTableFactory->savepoint();
}

void StorageState::rollback(size_t _savepoint)
{
    m_memoryTableFactory->rollback(_savepoint);
}

void StorageState::clear()
{
    // m_cache.clear();
}

bool StorageState::checkAuthority(Address const& _origin, Address const& _contract) const
{
    auto table = getTable(_contract);
    if (table)
        return table->checkAuthority(_origin);
    else
        return true;
}

void StorageState::createAccount(Address const& _address, u256 const& _nonce, u256 const& _amount)
{
    std::string tableName("_contract_data_" + _address.hex() + "_");
    if (g_BCOSConfig.version() >= V2_2_0)
    {
        tableName = std::string("c_" + _address.hex());
    }
    auto table = m_memoryTableFactory->createTable(tableName, STORAGE_KEY, STORAGE_VALUE, false);
    if (!table)
    {
        LOG(ERROR) << LOG_BADGE("StorageState") << LOG_DESC("createAccount failed")
                   << LOG_KV("Account", tableName);
        return;
    }
    auto entry = table->newEntry();
    entry->setField(STORAGE_KEY, ACCOUNT_BALANCE);
    entry->setField(STORAGE_VALUE, _amount.str());
    table->insert(ACCOUNT_BALANCE, entry);
    entry = table->newEntry();
    entry->setField(STORAGE_KEY, ACCOUNT_CODE_HASH);
    entry->setField(STORAGE_VALUE, toHex(EmptySHA3));
    table->insert(ACCOUNT_CODE_HASH, entry);
    entry = table->newEntry();
    entry->setField(STORAGE_KEY, ACCOUNT_CODE);
    entry->setField(STORAGE_VALUE, "");
    table->insert(ACCOUNT_CODE, entry);
    entry = table->newEntry();
    entry->setField(STORAGE_KEY, ACCOUNT_NONCE);
    entry->setField(STORAGE_VALUE, _nonce.str());
    table->insert(ACCOUNT_NONCE, entry);
    entry = table->newEntry();
    entry->setField(STORAGE_KEY, ACCOUNT_ALIVE);
    entry->setField(STORAGE_VALUE, "true");
    table->insert(ACCOUNT_ALIVE, entry);
}

inline storage::Table::Ptr StorageState::getTable(Address const& _address) const
{
    std::string tableName("_contract_data_" + _address.hex() + "_");
    if (g_BCOSConfig.version() >= V2_2_0)
    {
        tableName = std::string("c_" + _address.hex());
    }
    return m_memoryTableFactory->openTable(tableName);
}
