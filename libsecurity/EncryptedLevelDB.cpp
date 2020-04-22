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
 * @brief : Encrypt level DB
 * @author: jimmyshi, websterchen
 * @date: 2018-11-26
 */

#include "EncryptedLevelDB.h"
#include "libconfig/GlobalConfigure.h"
#include "libdevcrypto/CryptoInterface.h"

using namespace std;
using namespace dev;
using namespace dev::db;

namespace dev
{
namespace db
{
#if 0
char* ascii2hex(const char* _chs, int _len)
{
    char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

    char* ascii = (char*)calloc(_len * 3 + 1, sizeof(char));  // calloc ascii

    int i = 0;
    while (i < _len)
    {
        int b = _chs[i] & 0x000000ff;
        ascii[i * 2] = hex[b / 16];
        ascii[i * 2 + 1] = hex[b % 16];
        ++i;
    }
    return ascii;
}

char* ascii2hex(const string& _str)
{
    const char* chs = _str.c_str();
    int len = _str.length();
    return ascii2hex(chs, len);
}
#endif

std::string encryptValue(const bytes& _dataKey, leveldb::Slice _value)
{
    return crypto::SymmetricEncrypt((const unsigned char*)_value.data(), _value.size(),
        _dataKey.data(), _dataKey.size(), _dataKey.data());
}

std::string decryptValue(const bytes& _dataKey, const std::string& _value)
{
    return crypto::SymmetricDecrypt((const unsigned char*)_value.c_str(), _value.size(),
        _dataKey.data(), _dataKey.size(), _dataKey.data());
}

}  // namespace db
}  // namespace dev

void EncryptedLevelDBWriteBatch::insertSlice(
    const leveldb::Slice& _key, const leveldb::Slice& _value)
{
    string enData;
    try
    {
        enData = encryptValue(m_dataKey, _value);
        // ENCDB_LOG(TRACE)<< LOG_BADGE("ENC")<< LOG_DESC("Write batch insert")<<
        // LOG_KV("k",ascii2hex(_key.data(), _key.size()))<< LOG_KV("encv",ascii2hex(enData))<<
        // LOG_KV("v", ascii2hex(_value.data(), _value.size()));
    }
    catch (Exception& e)
    {
        ENCDB_LOG(ERROR) << LOG_BADGE("insertSlice") << LOG_DESC("Encrypt ERROR!")
                         << LOG_KV(_key.ToString(), _value.ToString());


        BOOST_THROW_EXCEPTION(EncryptedLevelDBEncryptFailed()
                              << errinfo_comment("EncryptedLevelDB batch encrypt error"));
    }
    dev::WriteGuard l(x_writeBatch);
    m_writeBatch.Put(_key, leveldb::Slice(enData));
}

EncryptedLevelDB::EncryptedLevelDB(const leveldb::Options& _options, const std::string& _name,
    const std::string& _cipherDataKey, const std::string& _dataKey)
  : BasicLevelDB(), m_cipherDataKey(_cipherDataKey), m_dataKey(asBytes(_dataKey))
{
    m_name = _name;
    // Open db
    auto db = static_cast<leveldb::DB*>(nullptr);
    m_openStatus = leveldb::DB::Open(_options, _name, &db);

    if (!m_openStatus.ok())
    {
        LOG(ERROR) << "Database open error";
        BOOST_THROW_EXCEPTION(std::runtime_error("open LevelDB failed"));
    }
    m_db.reset(db);

    // Determin open type
    OpenDBStatus type = checkOpenDBStatus();
    switch (type)
    {
    case OpenDBStatus::FirstCreation: {
        if (m_cipherDataKey.empty())
        {
            std::stringstream exitInfo;
            exitInfo
                << LOG_BADGE("Open")
                << LOG_DESC(
                       " Database key ERROR! Please set cipherDataKey when enable disk encryption!")
                << LOG_KV("name", _name);
            errorExit(exitInfo, InvalidDiskEncryptionSetting());
        }
        setCipherDataKey(m_cipherDataKey);
        ENCDB_LOG(DEBUG) << LOG_BADGE("open") << LOG_DESC("First creation encrypted DB")
                         << LOG_KV("name", _name) << LOG_KV("db", m_db)
                         << LOG_KV("cipherDataKey", m_cipherDataKey);
        break;
    }
    case OpenDBStatus::Encrypted: {
        ENCDB_LOG(DEBUG) << LOG_BADGE("open") << LOG_DESC(" Encrypted leveldb open success")
                         << LOG_KV("name", _name) << LOG_KV("db", m_db)
                         << LOG_KV("cipherDataKey", m_cipherDataKey);
        break;
    }

    case OpenDBStatus::NoEncrypted: {
        std::stringstream exitInfo;
        exitInfo << LOG_BADGE("Open")
                 << LOG_DESC("Database type ERROR! This DB is not EncryptedLevelDB")
                 << LOG_KV("db", _name) << endl;
        errorExit(exitInfo, InvalidDiskEncryptionSetting());
        break;
    }

    case OpenDBStatus::CipherKeyError: {
        std::stringstream exitInfo;
        exitInfo
            << LOG_BADGE("Open")
            << LOG_DESC(
                   "Configure CipherDataKey ERROR! Configure CipherDataKey is not the key of the "
                   "database")
            << LOG_KV("name", _name) << LOG_KV("database", getKeyOfDatabase())
            << LOG_KV("configure", _cipherDataKey) << endl;
        errorExit(exitInfo, InvalidDiskEncryptionSetting());
        break;
    }

    default: {
        std::stringstream exitInfo;
        exitInfo << LOG_BADGE("Open") << LOG_DESC("Unknown Open encrypted DB TYPE")
                 << LOG_KV("name", _name) << endl;
        errorExit(exitInfo, InvalidDiskEncryptionSetting());
    }
    }

    if (m_dataKey.empty())
    {
        m_openStatus = leveldb::Status::IOError(leveldb::Slice("Get dataKey failed"));
        return;
    }
}

