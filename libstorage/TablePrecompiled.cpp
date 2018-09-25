/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file TablePrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */
#include "TablePrecompiled.h"
#include "ConditionPrecompiled.h"
#include "EntriesPrecompiled.h"
#include "EntryPrecompiled.h"
#include "Table.h"
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::blockverifier;


std::string TablePrecompiled::toString(std::shared_ptr<ExecutiveContext>)
{
    return "Table";
}

bytes TablePrecompiled::call(std::shared_ptr<ExecutiveContext> context, bytesConstRef param)
{
    LOG(DEBUG) << "call Table";

    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    LOG(DEBUG) << "func:" << std::hex << func;

    dev::eth::ContractABI abi;

    bytes out;

    switch (func)
    {
    case 0xe8434e39:
    {  // select(string,address)
        std::string key;
        Address conditionAddress;
        abi.abiOut(data, key, conditionAddress);

        ConditionPrecompiled::Ptr conditionPrecompiled =
            std::dynamic_pointer_cast<ConditionPrecompiled>(
                context->getPrecompiled(conditionAddress));
        auto condition = conditionPrecompiled->getCondition();

        auto entries = m_table->select(key, condition);
        auto entriesPrecompiled = std::make_shared<EntriesPrecompiled>();
        entriesPrecompiled->setEntries(entries);

        auto newAddress = context->registerPrecompiled(entriesPrecompiled);
        out = abi.abiIn("", newAddress);

        break;
    }
    case 0x31afac36:
    {  // insert(string,address)
        std::string key;
        Address entryAddress;
        abi.abiOut(data, key, entryAddress);

        EntryPrecompiled::Ptr entryPrecompiled =
            std::dynamic_pointer_cast<EntryPrecompiled>(context->getPrecompiled(entryAddress));
        auto entry = entryPrecompiled->getEntry();

        size_t count = m_table->insert(key, entry);
        out = abi.abiIn("", u256(count));

        break;
    }
    case 0x7857d7c9:
    {  // newCondition()
        auto condition = m_table->newCondition();
        auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>();
        conditionPrecompiled->setCondition(condition);

        auto newAddress = context->registerPrecompiled(conditionPrecompiled);
        out = abi.abiIn("", newAddress);

        break;
    }
    case 0x13db9346:
    {  // newEntry()
        auto entry = m_table->newEntry();
        auto entryPrecompiled = std::make_shared<EntryPrecompiled>();
        entryPrecompiled->setEntry(entry);

        auto newAddress = context->registerPrecompiled(entryPrecompiled);
        out = abi.abiIn("", newAddress);

        break;
    }
    case 0x28bb2117:
    {  // remove(string,address)
        std::string key;
        Address conditionAddress;
        abi.abiOut(data, key, conditionAddress);

        ConditionPrecompiled::Ptr conditionPrecompiled =
            std::dynamic_pointer_cast<ConditionPrecompiled>(
                context->getPrecompiled(conditionAddress));
        auto condition = conditionPrecompiled->getCondition();

        size_t count = m_table->remove(key, condition);
        out = abi.abiIn("", u256(count));

        break;
    }
    case 0xbf2b70a1:
    {  // update(string,address,address)
        std::string key;
        Address entryAddress;
        Address conditionAddress;
        abi.abiOut(data, key, entryAddress, conditionAddress);

        EntryPrecompiled::Ptr entryPrecompiled =
            std::dynamic_pointer_cast<EntryPrecompiled>(context->getPrecompiled(entryAddress));
        ConditionPrecompiled::Ptr conditionPrecompiled =
            std::dynamic_pointer_cast<ConditionPrecompiled>(
                context->getPrecompiled(conditionAddress));
        auto entry = entryPrecompiled->getEntry();
        auto condition = conditionPrecompiled->getCondition();

        size_t count = m_table->update(key, entry, condition);
        out = abi.abiIn("", u256(count));

        break;
    }
#if 0
	case 0xbc902ad2: { //insert(address)
		Address address;
		abi.abiOut(data, address);

		EntryPrecompiled::Ptr entryPrecompiled = std::dynamic_pointer_cast<EntryPrecompiled>(context->getPrecompiled(address));
		auto entry = entryPrecompiled->getEntry();

		size_t count = m_table->insert(entry);
		out = abi.abiIn("", u256(count));

		break;
	}
	case 0x7857d7c9: { //newCondition()
		auto condition = m_table->newCondition();
		auto conditionPrecompiled = std::make_shared<ConditionPrecompiled>();
		conditionPrecompiled->setCondition(condition);

		auto newAddress = context->registerPrecompiled(conditionPrecompiled);
		out = abi.abiIn("", newAddress);

		break;
	}
	case 0x13db9346: { //newEntry()
		auto entry = m_table->newEntry();
		auto entryPrecompiled = std::make_shared<EntryPrecompiled>();
		entryPrecompiled->setEntry(entry);

		auto newAddress = context->registerPrecompiled(entryPrecompiled);
		out = abi.abiIn("", newAddress);

		break;
	}
	case 0x29092d0e: { //remove(address)
		Address address;
		abi.abiOut(data, address);

		ConditionPrecompiled::Ptr conditionPrecompiled = std::dynamic_pointer_cast<ConditionPrecompiled>(context->getPrecompiled(address));
		auto condition = conditionPrecompiled->getCondition();

		size_t count = m_table->remove(condition);
		out = abi.abiIn("", u256(count));

		break;
	}
	case 0x4f49f01c: { //select(address)
		Address address;
		abi.abiOut(data, address);

		ConditionPrecompiled::Ptr conditionPrecompiled = std::dynamic_pointer_cast<ConditionPrecompiled>(context->getPrecompiled(address));
		auto condition = conditionPrecompiled->getCondition();

		auto entries = m_table->select(condition);
		auto entriesPrecompiled = std::make_shared<EntriesPrecompiled>();
		entriesPrecompiled->setEntries(entries);

		auto newAddress = context->registerPrecompiled(entriesPrecompiled);
		out = abi.abiIn("", newAddress);

		break;
	}
	case 0xc640752d: { //update(address,address)
		Address entryAddress;
		Address conditionAddress;
		abi.abiOut(data, entryAddress, conditionAddress);

		EntryPrecompiled::Ptr entryPrecompiled = std::dynamic_pointer_cast<EntryPrecompiled>(context->getPrecompiled(entryAddress));
		ConditionPrecompiled::Ptr conditionPrecompiled = std::dynamic_pointer_cast<ConditionPrecompiled>(context->getPrecompiled(conditionAddress));
		auto entry = entryPrecompiled->getEntry();
		auto condition = conditionPrecompiled->getCondition();

		size_t count = m_table->update(entry, condition);
		out = abi.abiIn("", u256(count));

		break;
	}
#endif
    default:
    {
        break;
    }
    }

    return out;
}

h256 TablePrecompiled::hash()
{
    return m_table->hash();
}
