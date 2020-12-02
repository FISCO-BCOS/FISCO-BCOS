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
/** @file CNSPrecompiled.cpp
 *  @author chaychen
 *  @date 20181119
 */
#include "CNSPrecompiled.h"
#include <json/json.h>
#include <libblockverifier/ExecutiveContext.h>
#include <libethcore/ABI.h>
#include <libprecompiled/EntriesPrecompiled.h>
#include <libprecompiled/TableFactoryPrecompiled.h>

using namespace std;
using namespace bcos;
using namespace bcos::blockverifier;
using namespace bcos::storage;
using namespace bcos::precompiled;

const char* const CNS_METHOD_INS_STR4 = "insert(string,string,string,string)";
const char* const CNS_METHOD_SLT_STR = "selectByName(string)";
const char* const CNS_METHOD_SLT_STR2 = "selectByNameAndVersion(string,string)";
const char* const CNS_METHOD_GET_CONTRACT_ADDRESS = "getContractAddress(string,string)";

CNSPrecompiled::CNSPrecompiled()
{
    name2Selector[CNS_METHOD_INS_STR4] = getFuncSelector(CNS_METHOD_INS_STR4);
    name2Selector[CNS_METHOD_SLT_STR] = getFuncSelector(CNS_METHOD_SLT_STR);
    name2Selector[CNS_METHOD_SLT_STR2] = getFuncSelector(CNS_METHOD_SLT_STR2);
    name2Selector[CNS_METHOD_GET_CONTRACT_ADDRESS] =
        getFuncSelector(CNS_METHOD_GET_CONTRACT_ADDRESS);
}


std::string CNSPrecompiled::toString()
{
    return "CNS";
}

// check param of the cns
bool CNSPrecompiled::checkCNSParam(ExecutiveContext::Ptr _context,
    std::string const& _contractAddress, std::string const& _contractName,
    std::string const& _contractAbi)
{
    // check address
    Address contractAddress;
    try
    {
        contractAddress = Address(_contractAddress);
    }
    catch (...)
    {
        return false;
    }

    try
    {
        // check the status of the contract(only print the error message to the log)
        std::string tableName = precompiled::getContractTableName(contractAddress);
        ContractStatus contractStatus = getContractStatus(_context, tableName);

        if (contractStatus != ContractStatus::Available)
        {
            std::string errorMessage = "CNS operation failed for ";
            switch (contractStatus)
            {
            case ContractStatus::Invalid:
                errorMessage += "invalid contract \"" + _contractName +
                                "\", contractAddress = " + _contractAddress;
                break;
            case ContractStatus::Frozen:
                errorMessage += "\"" + _contractName +
                                "\" has been frozen, contractAddress = " + _contractAddress;
                break;
            case ContractStatus::AddressNonExistent:
                errorMessage += "the contract \"" + _contractName + "\" with address " +
                                _contractAddress + " does not exist";
                break;
            case ContractStatus::NotContractAddress:
                errorMessage += "invalid address " + _contractAddress +
                                ", please make sure it's a contract address";
                break;
            default:
                errorMessage += "invalid contract \"" + _contractName + "\" with address " +
                                _contractAddress + ", error code:" + std::to_string(contractStatus);
                break;
            }
            PRECOMPILED_LOG(INFO) << LOG_BADGE("CNSPrecompiled") << LOG_DESC(errorMessage)
                                  << LOG_KV("contractAddress", _contractAddress)
                                  << LOG_KV("contractName", _contractName);
        }
    }
    catch (std::exception const& _e)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("CNSPrecompiled")
                                 << LOG_DESC("check contract status exception")
                                 << LOG_KV("contractAddress", _contractAddress)
                                 << LOG_KV("contractName", _contractName)
                                 << LOG_KV("e", boost::diagnostic_information(_e));
    }
    // check the length of the key
    checkLengthValidate(
        _contractName, USER_TABLE_KEY_VALUE_MAX_LENGTH, CODE_TABLE_KEYVALUE_LENGTH_OVERFLOW);
    // check the length of the field value
    // (since the contract version length will be checked, here no need to check _contractVersion)
    checkLengthValidate(
        _contractAbi, USER_TABLE_FIELD_VALUE_MAX_LENGTH, CODE_TABLE_FIELDVALUE_LENGTH_OVERFLOW);
    return true;
}

