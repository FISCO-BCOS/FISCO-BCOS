#pragma once
#include <myAPI.h>
#include <string>
#include <iostream>
#include <memory>
#include <vector>
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <leveldb/cache.h>


namespace leveldb{


	enum DBUseType{
		stateType = 1,			//statedb
		blockType = 2,			//blockdb
		extrasType = 3			//extrasdb
	};

	enum DBEngineType{
		mysql = 1,				//mysql
		oracle = 2				//oracle
	};

	//static string DecodeValue(void* v) { return reinterpret_cast<char *>(v); }
	class OdbcWriteStruct{
	public:
		OdbcWriteStruct(leveldb::Slice const &sKey, leveldb::Slice const & sValue)
			:_sKey(sKey),
			_sValue(sValue)
		{}
	public:
		leveldb::Slice _sKey;
		leveldb::Slice _sValue;
	};

	class LvlDbInterface : public leveldb::DB
	{
	public:
		friend class leveldb::DB;

		LvlDbInterface(const std::string &sDbConnInfo, const std::string &sDbName, const std::string &sTableName, const std::string &sUserName, const std::string &sPwd, int iDBEngineType, int iCacheSize = 100 * 1048576)
			:_sDbConnInfo(sDbConnInfo),
			_sDbName(sDbName),
			_sTableName(sTableName),
			_sUserName(sUserName),
			_sPwd(sPwd)
		{
			std::cout << "LvlDbInterface init:" << _sDbConnInfo << "|" << _sDbName << "|" << _sTableName << "|" << _sUserName << "|" << _sPwd << "|" << iDBEngineType << "|" << iCacheSize << std::endl;;
			//cnote << _sDbConnInfo << "|" << _sDbName << "|" << _sTableName << "|" << _sUserName << "|" << _sPwd;
			
			switch (iDBEngineType)
			{
			case DBEngineType::mysql:
			{
				_saClient = SA_MySQL_Client;
			}
				break;
			case DBEngineType::oracle:
			{
				 _saClient = SA_Oracle_Client;
			}
				break;
			}

			if (!Connect())
			{
				//cwarn << "db connect failed.";
				exit(-1);
			}
			_dataCc = leveldb::NewLRUCache(iCacheSize);//100m = 100 * 1048576
			
		}

		virtual bool Connect()
		{
			bool bRet = true;
			try
			{
				SAString saDbInfo(_sDbConnInfo.c_str());
				SAString saDbUser(_sUserName.c_str());
				SAString saDbPwd(_sPwd.c_str());

				con.Connect(saDbInfo, saDbUser, saDbPwd, _saClient);
							
				con.setOption(SAString("MYSQL_OPT_READ_TIMEOUT")) = SAString("1");
				con.setOption(SAString("MYSQL_OPT_CONNECT_TIMEOUT")) = SAString("86400");
				con.setAutoCommit(SA_AutoCommitOn);
//				std::cout << "conn GetServerVersionString() :" << con.ServerVersionString().GetMultiByteChars() << std::endl;
			}
			catch (SAException &x){
				std::cerr << "SAException: " << x.ErrText().GetMultiByteChars() << std::endl;
				bRet = false;
			}
			catch (std::exception& e){
				std::cerr << "exception: " << e.what() << std::endl;
				bRet = false;
			}
			catch (...){
				std::cerr << "unknown exception occured" << std::endl;
				bRet = false;
			}
			return bRet;
		}

		virtual Status Delete(const WriteOptions&, const Slice& key) override = 0;
		virtual Status Get(const ReadOptions& options, const Slice& key, std::string* value) override = 0;
		virtual Status Write(const WriteOptions& options, WriteBatch* updates)override = 0;


		virtual Status Put(const WriteOptions&, const Slice& key, const Slice& value) = 0;

		//����Ľӿ���ʱ�Ȳ���Ҫʵ�� �ȼ���־ ����Щ��Ҫ
		virtual Iterator* NewIterator(const ReadOptions&) override{
			std::cout << "weodbc  interface neet NewIterator ";
			//cnote << "weodbc  interface neet NewIterator ";
			return nullptr;
			//return leveldb::DB::NewIterator(ro);
		}
		virtual const Snapshot* GetSnapshot() override{
			std::cout << "weodbc  interface neet GetSnapshot ";
			//cnote << "weodbc  interface neet GetSnapshot ";
			return nullptr;
		}
		virtual void ReleaseSnapshot(const Snapshot*) override{
			std::cout << "weodbc  interface neet ReleaseSnapshot.";
			//cnote << "weodbc  interface neet ReleaseSnapshot.";
		}

		virtual bool GetProperty(const Slice&, std::string* value) override{
			std::cout << "weodbc  interface neet GetProperty:" << *value;
			//cnote << "weodbc  interface neet GetProperty:" << *value;
			return true;
		}

		virtual void GetApproximateSizes(const Range*, int n, uint64_t* sizes)override{
			std::cout << "weodbc  interface neet GetApproximateSizes." << n << "|" << sizes;
			//cnote << "weodbc  interface neet GetApproximateSizes." << n << "|" << sizes;
		}

		virtual void CompactRange(const Slice* begin, const Slice* end)override{
			std::cout << "weodbc  interface neet put " << begin->ToString() << "|" << end->ToString();
			//cnote << "weodbc  interface neet put " << begin->ToString() << "|" << end->ToString();
		}


		LvlDbInterface(){}
		~LvlDbInterface(){ delete(_dataCc); _dataCc = nullptr; }


	public:
		//sa����
		SAConnection con;
		SAClient_t _saClient;
		std::string _sDbConnInfo;
		std::string _sDbName;
		std::string _sTableName;
		std::string _sUserName;
		std::string _sPwd;

		leveldb::Cache *_dataCc = nullptr;
	};

	class LvlDbInterfaceFactory{
	public:
		//static std::shared_ptr<LvlDbInterface> create(const std::string &sDbInfo, const std::string &sDbName, const std::string &sTableName, const std::string &username, const std::string &pwd);
		static LvlDbInterface* create(const std::string &sDbInfo, const std::string &sDbName, const std::string &sTableName, const std::string &username, const std::string &pwd, int dbEngineType, int iCacheSize = 100 * 1048576);
		static LvlDbInterface* create(int dbType);
	};

}


