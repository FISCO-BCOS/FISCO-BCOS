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
#include <libdevcore/easylog.h>

using namespace std;
using namespace dev;
using namespace dev::db;

namespace dev
{
namespace db
{
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

std::string encryptValue(const std::string& _dataKey, leveldb::Slice _value)
{
    string ivData = _dataKey.substr(0, 16);
    bytesConstRef valueRef = bytesConstRef((const unsigned char*)_value.data(), _value.size());
    bytes enData = aesCBCEncrypt(valueRef, _dataKey, _dataKey.length(),
        bytesConstRef{(const unsigned char*)ivData.c_str(), ivData.length()});
    return asString(enData);
}

std::string decryptValue(const std::string& _dataKey, const std::string& _value)
{
    string ivData = _dataKey.substr(0, 16);
    bytes deData = aesCBCDecrypt(
        bytesConstRef{(const unsigned char*)_value.c_str(), _value.length()}, _dataKey,
        _dataKey.length(), bytesConstRef{(const unsigned char*)ivData.c_str(), ivData.length()});
    return asString(deData);
}

void EncryptedLevelDBWriteBatch::insertSlice(leveldb::Slice _key, leveldb::Slice _value)
{
    string enData;
    try
    {
        enData = encryptValue(m_dataKey, _value);
        // ENCDBLOG(TRACE) << "[ENC] Write batch insert [k/encv/v]: " << ascii2hex(_key.data(),
        // _key.size()) << "/"
        //                << ascii2hex(enData) << "/" << ascii2hex(_value.data(), _value.size()) <<
        //                endl;
    }
    catch (Exception& e)
    {
        ENCDBLOG(ERROR) << "[insertSlice] Encrypt ERROR! [k/v]: " << _key.ToString() << "/ "
                        << _value.ToString() << endl;
        BOOST_THROW_EXCEPTION(EncryptedLevelDBEncryptFailed()
                              << errinfo_comment("EncryptedLevelDB batch encrypt error"));
    }
    m_writeBatch.Put(_key, leveldb::Slice(enData));
}

EncryptedLevelDB::EncryptedLevelDB(
    const leveldb::Options& _options, const std::string& _name, const std::string& _cypherDataKey)
  : BasicLevelDB(), m_cypherDataKey(_cypherDataKey)
{
    // Encrypted leveldb initralization

    // Open db
    auto db = static_cast<leveldb::DB*>(nullptr);
    m_openStatus = leveldb::DB::Open(_options, _name, &db);

    if (!m_openStatus.ok())
        LOG(ERROR) << "Database open error" << endl;
    assert(db);
    m_db.reset(db);

    if (!m_openStatus.ok())
        return;

    // Determin open type
    OpenDBStatus type = checkOpenDBStatus();
    switch (type)
    {
    case OpenDBStatus::FirstCreation:
        if (m_cypherDataKey.empty())
            m_cypherDataKey = g_keyCenter.generateCypherDataKey();
        setCypherDataKey(m_cypherDataKey);
        ENCDBLOG(DEBUG) << "[Open] First creation encrypted DB" << endl;
        // No need break here, break with Encrypted type
    case OpenDBStatus::Encrypted:
        ENCDBLOG(DEBUG)
            << "[open] Encrypted leveldb open success [name/db/keyCenterUrl/cypherDataKey]: "
            << _name << "/" << m_db << "/" << g_keyCenter.url() << "/" << m_cypherDataKey << endl;
        break;

    case OpenDBStatus::NoEncrypted:
        ENCDBLOG(FATAL) << "[Open] Database type ERROR! This DB is not EncryptedLevelDB" << endl;
        break;

    case OpenDBStatus::CypherKeyError:
        ENCDBLOG(FATAL)
            << "[Open] Configure CypherDataKey ERROR! Configure CypherDataKey is not the "
               "key of the database [database/configure]: "
            << getKeyOfDatabase() << "/" << _cypherDataKey << endl;  // log and exit
        break;

    default:
        ENCDBLOG(FATAL) << "[Open] Unknown Open encrypted DB TYPE" << endl;
    }

    // Decrypt cypher key from keycenter
    m_dataKey = g_keyCenter.getDataKey(m_cypherDataKey);
    if (m_dataKey == "")
    {
        m_openStatus = leveldb::Status::IOError(leveldb::Slice("Get dataKey failed"));
        return;
    }
}

leveldb::Status EncryptedLevelDB::Open(const leveldb::Options& _options, const std::string& _name,
    BasicLevelDB** _dbptr, const std::string& _cypherDataKey)
{
    *_dbptr = new EncryptedLevelDB(_options, _name, _cypherDataKey);
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
            // ENCDBLOG(TRACE) << "[DEC] Get [k/encv/v]: " << ascii2hex(_key.data(), _key.size()) <<
            // "/"
            // << ascii2hex(encValue)
            //                << "/" << *_value << endl;
        }
        catch (Exception& e)
        {
            ENCDBLOG(ERROR) << "[Get] Decrypt ERROR! [k/encv]: " << _key.ToString() << "/ "
                            << encValue << endl;
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
        // ENCDBLOG(TRACE) << "[ENC] Put [k/encv/v]: " << ascii2hex(_key.data(), _key.size()) << "/"
        // << ascii2hex(enData)
        //                << "/" << ascii2hex(_value.data(), _value.size()) << endl;
    }
    catch (Exception& e)
    {
        ENCDBLOG(ERROR) << "[Put] Encrypt ERROR! [k/v]: " << _key.ToString() << "/ "
                        << _value.ToString() << endl;
        BOOST_THROW_EXCEPTION(
            EncryptedLevelDBEncryptFailed() << errinfo_comment("EncryptedLevelDB encrypt error"));
    }
    return m_db->Put(_options, _key, leveldb::Slice(enData));
}

std::unique_ptr<LevelDBWriteBatch> EncryptedLevelDB::createWriteBatch() const
{
    return std::unique_ptr<LevelDBWriteBatch>(new EncryptedLevelDBWriteBatch(m_dataKey));
}

string EncryptedLevelDB::getKeyOfDatabase()
{
    if (!m_db)
    {
        ENCDBLOG(ERROR) << "[key] getKeyOfDatabase() db not opened" << endl;
        return string("");
    }

    std::string key;
    leveldb::Status status =
        m_db->Get(leveldb::ReadOptions(), leveldb::Slice(c_cypherDataKeyName), &key);
    if (!status.ok())
        return string("");
    return key;
}

void EncryptedLevelDB::setCypherDataKey(string _cypherDataKey)
{
    if (!m_db)
    {
        ENCDBLOG(ERROR) << "[key] getKeyOfDatabase() db not opened" << endl;
        return;
    }
    string oldKey = getKeyOfDatabase();
    if (!oldKey.empty() && oldKey != _cypherDataKey)
        ENCDBLOG(FATAL)
            << "[key] Configure CypherDataKey ERROR! Configure CypherDataKey is not the "
               "key of the database [database/configure]: "
            << oldKey << "/" << _cypherDataKey << endl;  // log and exit

    auto status = m_db->Put(leveldb::WriteOptions(), leveldb::Slice(c_cypherDataKeyName),
        leveldb::Slice(_cypherDataKey));

    if (!status.ok())
        ENCDBLOG(FATAL) << "[Key] Configure CypherDataKey ERROR! Write key failed."
                        << endl;  // log and exit
    else
        ENCDBLOG(DEBUG) << "[key] Configure CypherDataKey: " << _cypherDataKey << endl;
}

EncryptedLevelDB::OpenDBStatus EncryptedLevelDB::checkOpenDBStatus()
{
    if (empty())
        return OpenDBStatus::FirstCreation;

    string keyOfDatabase = getKeyOfDatabase();
    if (keyOfDatabase.empty())
        return OpenDBStatus::NoEncrypted;

    if (keyOfDatabase != m_cypherDataKey)
        return OpenDBStatus::CypherKeyError;

    return OpenDBStatus::Encrypted;
}

}  // namespace db
}  // namespace dev