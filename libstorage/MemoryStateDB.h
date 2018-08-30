#pragma once

#include "StateDB.h"
#include "StateStorage.h"

namespace dev {

namespace storage {

class MemoryStateDB: public StateDB {
public:
	typedef std::shared_ptr<MemoryStateDB> Ptr;

	virtual ~MemoryStateDB() {};

	virtual void init(const std::string &tableName);
	virtual Entries::Ptr select(const std::string &key, Condition::Ptr condition) override;
	virtual size_t update(const std::string &key, Entry::Ptr entry, Condition::Ptr condition) override;
	virtual size_t insert(const std::string &key, Entry::Ptr entry) override;
	virtual size_t remove(const std::string &key, Condition::Ptr condition) override;

	virtual h256 hash();
	virtual void clear();
	virtual std::map<std::string, Entries::Ptr>* data() override;

	void setStateStorage(StateStorage::Ptr amopDB);

	void setBlockHash(h256 blockHash);
	void setBlockNum(int blockNum);

private:
	Entries::Ptr processEntries(Entries::Ptr entries, Condition::Ptr condition);
	bool processCondition(Entry::Ptr entry, Condition::Ptr condition);

	StateStorage::Ptr _remoteDB;
	TableInfo::Ptr _tableInfo;
	std::map<std::string, Entries::Ptr> _cache;

	h256 _blockHash;
	int _blockNum = 0;
};

}

}
