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
/** @file MemoryTable.cpp
 *  @author ancelmo
 *  @date 20180921
 */
#include "MemoryTable.h"
#include "Common.h"
#include "Table.h"
#include <json/json.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Hash.h>
#include <libdevcore/FixedHash.h>
#include <libprecompiled/Common.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <arpa/inet.h>

using namespace dev;
using namespace dev::storage;
using namespace dev::precompiled;

MemoryTable::MemoryTable() {
	m_newEntries = std::make_shared<Entries>();
}

Entries::Ptr MemoryTable::select(const std::string& key, Condition::Ptr condition) {
	try {
		if(m_remoteDB) {
			condition->EQ(m_tableInfo->key, key);
			//query remoteDB anyway
			Entries::Ptr dbEntries = m_remoteDB->select(m_blockHash, m_blockNum, m_tableInfo->name, key, condition);

			if(!dbEntries) {
				return std::make_shared<Entries>();
			}

			auto entries = std::make_shared<Entries>();
			for(size_t i=0; i<dbEntries->size(); ++i) {
				auto entryIt = m_cache.find(dbEntries->get(i)->getID());
				if(entryIt != m_cache.end()) {
					entries->addEntry(entryIt->second);
				}
				else {
					entries->addEntry(dbEntries->get(i));
				}
			}

			auto indices = processEntries(m_newEntries, condition);
			for(auto it : indices) {
				entries->addEntry(m_newEntries->get(it));
			}

			return entries;
		}
	} catch (std::exception& e) {
		 STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTable") << LOG_DESC("Table select failed for")
		                   << LOG_KV("msg", boost::diagnostic_information(e));
	}


	return std::make_shared<Entries>();
}

int MemoryTable::update(const std::string& key, Entry::Ptr entry, Condition::Ptr condition,
            AccessOptions::Ptr options) {
	try {
		if (options->check && !checkAuthority(options->origin)) {
			STORAGE_LOG(WARNING) << LOG_BADGE("MemoryTable") << LOG_DESC("update non-authorized")
			<< LOG_KV("origin", options->origin.hex()) << LOG_KV("key", key);

			return storage::CODE_NO_AUTHORIZED;
		}

		checkField(entry);

		auto entries = select(key, condition);

		std::vector<Change::Record> records;

		for(size_t i=0; i<entries->size(); ++i) {
			auto updateEntry = entries->get(i);
			size_t newIndex = 0;

			//if id equals to zero and not in the m_cache, must be new dirty entry
			if(updateEntry->getID() != 0 && m_cache.find(updateEntry->getID()) == m_cache.end()) {
				newIndex = m_cache.size();
				m_cache.insert(std::make_pair(updateEntry->getID(), updateEntry));
			}

			for (auto& it : *(entry->fields())) {
				records.emplace_back(updateEntry->getID(), newIndex, it.first, updateEntry->getField(it.first));
				updateEntry->setField(it.first, it.second);
			}
		}

		 m_recorder(shared_from_this(), Change::Update, key, records);

		return entries->size();
	} catch (std::exception& e) {
		STORAGE_LOG(ERROR)<< LOG_BADGE("MemoryTable")
		<< LOG_DESC("Access MemoryTable failed for")
		<< LOG_KV("msg", boost::diagnostic_information(e));
	}

	return 0;
}

int MemoryTable::insert(const std::string& key, Entry::Ptr entry,
            AccessOptions::Ptr options, bool needSelect) {
	try {
		(void)needSelect;

		if (options->check && !checkAuthority(options->origin)) {
			STORAGE_LOG(WARNING) << LOG_BADGE("MemoryTable") << LOG_DESC("insert non-authorized")
			<< LOG_KV("origin", options->origin.hex()) << LOG_KV("key", key);
			return storage::CODE_NO_AUTHORIZED;
		}

		checkField(entry);

		entry->setField(m_tableInfo->key, key);
		Change::Record record(0, m_newEntries->size());
		m_newEntries->addEntry(entry);

		std::vector<Change::Record> value{record};
		m_recorder(shared_from_this(), Change::Insert, key, value);

		return 1;
	} catch (std::exception& e) {
		STORAGE_LOG(ERROR)<< LOG_BADGE("MemoryTable")
		<< LOG_DESC("Access MemoryTable failed for")
		<< LOG_KV("msg", boost::diagnostic_information(e));
	}

	return 0;
}

