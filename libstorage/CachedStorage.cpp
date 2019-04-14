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
#include <thread>

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

void TableCaches::setTableInfo(TableInfo::Ptr tableInfo) {
	m_tableInfo = tableInfo;
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
		//m_caches.erase(it);
		std::lock_guard<std::mutex> lock(m_mutex);
		m_caches.unsafe_erase(it);
	}
}

tbb::concurrent_unordered_map<std::string, Caches::Ptr>* TableCaches::caches() {
	return &m_caches;
}

CachedStorage::CachedStorage() {
	m_taskThreadPool = std::make_shared<dev::ThreadPool>("FlushStorage", 1);
	m_syncNum.store(0);
	m_commitNum.store(0);
}

Entries::Ptr CachedStorage::select(h256 hash, int num, TableInfo::Ptr tableInfo,
        const std::string& key, Condition::Ptr condition) {
	STORAGE_LOG(TRACE) << "Query data from cachedStorage table: " << tableInfo->name << " key: " << key;
	auto out = std::make_shared<Entries>();

	auto entries = selectNoCondition(hash, num, tableInfo, key, condition)->entries();

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

Caches::Ptr CachedStorage::selectNoCondition(h256 hash, int num, TableInfo::Ptr tableInfo,
        const std::string& key, Condition::Ptr condition) {
	(void)condition;

	{
		auto tableIt = m_caches.find(tableInfo->name);
		if(tableIt != m_caches.end()) {
			auto tableCaches = tableIt->second;
			auto caches = tableCaches->findCache(key);
			if(caches) {
				std::lock_guard<std::mutex> lock(m_mutex);
				auto r = m_mru.push_front(std::make_pair(tableInfo->name, key));
				if(!r.second) {
					m_mru.relocate(m_mru.end(), r.first);
				}

				return caches;
			}
		}
	}

	if(m_backend) {
		auto conditionKey = std::make_shared<Condition>();
		conditionKey->EQ(tableInfo->key, key);
		auto backendData = m_backend->select(hash, num, tableInfo, key, conditionKey);

		auto tableIt = m_caches.find(tableInfo->name);
		if(tableIt == m_caches.end()) {
			tableIt = m_caches.insert(std::make_pair(tableInfo->name, std::make_shared<TableCaches>())).first;
			tableIt->second->setTableInfo(tableInfo);
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

#if 0
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
#endif

	size_t total = 0;

	std::vector<TableData::Ptr> commitDatas;
	//update cache & copy datas
	for(auto it: datas) {
		auto tableData = std::make_shared<TableData>();
		tableData->info = it->info;

		for(size_t i=0; i<it->entries->size(); ++i) {
			++total;

			auto entry = it->entries->get(i);
			auto key = entry->getField(it->info->key);
			auto id = entry->getID();


			if(id != 0) {
				auto caches = selectNoCondition(h256(), 0, it->info, key, nullptr);
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

				if(entry->force()) {
					auto tableIt = m_caches.find(it->info->name);
					if(tableIt == m_caches.end()) {
						tableIt = m_caches.insert(std::make_pair(it->info->name, std::make_shared<TableCaches>())).first;
						tableIt->second->tableInfo()->name = it->info->name;
					}

					auto caches = std::make_shared<Caches>();
					auto newEntries = std::make_shared<Entries>();
					newEntries->addEntry(cacheEntry);
					caches->setEntries(newEntries);
					caches->setNum(num);

					tableIt->second->addCache(key, caches);

				}
				else {
					auto caches = selectNoCondition(h256(), 0, it->info, key, nullptr);
					caches->entries()->addEntry(cacheEntry);
					caches->setNum(num);
				}

				LOG(TRACE) << "Set new entry ID: " << cacheEntry->getID();

				auto commitEntry = std::make_shared<Entry>();
				commitEntry->copyFrom(cacheEntry);
				tableData->entries->addEntry(commitEntry);
			}
		}

		commitDatas.push_back(tableData);
	}

#if 0
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
#endif

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

	m_commitNum.store(num);
	m_taskThreadPool->enqueue([backend, task, self]() {
		STORAGE_LOG(INFO) << "Start commit block: " << task->num << " to persist storage";
		backend->commit(task->hash, task->num, task->datas);

		auto storage = self.lock();
		if(storage) {
			STORAGE_LOG(INFO) << "Commit block: " << task->num << " to persist storage finished, current syncd block: " << storage->syncNum();
			storage->setSyncNum(task->num);
		}
	});

	STORAGE_LOG(INFO) << "Submited block task: " << num << ", current syncd block: " << m_syncNum;
	checkAndClear();

	return total;
}

bool CachedStorage::onlyDirty() {
	return true;
}

void CachedStorage::setBackend(Storage::Ptr backend) {
	m_backend = backend;
}

void CachedStorage::init() {
	auto tableInfo = std::make_shared<storage::TableInfo>();
	tableInfo->name = SYS_CURRENT_STATE;
	tableInfo->key = SYS_KEY;
	tableInfo->fields = std::vector<std::string>{"value"};

	//get id from backend
	auto out = m_backend->select(h256(), 0, tableInfo, SYS_KEY_CURRENT_ID, nullptr);
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
	m_syncNum.store(syncNum);
}

void CachedStorage::setMaxStoreKey(size_t maxStoreKey) {
	m_maxStoreKey = maxStoreKey;
}

size_t CachedStorage::ID() {
	return m_ID;
}

void CachedStorage::checkAndClear() {
	bool needClear = false;
	size_t clearTimes = 0;

	do {
		needClear = false;

		if(clearTimes > 1) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		if(m_mru.size() > m_maxStoreKey) {
			STORAGE_LOG(INFO) << "Total keys: " << m_mru.size() << " greater than maxStoreKey: " << m_maxStoreKey << ", waiting...";
			needClear = true;
		}
		else if((size_t)(m_commitNum - m_syncNum) > m_maxForwardBlock) {
			STORAGE_LOG(INFO) << "Current block number: " << m_commitNum << " greater than syncd block number: " << m_syncNum << ", waiting...";
			needClear = true;
		}

		if(needClear) {
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

		++clearTimes;
	} while(needClear);
}