leveldb::Status EncryptedLevelDB::Open(const leveldb::Options& _options, const std::string& _name,
    BasicLevelDB** _dbptr, const std::string& _cipherDataKey, const std::string& _dataKey)
{
    *_dbptr = new EncryptedLevelDB(_options, _name, _cipherDataKey, _dataKey);
    leveldb::Status status = (*_dbptr)->OpenStatus();

    if (!status.ok())
    {
        delete *_dbptr;
        *_dbptr = NULL;
    }

    return status;
}

leveldb::Status EncryptedLevelDB::Get(
    const leveldb::ReadOptions& _options, const leveldb::Slice& _key, std::string* _value)
{
    if (!m_db)
        return leveldb::Status::IOError(leveldb::Slice("DB not open"));

    leveldb::Status status;
    std::string encValue;
    status = m_db->Get(_options, _key, &encValue);
    if (!encValue.empty())
    {
        try
        {
            *_value = decryptValue(m_dataKey, encValue);

            // ENCDB_LOG(TRACE)<< LOG_BADGE("DEC")<< LOG_DESC("Get")<< LOG_KV("k",
            // ascii2hex(_key.data(), _key.size()))<< LOG_KV("encv",ascii2hex(encValue))<<
            // LOG_KV("v", *_value);
        }
        catch (Exception& e)
        {
            ENCDB_LOG(ERROR) << LOG_BADGE("Get") << LOG_DESC("Decrypt ERROR!")
                             << LOG_KV(_key.ToString(), encValue);
            BOOST_THROW_EXCEPTION(EncryptedLevelDBDecryptFailed()
                                  << errinfo_comment("EncryptedLevelDB decrypt error"));
        }
    }
    return status;
}
leveldb::Status EncryptedLevelDB::Put(
    const leveldb::WriteOptions& _options, const leveldb::Slice& _key, const leveldb::Slice& _value)
{
    if (!m_db)
        return leveldb::Status::IOError(leveldb::Slice("DB not open"));

    string enData;
    try
    {
        enData = encryptValue(m_dataKey, _value);
    }
    catch (Exception& e)
    {
        ENCDB_LOG(ERROR) << LOG_BADGE("Put") << LOG_DESC(" Encrypt ERROR!")
                         << LOG_KV(_key.ToString(), _value.ToString());
        BOOST_THROW_EXCEPTION(
            EncryptedLevelDBEncryptFailed() << errinfo_comment("EncryptedLevelDB encrypt error"));
    }
    return m_db->Put(_options, _key, leveldb::Slice(enData));
}

std::unique_ptr<LevelDBWriteBatch> EncryptedLevelDB::createWriteBatch() const
{
    return std::unique_ptr<LevelDBWriteBatch>(new EncryptedLevelDBWriteBatch(m_dataKey, m_name));
}

string EncryptedLevelDB::getKeyOfDatabase()
{
    if (!m_db)
    {
        ENCDB_LOG(ERROR) << LOG_BADGE("key") << LOG_DESC(" getKeyOfDatabase() db not opened");
        return string("");
    }

    std::string key;
    leveldb::Status status =
        m_db->Get(leveldb::ReadOptions(), leveldb::Slice(c_cipherDataKeyName), &key);
    if (!status.ok())
        return string("");
    return key;
}

void EncryptedLevelDB::setCipherDataKey(string _cipherDataKey)
{
    if (!m_db)
    {
        ENCDB_LOG(ERROR) << LOG_BADGE("key") << LOG_DESC("getKeyOfDatabase() db not opened");
        return;
    }
    string oldKey = getKeyOfDatabase();
    if (!oldKey.empty() && oldKey != _cipherDataKey)
    {
        std::stringstream exitInfo;
        exitInfo
            << LOG_BADGE("key")
            << LOG_DESC(
                   "Configure CipherDataKey ERROR! Configure CipherDataKey is not the key of the "
                   "database")
            << LOG_KV("name", m_name) << LOG_KV("oldKey", oldKey)
            << LOG_KV("keyToSet", _cipherDataKey) << endl;
        errorExit(exitInfo, InvalidDiskEncryptionSetting());
    }

    auto status = m_db->Put(leveldb::WriteOptions(), leveldb::Slice(c_cipherDataKeyName),
        leveldb::Slice(_cipherDataKey));

    if (!status.ok())
    {
        std::stringstream exitInfo;
        exitInfo << LOG_BADGE("key") << LOG_DESC("Configure CipherDataKey ERROR! Write key failed.")
                 << LOG_KV("name", m_name) << endl;
        errorExit(exitInfo, WriteDBFailed());
    }
    else
        ENCDB_LOG(DEBUG) << LOG_BADGE("key") << LOG_DESC("Configure CipherDataKey")
                         << LOG_KV("key", _cipherDataKey);
}

EncryptedLevelDB::OpenDBStatus EncryptedLevelDB::checkOpenDBStatus()
{
    if (empty())
        return OpenDBStatus::FirstCreation;

    string keyOfDatabase = getKeyOfDatabase();
    if (keyOfDatabase.empty())
        return OpenDBStatus::NoEncrypted;

    if (keyOfDatabase != m_cipherDataKey)
        return OpenDBStatus::CipherKeyError;

    return OpenDBStatus::Encrypted;
}