int MemoryTable::remove(const std::string& key, Condition::Ptr condition, AccessOptions::Ptr options) {
	try {
		if (options->check && !checkAuthority(options->origin)) {
			STORAGE_LOG(WARNING) << LOG_BADGE("MemoryTable") << LOG_DESC("remove non-authorized")
								<< LOG_KV("origin", options->origin.hex()) << LOG_KV("key", key);
			return storage::CODE_NO_AUTHORIZED;
		}

		auto entries = select(key, condition);

		std::vector<Change::Record> records;
		for(size_t i=0; i<entries->size(); ++i) {
			auto removeEntry = entries->get(i);
			size_t newIndex = 0;

			removeEntry->setStatus(1);

			//if id equals to zero and not in the m_cache, must be new dirty entry
			if(removeEntry->getID() != 0 && m_cache.find(removeEntry->getID()) == m_cache.end()) {
				m_cache.insert(std::make_pair(removeEntry->getID(), removeEntry));
			}

			records.emplace_back(removeEntry->getID(), newIndex);
		}

		m_recorder(shared_from_this(), Change::Remove, key, records);

		return entries->size();
	} catch (std::exception& e) {
		STORAGE_LOG(ERROR)<< LOG_BADGE("MemoryTable")
		<< LOG_DESC("Access MemoryTable failed for")
		<< LOG_KV("msg", boost::diagnostic_information(e));
	}

	return 0;
}

dev::h256 MemoryTable::hash() {
	bytes data;

	for (auto it : m_cache) {
		auto id = htonl(it.first);
		data.insert(data.end(), (char*)&id, (char*)&id + sizeof(id));
		for(auto fieldIt: *(it.second->fields())) {
			if(isHashField(fieldIt.first)) {
				data.insert(data.end(), fieldIt.first.begin(), fieldIt.first.end());
				data.insert(data.end(), fieldIt.second.begin(), fieldIt.second.end());
			}
		}
	}

	for(size_t i=0; i<m_newEntries->size(); ++i) {
		auto entry = m_newEntries->get(i);
		for(auto fieldIt: *(entry->fields())) {
			if(isHashField(fieldIt.first)) {
				data.insert(data.end(), fieldIt.first.begin(), fieldIt.first.end());
				data.insert(data.end(), fieldIt.second.begin(), fieldIt.second.end());
			}
		}
	}

	if(data.empty()) {
		return h256();
	}

	bytesConstRef bR(data.data(), data.size());
	h256 hash = dev::sha256(bR);

	return hash;
}

void MemoryTable::rollback(const Change& _change)
{
	switch (_change.kind)
	{
	case Change::Insert:
	{
		m_newEntries->removeEntry(_change.value[0].newIndex);
		break;
	}
	case Change::Update:
	{
		for (auto& record : _change.value)
		{
			if(record.id) {
				auto it = m_cache.find(record.id);
				if(it != m_cache.end()) {
					it->second->setField(record.key, record.oldValue);
				}
			}
			else {
				auto entry = m_newEntries->get(record.newIndex);
				entry->setField(record.key, record.oldValue);
			}
		}
		break;
	}
	case Change::Remove:
	{
		for (auto& record : _change.value)
		{
			if(record.id) {
				auto it = m_cache.find(record.id);
				if(it != m_cache.end()) {
					it->second->setStatus(0);
				}
			}
			else {
				auto entry = m_newEntries->get(record.newIndex);
				entry->setStatus(0);
			}
		}
		break;
	}
	case Change::Select:

	default:
		break;
	}
}
