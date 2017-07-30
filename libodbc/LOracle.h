#pragma once
#include "LvlDbInterface.h"
using namespace leveldb;

namespace leveldb{
	class LOracle : public LvlDbInterface
	{
	public:
		LOracle(const std::string &sDbConnInfo, const std::string &sDbName, const std::string &sTableName, const std::string &sUserName, const std::string &sPwd, int iCacheSize = 100 * 1048576);

		virtual Status Delete(const WriteOptions&, const Slice& key) override;
		virtual Status Write(const WriteOptions& options, WriteBatch* updates) override;
		virtual Status Get(const ReadOptions& options, const Slice& key, std::string* value) override;
		virtual Status Put(const WriteOptions& opt, const Slice& key, const Slice& value) override;

		LOracle(){}
		~LOracle(){}
	};

}