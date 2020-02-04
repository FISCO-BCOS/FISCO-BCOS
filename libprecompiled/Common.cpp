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

using namespace std;
using namespace dev;
using namespace dev::storage;

static const string USER_TABLE_PREFIX = "_user_";
static const string USER_TABLE_PREFIX_SHORT = "u_";

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
    return std::string("c_" + _contractAddress.hex());
}


void dev::precompiled::checkNameValidate(
    const std::string& tableName, std::string& keyField, std::vector<std::string>& valueFieldList)
{
    if (g_BCOSConfig.version() >= V2_2_0)
    {
        set<string> valueFieldSet;
        boost::trim(keyField);
        valueFieldSet.insert(keyField);

        std::vector<char> allowChar = {'$', '_', '@'};

        auto checkTableNameValidate = [allowChar](const std::string& tableName) {
            size_t iSize = tableName.size();
            for (size_t i = 0; i < iSize; i++)
            {
                if (!isalnum(tableName[i]) &&
                    (allowChar.end() == find(allowChar.begin(), allowChar.end(), tableName[i])))
                {
                    STORAGE_LOG(ERROR)
                        << LOG_DESC("invalidate tablename") << LOG_KV("table name", tableName);
                    BOOST_THROW_EXCEPTION(StorageException(CODE_TABLE_INVALIDATE_FIELD,
                        std::string("invalidate tablename:") + tableName));
                }
            }
        };
        auto checkFieldNameValidate = [allowChar](const std::string& tableName,
                                          const std::string& fieldName) {
            if (fieldName.size() == 0 || fieldName[0] == '_')
            {
                STORAGE_LOG(ERROR) << LOG_DESC("error key") << LOG_KV("field name", fieldName)
                                   << LOG_KV("table name", tableName);
                BOOST_THROW_EXCEPTION(StorageException(
                    CODE_TABLE_INVALIDATE_FIELD, std::string("invalidate field:") + fieldName));
            }
            size_t iSize = fieldName.size();
            for (size_t i = 0; i < iSize; i++)
            {
                if (!isalnum(fieldName[i]) &&
                    (allowChar.end() == find(allowChar.begin(), allowChar.end(), fieldName[i])))
                {
                    STORAGE_LOG(ERROR)
                        << LOG_DESC("invalidate fieldname") << LOG_KV("field name", fieldName)
                        << LOG_KV("table name", tableName);
                    BOOST_THROW_EXCEPTION(StorageException(
                        CODE_TABLE_INVALIDATE_FIELD, std::string("invalidate field:") + fieldName));
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
                BOOST_THROW_EXCEPTION(StorageException(
                    CODE_TABLE_DUMPLICATE_FIELD, std::string("dumplicate field:") + valueField));
            }
            checkFieldNameValidate(tableName, valueField);
        }
    }
}

int dev::precompiled::checkLengthValidate(
    const std::string& fieldValue, int32_t maxLength, int32_t errorCode, bool throwStorageException)
{
    if (fieldValue.size() > (size_t)maxLength)
    {
        PRECOMPILED_LOG(ERROR) << "key:" << fieldValue << " value size:" << fieldValue.size()
                               << " greater than " << maxLength;
        if (throwStorageException)
        {
            BOOST_THROW_EXCEPTION(StorageException(errorCode,
                std::string("size of value/key greater than") + std::to_string(maxLength)));
        }
        else
        {
            BOOST_THROW_EXCEPTION(PrecompiledException(
                std::string("size of value/key greater than") + std::to_string(maxLength)));
        }

        return errorCode;
    }
    return 0;
}
