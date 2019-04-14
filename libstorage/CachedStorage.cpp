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
/** @file storage.h
 *  @author monan
 *  @date 20190409
 */

#include "CachedStorage.h"
#include "StorageException.h"
#include <libdevcore/easylog.h>
#include <libdevcore/FixedHash.h>

using namespace dev;
using namespace dev::storage;

Entries::Ptr Caches::entries() {
	return m_entries;
}

void Caches::setEntries(Entries::Ptr entries) {
	m_entries = entries;
}

int64_t Caches::num() const {
	return m_num;
}

void Caches::setNum(int64_t num) {
	m_num = num;
}

TableInfo::Ptr TableCaches::tableInfo() {
	return m_tableInfo;
}

Caches::Ptr TableCaches::findCache(const std::string &key) {
	auto it = m_caches.find(key);
	if(it != m_caches.end()) {
		return it->second;
	}
	else {
		return Caches::Ptr();
	}
}

void TableCaches::addCache(const std::string &key, Caches::Ptr cache) {
	auto it = m_caches.find(key);
	if(it != m_caches.end()) {
		it->second = cache;
	}
	else {
		m_caches.insert(std::make_pair(key, cache));
	}
}

void TableCaches::removeCache(const std::string &key) {
	auto it = m_caches.find(key);
	if(it != m_caches.end()) {
		m_caches.erase(it);
	}
}

std::map<std::string, Caches::Ptr>* TableCaches::caches() {
	return &m_caches;
}

CachedStorage::CachedStorage() {
	m_taskThreadPool = std::make_shared<dev::ThreadPool>("FlushStorage", 1);
	m_writeLock = std::make_shared<boost::shared_mutex>();
}

Entries::Ptr CachedStorage::select(h256 hash, int num, const std::string& table,
        const std::string& key, Condition::Ptr condition) {
	STORAGE_LOG(TRACE) << "Query data from cachedStorage table: " << table << " key: " << key;
	auto out = std::make_shared<Entries>();

	auto entries = selectNoCondition(hash, num, table, key, condition)->entries();

	for(size_t i=0; i<entries->size(); ++i) {
		auto entry = entries->get(i);
		if(condition) {
			if(condition->process(entry)) {
				auto outEntry = std::make_shared<Entry>();
				outEntry->copyFrom(entry);

				out->addEntry(outEntry);
			}
		}
		else {
			auto outEntry = std::make_shared<Entry>();
			outEntry->copyFrom(entry);

			out->addEntry(outEntry);
		}
	}

	return out;
}

Caches::Ptr CachedStorage::selectNoCondition(h256 hash, int num, const std::string& table,
        const std::string& key, Condition::Ptr condition) {
	(void)condition;

	auto tableIt = m_caches.find(table);
	if(tableIt != m_caches.end()) {
		auto tableCaches = tableIt->second;
		auto caches = tableCaches->findCache(key);
		if(caches) {
			auto r = m_mru.push_front(std::make_pair(table, key));
			if(!r.second) {
				m_mru.relocate(m_mru.end(), r.first);
			}

			return caches;
		}
	}

	if(m_backend) {
		auto backendData = m_backend->select(hash, num, table, key, nullptr);

		auto tableIt = m_caches.find(table);
		if(tableIt == m_caches.end()) {
			tableIt = m_caches.insert(std::make_pair(table, std::make_shared<TableCaches>())).first;
			tableIt->second->tableInfo()->name = table;
		}

		auto caches = std::make_shared<Caches>();
		caches->setEntries(backendData);

		tableIt->second->addCache(key, caches);

		return caches;
	}

	return std::make_shared<Caches>();
}

