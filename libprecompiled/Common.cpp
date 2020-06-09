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
#include <libstorage/Table.h>

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

uint32_t dev::precompiled::getParamFunc(bytesConstRef _param)
{
    auto funcBytes = _param.cropped(0, 4);
    uint32_t func = *((uint32_t*)(funcBytes.data()));

    return ((func & 0x000000FF) << 24) | ((func & 0x0000FF00) << 8) | ((func & 0x00FF0000) >> 8) |
           ((func & 0xFF000000) >> 24);
}

bytesConstRef dev::precompiled::getParamData(bytesConstRef _param)
{
    return _param.cropped(4);
}

uint32_t dev::precompiled::getFuncSelectorByFunctionName(std::string const& _functionName)
{
    uint32_t func = *(uint32_t*)(crypto::Hash(_functionName).ref().cropped(0, 4).data());
    uint32_t selector = ((func & 0x000000FF) << 24) | ((func & 0x0000FF00) << 8) |
                        ((func & 0x00FF0000) >> 8) | ((func & 0xFF000000) >> 24);
    return selector;
}

// get node list of the given type from the consensus table
dev::h512s dev::precompiled::getNodeListByType(
    dev::storage::Table::Ptr _consTable, int64_t _blockNumber, std::string const& _type)
{
    dev::h512s list;
    try
    {
        auto nodes = _consTable->select(PRI_KEY, _consTable->newCondition());
        if (!nodes)
            return list;

        for (size_t i = 0; i < nodes->size(); i++)
        {
            auto node = nodes->get(i);
            if (!node)
                return list;

            if ((node->getField(NODE_TYPE) == _type) &&
                (boost::lexical_cast<int>(node->getField(NODE_KEY_ENABLENUM)) <= _blockNumber))
            {
                h512 nodeID = h512(node->getField(NODE_KEY_NODEID));
                list.push_back(nodeID);
            }
        }
    }
    catch (std::exception& e)
    {
        PRECOMPILED_LOG(ERROR) << LOG_DESC("[#getNodeListByType]Failed")
                               << LOG_KV("EINFO", boost::diagnostic_information(e));
    }
    return list;
}

// Get the configuration value of the given key from the system configuration table
std::shared_ptr<std::pair<std::string, int64_t>> dev::precompiled::getSysteConfigByKey(
    dev::storage::Table::Ptr _sysConfigTable, std::string const& _key, int64_t const& _num)
{
    std::shared_ptr<std::pair<std::string, int64_t>> result =
        std::make_shared<std::pair<std::string, int64_t>>();
    *result = std::make_pair("", -1);

    auto values = _sysConfigTable->select(_key, _sysConfigTable->newCondition());
    if (!values || values->size() != 1)
    {
        PRECOMPILED_LOG(ERROR) << LOG_DESC("[#getSystemConfigByKey]Select error")
                               << LOG_KV("key", _key);
        // FIXME: throw exception here, or fatal error
        return result;
    }

    auto value = values->get(0);
    if (!value)
    {
        PRECOMPILED_LOG(ERROR) << LOG_DESC("[#getSystemConfigByKey]Null pointer")
                               << LOG_KV("key", _key);
        // FIXME: throw exception here, or fatal error
        return result;
    }
    if (boost::lexical_cast<int64_t>(value->getField(SYSTEM_CONFIG_ENABLENUM)) <= _num)
    {
        result->first = value->getField(SYSTEM_CONFIG_VALUE);
        result->second = boost::lexical_cast<int64_t>(value->getField(SYSTEM_CONFIG_ENABLENUM));
    }
    return result;
}