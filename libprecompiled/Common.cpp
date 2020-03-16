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
/** @file Common.h
 *  @author ancelmo
 *  @date 20180921
 */
#include "Common.h"
#include "libstorage/StorageException.h"
#include <libconfig/GlobalConfigure.h>
#include <libethcore/ABI.h>
#include <libethcore/Exceptions.h>

using namespace std;
using namespace dev;
using namespace dev::storage;


static const string USER_TABLE_PREFIX = "_user_";
static const string USER_TABLE_PREFIX_SHORT = "u_";
static const string CONTRACT_TABLE_PREFIX_SHORT = "c_";

void dev::precompiled::getErrorCodeOut(bytes& out, int const& result)
{
    dev::eth::ContractABI abi;
    if (result > 0 && result < 128)
    {
        out = abi.abiIn("", u256(result));
        return;
    }
    out = abi.abiIn("", s256(result));
    if (g_BCOSConfig.version() < RC2_VERSION)
    {
        out = abi.abiIn("", -result);
    }
    else if (g_BCOSConfig.version() == RC2_VERSION)
    {
        out = abi.abiIn("", u256(-result));
    }
}

void dev::precompiled::logError(
    const std::string& precompiled_name, const std::string& op, const std::string& msg)
{
    PRECOMPILED_LOG(ERROR) << LOG_BADGE(precompiled_name) << LOG_DESC(op) << ": " << LOG_DESC(msg);
}

void dev::precompiled::logError(const std::string& precompiled_name, const std::string& op,
    const std::string& _key, const std::string& _value)
{
    PRECOMPILED_LOG(ERROR) << LOG_BADGE(precompiled_name) << LOG_DESC(op) << LOG_KV(_key, _value);
}

void dev::precompiled::logError(const std::string& precompiled_name, const std::string& op,
    const std::string& key1, const std::string& value1, const std::string& key2,
    const std::string& value2)
{
    PRECOMPILED_LOG(ERROR) << LOG_BADGE(precompiled_name) << LOG_DESC(op) << LOG_KV(key1, value1)
                           << LOG_KV(key2, value2);
}

void dev::precompiled::throwException(const std::string& msg)
{
    BOOST_THROW_EXCEPTION(dev::eth::TransactionRefused() << dev::errinfo_comment(msg));
}

char* dev::precompiled::stringToChar(std::string& params)
{
    return const_cast<char*>(params.data());
}

std::string dev::precompiled::getTableName(const std::string& _tableName)
{
    if (g_BCOSConfig.version() < V2_2_0)
    {
        return USER_TABLE_PREFIX + _tableName;
    }
    return USER_TABLE_PREFIX_SHORT + _tableName;
}

std::string dev::precompiled::getContractTableName(Address const& _contractAddress)
{
    return std::string(CONTRACT_TABLE_PREFIX_SHORT + _contractAddress.hex());
}


void dev::precompiled::checkNameValidate(const string& tableName, string& keyField,
    vector<string>& valueFieldList, bool throwStorageException)
{
    if (g_BCOSConfig.version() >= V2_2_0)
    {
        set<string> valueFieldSet;
        boost::trim(keyField);
        valueFieldSet.insert(keyField);

        vector<char> allowChar = {'$', '_', '@'};

        auto checkTableNameValidate = [allowChar, throwStorageException](const string& tableName) {
            size_t iSize = tableName.size();
            for (size_t i = 0; i < iSize; i++)
            {
                if (!isalnum(tableName[i]) &&
                    (allowChar.end() == find(allowChar.begin(), allowChar.end(), tableName[i])))
                {
                    STORAGE_LOG(ERROR)
                        << LOG_DESC("invalid tablename") << LOG_KV("table name", tableName);
                    if (throwStorageException)
                    {
                        BOOST_THROW_EXCEPTION(StorageException(
                            CODE_TABLE_INVALIDATE_FIELD, string("invalid tablename:") + tableName));
                    }
                    else
                    {
                        BOOST_THROW_EXCEPTION(
                            PrecompiledException(string("invalid tablename:") + tableName));
                    }
                }
            }
        };
        auto checkFieldNameValidate = [allowChar, throwStorageException](
                                          const string& tableName, const string& fieldName) {
            if (fieldName.size() == 0 || fieldName[0] == '_')
            {
                STORAGE_LOG(ERROR) << LOG_DESC("error key") << LOG_KV("field name", fieldName)
                                   << LOG_KV("table name", tableName);
                if (throwStorageException)
                {
                    BOOST_THROW_EXCEPTION(StorageException(
                        CODE_TABLE_INVALIDATE_FIELD, string("invalid field:") + fieldName));
                }
                else
                {
                    BOOST_THROW_EXCEPTION(
                        PrecompiledException(string("invalid field:") + fieldName));
                }
            }
            size_t iSize = fieldName.size();
            for (size_t i = 0; i < iSize; i++)
            {
                if (!isalnum(fieldName[i]) &&
                    (allowChar.end() == find(allowChar.begin(), allowChar.end(), fieldName[i])))
                {
                    STORAGE_LOG(ERROR)
                        << LOG_DESC("invalid fieldname") << LOG_KV("field name", fieldName)
                        << LOG_KV("table name", tableName);
                    if (throwStorageException)
                    {
                        BOOST_THROW_EXCEPTION(StorageException(
                            CODE_TABLE_INVALIDATE_FIELD, string("invalid field:") + fieldName));
                    }
                    else
                    {
                        BOOST_THROW_EXCEPTION(
                            PrecompiledException(string("invalid field:") + fieldName));
                    }
                }
            }
        };

        checkTableNameValidate(tableName);
        checkFieldNameValidate(tableName, keyField);

        for (auto& valueField : valueFieldList)
        {
            auto ret = valueFieldSet.insert(valueField);
            if (!ret.second)
            {
                STORAGE_LOG(ERROR)
                    << LOG_DESC("dumplicate field") << LOG_KV("field name", valueField)
                    << LOG_KV("table name", tableName);
                if (throwStorageException)
                {
                    BOOST_THROW_EXCEPTION(StorageException(
                        CODE_TABLE_DUMPLICATE_FIELD, string("dumplicate field:") + valueField));
                }
                else
                {
                    BOOST_THROW_EXCEPTION(
                        PrecompiledException(string("dumplicate field:") + valueField));
                }
            }
            checkFieldNameValidate(tableName, valueField);
        }
    }
}

int dev::precompiled::checkLengthValidate(
    const string& fieldValue, int32_t maxLength, int32_t errorCode, bool throwStorageException)
{
    if (fieldValue.size() > (size_t)maxLength)
    {
        PRECOMPILED_LOG(ERROR) << "key:" << fieldValue << " value size:" << fieldValue.size()
                               << " greater than " << maxLength;
        if (throwStorageException)
        {
            BOOST_THROW_EXCEPTION(StorageException(
                errorCode, string("size of value/key greater than") + to_string(maxLength)));
        }
        else
        {
            BOOST_THROW_EXCEPTION(PrecompiledException(
                string("size of value/key greater than") + to_string(maxLength)));
        }

        return errorCode;
    }
    return 0;
}

bytes precompiled::PrecompiledException::ToOutput()
{
    eth::ContractABI abi;
    return abi.abiIn("Error(string)", string(what()));
}