size_t CachedStorage::commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas) {
	STORAGE_LOG(TRACE) << "CachedStorage commit: " << datas.size();

	STORAGE_LOG(TRACE) << "Commit data";
	for(auto it: datas) {
		STORAGE_LOG(TRACE) << "table: " << it->info->name;
		for(size_t i=0;i<it->entries->size();++i) {
			auto entry = it->entries->get(i);

			std::stringstream ss;
			for(auto fieldIt: *(entry->fields())) {
				ss << " " << fieldIt.first << ":" << fieldIt.second;
			}

			STORAGE_LOG(TRACE) << "Entry: " << ss.str();
		}
	}

	size_t total = 0;

	std::vector<TableData::Ptr> commitDatas;
	//update cache & copy datas
	for(auto it: datas) {
		auto tableData = std::make_shared<TableData>();
		tableData->info = it->info;

		for(size_t i=0; i<it->entries->size(); ++i) {
			auto entry = it->entries->get(i);
			auto key = entry->getField(it->info->key);

			auto caches = selectNoCondition(h256(), 0, it->info->name, key, nullptr);

			for(size_t i=0;i<it->entries->size();++i) {
				++total;
				auto entry = it->entries->get(i);
				auto id = entry->getID();
				if(id != 0) {
					auto data = caches->entries()->entries();
					auto entryIt = std::lower_bound(data->begin(), data->end(), entry, [](const Entry::Ptr &lhs, const Entry::Ptr &rhs) {
						return lhs->getID() < rhs->getID();
					});

					if(entryIt != data->end() && (*entryIt)->getID() == id) {
						for(auto fieldIt: *entry->fields()) {
							if (fieldIt.first != "_id_") {
								(*entryIt)->setField(fieldIt.first, fieldIt.second);
							}
						}

						auto commitEntry = std::make_shared<Entry>();
						commitEntry->copyFrom(*entryIt);
						tableData->entries->addEntry(commitEntry);
					}
					else {
						STORAGE_LOG(ERROR) << "Can not find entry in cache, id:" << entry->getID() << " key:" << key;

						//impossible
						BOOST_THROW_EXCEPTION(StorageException(-1, "Can not find entry in cache, id: " + boost::lexical_cast<std::string>(entry->getID())));
					}
				}
				else {
					auto cacheEntry = std::make_shared<Entry>();
					cacheEntry->copyFrom(entry);
					cacheEntry->setID(++m_ID);
					caches->entries()->addEntry(cacheEntry);

					LOG(TRACE) << "Set new entry ID: " << cacheEntry->getID();

					auto commitEntry = std::make_shared<Entry>();
					commitEntry->copyFrom(cacheEntry);
					tableData->entries->addEntry(commitEntry);
				}
			}

			caches->setNum(num);
		}

		commitDatas.push_back(tableData);
	}

	STORAGE_LOG(TRACE) << "Current cache --";
	for(auto it: m_caches) {
		STORAGE_LOG(TRACE) << "table: " << it.second->tableInfo()->name;
		for(auto cacheIt: *(it.second->caches())) {
			STORAGE_LOG(TRACE) << "key: " << cacheIt.first;
			for(size_t i=0;i<cacheIt.second->entries()->size();++i) {
				auto entry = cacheIt.second->entries()->get(i);

				std::stringstream ss;
				for(auto fieldIt: *(entry->fields())) {
					ss << " " << fieldIt.first << ":" << fieldIt.second;
				}

				STORAGE_LOG(TRACE) << "Entry: " << ss.str();
			}
		}
	}

	STORAGE_LOG(TRACE) << "Commit commitDatas ---";
	for(auto it: commitDatas) {
		STORAGE_LOG(TRACE) << "table: " << it->info->name;
		for(size_t i=0;i<it->entries->size();++i) {
			auto entry = it->entries->get(i);

			std::stringstream ss;
			for(auto fieldIt: *(entry->fields())) {
				ss << " " << fieldIt.first << ":" << fieldIt.second;
			}

			STORAGE_LOG(TRACE) << "Entry: " << ss.str();
		}
	}

	//new task write to backend
	Task::Ptr task = std::make_shared<Task>();
	task->hash = hash;
	task->num = num;
	task->datas = commitDatas;

	TableData::Ptr data = std::make_shared<TableData>();
	data->info->name = SYS_CURRENT_STATE;
	data->info->key = SYS_KEY;
	data->info->fields = std::vector<std::string>{"value"};

	Entry::Ptr idEntry = std::make_shared<Entry>();
	idEntry->setID(1);
	idEntry->setNum(num);
	idEntry->setStatus(0);
	idEntry->setField(SYS_KEY, SYS_KEY_CURRENT_ID);
	idEntry->setField("value", boost::lexical_cast<std::string>(m_ID));

	data->entries->addEntry(idEntry);

	task->datas.push_back(data);
	auto backend = m_backend;
	auto self = std::weak_ptr<CachedStorage>(std::dynamic_pointer_cast<CachedStorage>(shared_from_this()));

	m_taskThreadPool->enqueue([backend, task, self]() {
		STORAGE_LOG(INFO) << "Start commit block: " << task->num << " to persist storage";
		backend->commit(task->hash, task->num, task->datas);
		STORAGE_LOG(INFO) << "Commit block: " << task->num << " to persist storage finished";

		auto storage = self.lock();
		if(storage) {
			storage->setSyncNum(task->num);
		}
	});

	STORAGE_LOG(INFO) << "Submited block task: " << num << ", current syncd block: " << m_syncNum;

	return total;
}

bool CachedStorage::onlyDirty() {
	return true;
}

void CachedStorage::setBackend(Storage::Ptr backend) {
	m_backend = backend;
}

void CachedStorage::init() {
	//get id from backend
	auto out = m_backend->select(h256(), 0, SYS_CURRENT_STATE, SYS_KEY_CURRENT_ID, nullptr);
	if(out->size() > 0) {
		auto entry = out->get(0);
		auto numStr = entry->getField(SYS_VALUE);
		m_ID = boost::lexical_cast<size_t>(numStr);
	}
}


int64_t CachedStorage::syncNum() {
	return m_syncNum;
}

void CachedStorage::setSyncNum(int64_t syncNum) {
	m_syncNum = syncNum;
}

void CachedStorage::setMaxStoreKey(size_t maxStoreKey) {
	m_maxStoreKey = maxStoreKey;
}

size_t CachedStorage::ID() {
	return m_ID;
}

void CachedStorage::clearCache() {
	if(m_mru.size() > m_maxStoreKey) {
		for(auto it = m_mru.begin(); it != m_mru.end(); ++it) {
			auto tableIt = m_caches.find(it->first);
			if(tableIt != m_caches.end()) {
				auto cache = tableIt->second->findCache(it->second);
				if(cache) {
					if(cache->num() <= m_syncNum) {
						tableIt->second->removeCache(it->second);
						it = m_mru.erase(it);
					}
				}
				else {
					it = m_mru.erase(it);
				}
			}
			else {
				it = m_mru.erase(it);
			}

			if(m_mru.size() <= m_maxStoreKey) {
				break;
			}
		}
	}
}
