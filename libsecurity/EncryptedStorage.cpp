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
 * @brief : Encrypt Storage
 * @author: jimmyshi
 * @date: 2019-05-22
 */

#include "EncryptedStorage.h"
#include "Common.h"
#include "KeyCenter.h"
#include <libdevcore/easylog.h>
#include <libdevcrypto/AES.h>
#include <libstorage/Common.h>

using namespace dev;
using namespace dev::storage;
using namespace std;

std::string encryptValue(const bytes& dataKey, const string& value, const string& k)
{
    // FIXME: use std::string instead of bytesRef
    try
    {
        bytesConstRef valueRef{(const unsigned char*)value.c_str(), value.length()};
        bytes enData = aesCBCEncrypt(valueRef, ref(dataKey));
        return asString(enData);
    }
    catch (...)
    {
        LOG(ERROR) << LOG_BADGE("EncStorage")
                   << LOG_DESC("encryptValue error, just return without encrypt")
                   << LOG_KV(k, value);
    }
    return value;
}

std::string decryptValue(const bytes& dataKey, const std::string& value, const string& k)
{
    try
    {
        bytes deData = aesCBCDecrypt(
            bytesConstRef{(const unsigned char*)value.c_str(), value.length()}, ref(dataKey));
        return asString(deData);
    }
    catch (...)
    {
        LOG(ERROR) << LOG_BADGE("EncStorage")
                   << LOG_DESC("decryptValue error, just return without decrypt")
                   << LOG_KV(k, value);
    }
    return value;
}

Entries::Ptr EncryptedStorage::select(h256 hash, int64_t num, TableInfo::Ptr tableInfo,
    const std::string& key, Condition::Ptr condition)
{
    (void)condition;
    /*
        Notice:
        If config.ini [storage security] enable=true, the selection condition is not available!
        The condition selection is only depended on upper class(MemoryTable2's processEntry()).
        Thus, force onlyDiry() to return false and overwrite condition to empty
    */
    string encKey = encryptValue(m_dataKey, key, "entries key");
    Entries::Ptr encEntries =
        m_backend->select(hash, num, tableInfo, encKey, std::make_shared<Condition>());
    return decryptEntries(encEntries);
}

size_t EncryptedStorage::commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas)
{
    set<string> hasEncryptedTable;
    for (auto data : datas)
    {
        string tableName = data->info->name;
        auto it = hasEncryptedTable.find(tableName);
        if (it != hasEncryptedTable.end())
        {
            LOG(WARNING) << LOG_BADGE("EncStorage") << LOG_DESC("Commit table twice")
                         << LOG_KV("tableName", tableName);
            continue;  // Forbid encrypting a table twice at a commit process
        }
        // FIXME: should not overwrite const params, find a more elegant way
        data->dirtyEntries = encryptEntries(data->dirtyEntries);
        data->newEntries = encryptEntries(data->newEntries);
        hasEncryptedTable.insert(tableName);
    }
    return m_backend->commit(hash, num, datas);
}

void EncryptedStorage::setBackend(Storage::Ptr backend)
{
    m_backend = backend;
}

Entries::Ptr EncryptedStorage::encryptEntries(Entries::Ptr inEntries)
{
    auto entriesSize = inEntries->size();
    // TODO: use tbb parallel for
    for (size_t i = 0; i < entriesSize; i++)
    {
        Entry::Ptr inEntry = inEntries->get(i);
        for (auto const& inKV : *inEntry)
        {
            if (isHashField(inKV.first))
            {
                string v = encryptValue(m_dataKey, inKV.second, inKV.first);
                inEntry->setField(inKV.first, v);
            }
        }
    }
    return inEntries;
}

Entries::Ptr EncryptedStorage::decryptEntries(Entries::Ptr inEntries)
{
    auto entriesSize = inEntries->size();
    for (size_t i = 0; i < entriesSize; i++)
    {
        Entry::Ptr inEntry = inEntries->get(i);
        for (auto const& inKV : *inEntry)
        {
            if (isHashField(inKV.first))
            {
                string v = decryptValue(m_dataKey, inKV.second, inKV.first);
                inEntry->setField(inKV.first, v);
            }
        }
    }
    return inEntries;
}
