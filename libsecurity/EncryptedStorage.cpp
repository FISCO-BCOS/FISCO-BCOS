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
#include <libdevcrypto/AES.h>
#include <libstorage/Common.h>

using namespace dev;
using namespace dev::storage;
using namespace std;

std::string encryptValue(const bytes& dataKey, const string value)
{
    bytesConstRef valueRef{(const unsigned char*)value.c_str(), value.length()};
    bytes enData = aesCBCEncrypt(valueRef, ref(dataKey));
    return asString(enData);
}

std::string decryptValue(const bytes& dataKey, const std::string& value)
{
    bytes deData = aesCBCDecrypt(
        bytesConstRef{(const unsigned char*)value.c_str(), value.length()}, ref(dataKey));
    return asString(deData);
}

Entries::Ptr EncryptedStorage::select(
    h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    // Ignore system field
    if (isHashField(key))
    {
        return m_backend->select(hash, num, tableInfo, key, condition);
    }

    /*
        Notice:
        If config.ini [storage security] enable=true, the selection condition is not avaliable!
        The condition selection is only depended on upper class(MemoryTable2's processEntry()).
        Thus, force onlyDiry() to return false;
    */

    // Overwrite condition to empty
    condition = std::make_shared<Condition>();
    Entries::Ptr encEntries = m_backend->select(hash, num, tableInfo, key, condition);
    return decryptEntries(encEntries);
}

size_t EncryptedStorage::commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas)
{
    for (auto data : datas)
    {
        data->dirtyEntries = encryptEntries(data->dirtyEntries);
        data->newEntries = encryptEntries(data->newEntries);
    }
    return m_backend->commit(hash, num, datas);
}

void EncryptedStorage::setBackend(Storage::Ptr backend)
{
    // Check backend storage
    m_backend = backend;
}

Entries::Ptr EncryptedStorage::encryptEntries(Entries::Ptr inEntries)
{
    checkDataKey();

    for (size_t i = 0; i < inEntries->size(); i++)  // XX need parallel
    {
        Entry::Ptr inEntry = inEntries->get(i);
        for (auto const& inKV : *inEntry->fields())
        {
            if (!isHashField(inKV.first))
            {
                string v = encryptValue(m_dataKey, inKV.second);
                inEntry->setField(inKV.first, v);
            }
        }
    }
    return inEntries;
}

Entries::Ptr EncryptedStorage::decryptEntries(Entries::Ptr inEntries)
{
    checkDataKey();

    for (size_t i = 0; i < inEntries->size(); i++)  // XX need parallel
    {
        Entry::Ptr inEntry = inEntries->get(i);
        for (auto const& inKV : *inEntry->fields())
        {
            if (!isHashField(inKV.first))
            {
                string v = decryptValue(m_dataKey, inKV.second);
                inEntry->setField(inKV.first, v);
            }
        }
    }
    return inEntries;
}

void EncryptedStorage::checkDataKey()
{
    if (m_dataKey == bytes())
    {
        m_dataKey = g_keyCenter->getDataKey(g_BCOSConfig.diskEncryption.cipherDataKey);
    }
}