PrecompiledExecResult::Ptr CNSPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin, Address const&)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", *toHexString(param));

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    bcos::eth::ContractABI abi;
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();

    callResult->gasPricer()->setMemUsed(param.size());

    if (func == name2Selector[CNS_METHOD_INS_STR4])
    {  // FIXME: modify insert(string,string,string,string) ==> insert(string,string,address,string)
        // insert(name, version, address, abi), 4 fields in table, the key of table is name field
        std::string contractName, contractVersion, contractAddress, contractAbi;
        abi.abiOut(data, contractName, contractVersion, contractAddress, contractAbi);
        Table::Ptr table = openTable(context, SYS_CNS);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::OpenTable);

        bool isValid = checkCNSParam(context, contractAddress, contractName, contractAbi);
        // check exist or not
        bool exist = false;
        auto entries = table->select(contractName, table->newCondition());
        // Note: The selection here is only used as an internal logical judgment,
        // so only calculate the computation gas
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Select, entries->size());

        if (entries.get())
        {
            for (size_t i = 0; i < entries->size(); i++)
            {
                auto entry = entries->get(i);
                if (!entry)
                    continue;
                if (entry->getField(SYS_CNS_FIELD_VERSION) == contractVersion)
                {
                    exist = true;
                    break;
                }
            }
        }
        int result = 0;
        if (contractVersion.size() > CNS_VERSION_MAX_LENGTH)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("CNS") << LOG_DESC("version length overflow 128")
                << LOG_KV("contractName", contractName) << LOG_KV("address", contractAddress)
                << LOG_KV("version", contractVersion);
            result = CODE_VERSION_LENGTH_OVERFLOW;
        }
        else if (exist)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("CNSPrecompiled") << LOG_DESC("address and version exist")
                << LOG_KV("contractName", contractName) << LOG_KV("address", contractAddress)
                << LOG_KV("version", contractVersion);
            result = CODE_ADDRESS_AND_VERSION_EXIST;
        }
        else if (!isValid)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("address invalid")
                                   << LOG_KV("address", contractAddress);
            result = CODE_ADDRESS_INVALID;
        }
        else
        {
            // do insert
            auto entry = table->newEntry();
            entry->setField(SYS_CNS_FIELD_NAME, contractName);
            entry->setField(SYS_CNS_FIELD_VERSION, contractVersion);
            entry->setField(SYS_CNS_FIELD_ADDRESS, contractAddress);
            entry->setField(SYS_CNS_FIELD_ABI, contractAbi);
            int count = table->insert(contractName, entry, std::make_shared<AccessOptions>(origin));
            if (count > 0)
            {
                callResult->gasPricer()->updateMemUsed(entry->size() * count);
                callResult->gasPricer()->appendOperation(InterfaceOpcode::Insert, count);
            }
            if (count == storage::CODE_NO_AUTHORIZED)
            {
                PRECOMPILED_LOG(DEBUG)
                    << LOG_BADGE("CNSPrecompiled") << LOG_DESC("permission denied");
                result = storage::CODE_NO_AUTHORIZED;
            }
            else
            {
                PRECOMPILED_LOG(DEBUG)
                    << LOG_BADGE("CNSPrecompiled") << LOG_DESC("insert successfully");
                result = count;
            }
        }
        getErrorCodeOut(callResult->mutableExecResult(), result);
    }
    else if (func == name2Selector[CNS_METHOD_SLT_STR])
    {
        // selectByName(string) returns(string)
        // Cursor is not considered.
        std::string contractName;
        abi.abiOut(data, contractName);
        Table::Ptr table = openTable(context, SYS_CNS);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::OpenTable);

        Json::Value CNSInfos(Json::arrayValue);
        auto entries = table->select(contractName, table->newCondition());
        // Note: Because the selected data has been returned as cnsInfo,
        // the memory is not updated here
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Set, entries->size());

        if (entries.get())
        {
            for (size_t i = 0; i < entries->size(); i++)
            {
                auto entry = entries->get(i);
                if (!entry)
                    continue;
                Json::Value CNSInfo;
                CNSInfo[SYS_CNS_FIELD_NAME] = contractName;
                CNSInfo[SYS_CNS_FIELD_VERSION] = entry->getField(SYS_CNS_FIELD_VERSION);
                CNSInfo[SYS_CNS_FIELD_ADDRESS] = entry->getField(SYS_CNS_FIELD_ADDRESS);
                CNSInfo[SYS_CNS_FIELD_ABI] = entry->getField(SYS_CNS_FIELD_ABI);
                CNSInfos.append(CNSInfo);
            }
        }
        Json::FastWriter fastWriter;
        std::string str = fastWriter.write(CNSInfos);
        callResult->setExecResult(abi.abiIn("", str));
    }
    else if (func == name2Selector[CNS_METHOD_SLT_STR2])
    {
        // selectByNameAndVersion(string,string) returns(string)
        std::string contractName, contractVersion;
        abi.abiOut(data, contractName, contractVersion);
        Table::Ptr table = openTable(context, SYS_CNS);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::OpenTable);

        Json::Value CNSInfos(Json::arrayValue);
        auto condition = table->newCondition();
        condition->EQ(SYS_CNS_FIELD_VERSION, contractVersion);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::EQ);

        auto entries = table->select(contractName, condition);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Select, entries->size());

        if (entries->size() != 0)
        {
            auto entry = entries->get(0);
            Json::Value CNSInfo;
            CNSInfo[SYS_CNS_FIELD_NAME] = contractName;
            CNSInfo[SYS_CNS_FIELD_VERSION] = entry->getField(SYS_CNS_FIELD_VERSION);
            CNSInfo[SYS_CNS_FIELD_ADDRESS] = entry->getField(SYS_CNS_FIELD_ADDRESS);
            CNSInfo[SYS_CNS_FIELD_ABI] = entry->getField(SYS_CNS_FIELD_ABI);
            CNSInfos.append(CNSInfo);
        }
        Json::FastWriter fastWriter;
        std::string str = fastWriter.write(CNSInfos);
        callResult->setExecResult(abi.abiIn("", str));
    }
    else if (func == name2Selector[CNS_METHOD_GET_CONTRACT_ADDRESS])
    {  // getContractAddress(string,string) returns(address)
        std::string contractName, contractVersion;
        abi.abiOut(data, contractName, contractVersion);
        Table::Ptr table = openTable(context, SYS_CNS);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::OpenTable);

        Address ret;
        auto condition = table->newCondition();
        condition->EQ(SYS_CNS_FIELD_VERSION, contractVersion);
        auto entries = table->select(contractName, condition);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Select, entries->size());

        if (entries.get())
        {
            auto entry = entries->get(0);
            string value = entry->getField(SYS_CNS_FIELD_ADDRESS);
            ret = Address(value);
        }
        callResult->setExecResult(abi.abiIn("", ret));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CNSPrecompiled") << LOG_DESC("call undefined function")
                               << LOG_KV("func", func);
    }

    return callResult;
}
