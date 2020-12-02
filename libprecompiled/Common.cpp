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
#include "libstoragestate/StorageState.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libconfig/GlobalConfigure.h>
#include <libethcore/ABI.h>
#include <libstorage/Table.h>

using namespace std;
using namespace bcos;
using namespace bcos::storage;

static const string USER_TABLE_PREFIX = "_user_";
static const string USER_TABLE_PREFIX_SHORT = "u_";
static const string CONTRACT_TABLE_PREFIX_SHORT = "c_";

void bcos::precompiled::getErrorCodeOut(bytes& out, int const& result)
{
    bcos::eth::ContractABI abi;
    if (result > 0 && result < 128)
    {
        out = abi.abiIn("", u256(result));
        return;
    }
    out = abi.abiIn("", s256(result));
}

std::string bcos::precompiled::getTableName(const std::string& _tableName)
{
    return USER_TABLE_PREFIX_SHORT + _tableName;
}

std::string bcos::precompiled::getContractTableName(Address const& _contractAddress)
{
    return std::string(CONTRACT_TABLE_PREFIX_SHORT + _contractAddress.hex());
}


void bcos::precompiled::checkNameValidate(const string& tableName, string& keyField,
    vector<string>& valueFieldList, bool throwStorageException)
{
    set<string> valueFieldSet;
    boost::trim(keyField);
    valueFieldSet.insert(keyField);
    vector<char> allowChar = {'$', '_', '@'};
    std::string allowCharString = "{$, _, @}";
    auto checkTableNameValidate = [allowChar, allowCharString, throwStorageException](
                                      const string& tableName) {
        size_t iSize = tableName.size();
        for (size_t i = 0; i < iSize; i++)
        {
            if (!isalnum(tableName[i]) &&
                (allowChar.end() == find(allowChar.begin(), allowChar.end(), tableName[i])))
            {
                std::string errorMessage =
                    "Invalid table name \"" + tableName +
                    "\", the table name must be letters or numbers, and only supports \"" +
                    allowCharString + "\" as special character set";
                STORAGE_LOG(ERROR) << LOG_DESC(errorMessage);
                // Note: the StorageException and PrecompiledException content can't be modified
                // at will for the information will be write to the blockchain
                if (throwStorageException)
                {
                    BOOST_THROW_EXCEPTION(
                        StorageException(CODE_TABLE_INVALIDATE_FIELD, errorMessage));
                }
                else
                {
                    BOOST_THROW_EXCEPTION(
                        PrecompiledException(string("invalid tablename:") + tableName));
                }
            }
        }
    };
    auto checkFieldNameValidate = [allowChar, allowCharString, throwStorageException](
                                      const string& tableName, const string& fieldName) {
        if (fieldName.size() == 0 || fieldName[0] == '_')
        {
            string errorMessage = "Invalid field \"" + fieldName +
                                  "\", the size of the field must be larger than 0 and the "
                                  "field can't start with \"_\"";
            STORAGE_LOG(ERROR) << LOG_DESC("error key") << LOG_KV("field name", fieldName)
                               << LOG_KV("table name", tableName);
            if (throwStorageException)
            {
                BOOST_THROW_EXCEPTION(StorageException(CODE_TABLE_INVALIDATE_FIELD, errorMessage));
            }
            else
            {
                BOOST_THROW_EXCEPTION(PrecompiledException(string("invalid field:") + fieldName));
            }
        }
        size_t iSize = fieldName.size();
        for (size_t i = 0; i < iSize; i++)
        {
            if (!isalnum(fieldName[i]) &&
                (allowChar.end() == find(allowChar.begin(), allowChar.end(), fieldName[i])))
            {
                std::string errorMessage =
                    "Invalid field \"" + fieldName +
                    "\", the field name must be letters or numbers, and only supports \"" +
                    allowCharString + "\" as special character set";

                STORAGE_LOG(ERROR)
                    << LOG_DESC("invalid fieldname") << LOG_KV("field name", fieldName)
                    << LOG_KV("table name", tableName);
                if (throwStorageException)
                {
                    BOOST_THROW_EXCEPTION(
                        StorageException(CODE_TABLE_INVALIDATE_FIELD, errorMessage));
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
            STORAGE_LOG(ERROR) << LOG_DESC("dumplicate field") << LOG_KV("field name", valueField)
                               << LOG_KV("table name", tableName);
            if (throwStorageException)
            {
                BOOST_THROW_EXCEPTION(StorageException(
                    CODE_TABLE_DUPLICATE_FIELD, string("duplicate field:") + valueField));
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

int bcos::precompiled::checkLengthValidate(
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

bcos::precompiled::ContractStatus bcos::precompiled::getContractStatus(
    std::shared_ptr<bcos::blockverifier::ExecutiveContext> context, std::string const& tableName)
{
    Table::Ptr table = openTable(context, tableName);
    if (!table)
    {
        return ContractStatus::AddressNonExistent;
    }

    auto codeHashEntries =
        table->select(bcos::storagestate::ACCOUNT_CODE_HASH, table->newCondition());
    h256 codeHash;
    codeHash = h256(codeHashEntries->get(0)->getFieldBytes(bcos::storagestate::STORAGE_VALUE));

    if (EmptyHash == codeHash)
    {
        return ContractStatus::NotContractAddress;
    }

    auto frozenEntries = table->select(bcos::storagestate::ACCOUNT_FROZEN, table->newCondition());
    if (frozenEntries->size() > 0 &&
        "true" == frozenEntries->get(0)->getField(bcos::storagestate::STORAGE_VALUE))
    {
        return ContractStatus::Frozen;
    }
    else
    {
        return ContractStatus::Available;
    }
    PRECOMPILED_LOG(ERROR) << LOG_DESC("getContractStatus error")
                           << LOG_KV("table name", tableName);

    return ContractStatus::Invalid;
}

bytes precompiled::PrecompiledException::ToOutput()
{
    eth::ContractABI abi;
    return abi.abiIn("Error(string)", string(what()));
}

uint32_t bcos::precompiled::getParamFunc(bytesConstRef _param)
{
    auto funcBytes = _param.getCroppedData(0, 4);
    uint32_t func = *((uint32_t*)(funcBytes.data()));

    return ((func & 0x000000FF) << 24) | ((func & 0x0000FF00) << 8) | ((func & 0x00FF0000) >> 8) |
           ((func & 0xFF000000) >> 24);
}

bytesConstRef bcos::precompiled::getParamData(bytesConstRef _param)
{
    return _param.getCroppedData(4);
}

uint32_t bcos::precompiled::getFuncSelectorByFunctionName(std::string const& _functionName)
{
    uint32_t func = *(uint32_t*)(crypto::Hash(_functionName).ref().getCroppedData(0, 4).data());
    uint32_t selector = ((func & 0x000000FF) << 24) | ((func & 0x0000FF00) << 8) |
                        ((func & 0x00FF0000) >> 8) | ((func & 0xFF000000) >> 24);
    return selector;
}

// get node list of the given type from the consensus table
bcos::h512s bcos::precompiled::getNodeListByType(
    bcos::storage::Table::Ptr _consTable, int64_t _blockNumber, std::string const& _type)
{
    bcos::h512s list;
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
std::shared_ptr<std::pair<std::string, int64_t>> bcos::precompiled::getSysteConfigByKey(
    bcos::storage::Table::Ptr _sysConfigTable, std::string const& _key, int64_t const& _num)
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

bcos::storage::Table::Ptr bcos::precompiled::openTable(
    bcos::blockverifier::ExecutiveContext::Ptr _context, const std::string& _tableName)
{
    return _context->getMemoryTableFactory()->openTable(_tableName);
}
