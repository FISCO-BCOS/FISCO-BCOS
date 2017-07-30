#include "LMysql.h"

#include <unistd.h>
#include <iostream>
#include <libdevcore/db.h>
#include <libdevcore/Common.h>
#include <libdevcore/CommonData.h>

using namespace std;
using namespace leveldb;
using namespace dev;

static void* EncodeValue(char* v) { return reinterpret_cast<void*>(v); }

static void Deleter(const Slice&, void* v) {

	char * pch = reinterpret_cast<char *>(v);
	delete pch;
//	std::cout << "delete cache " << toHex(dev::bytesConstRef(key)) << std::endl;
}

class MysqlWriterBatch : public leveldb::WriteBatch::Handler
{
public:

	virtual void Put(leveldb::Slice const& _key, leveldb::Slice const& _value)
	{
		leveldb::OdbcWriteStruct odbcWrite(_key, _value);
		//std::cout << "LMYSQL write "  << _key.ToString() << "|" << _value.ToString() << std::endl;
		//std::cout << "LMYSQL write hex " << odbcWrite._sKey << "|" << odbcWrite._sValue << std::endl;

		_vData.push_back(odbcWrite);
	}

	virtual void Delete(leveldb::Slice const& _key)
	{
		std::cout << "Delete" << _key.ToString();
		//cnote << "Delete" << _key.ToString();
	}

	virtual void setCache(Cache *cc){
		if (cc == nullptr)
		{
			return;
		}
		
		for (auto data : _vData)
		{
			std::string sHex = toHex(dev::bytesConstRef(data._sValue));
			char *cstr = new char[sHex.size() + 1];
			strcpy(cstr, sHex.c_str());
			cc->Release(cc->Insert(data._sKey, EncodeValue(cstr), sHex.size(), &Deleter));
		}
	}

	std::string toWriteSqlString(std::string tableName)
	{
		if (_vData.size() == 0)
		{
			return "";
		}

		std::string retSql = "replace into " + tableName + " ( s_key,s_value) values ";
		int i = 0;
		for (auto data : _vData)
		{
			if (i > 0)
			{
				retSql += ",";
			}

			retSql += " ('" + toHex(dev::bytesConstRef(data._sKey)) + "','" + toHex(dev::bytesConstRef(data._sValue)) + "')";
			i++;
		}
		
		retSql += ";";
	    //std::cout << "write sql is " << retSql <<std::endl;
		//LOG(TRACE) << "write sql is " << retSql;
		return retSql;
	}

private:
	std::vector<leveldb::OdbcWriteStruct> _vData;
};

LMysql::LMysql(const std::string &sDbConnInfo, const std::string &sDbName, const std::string &sTableName, const std::string &sUserName, const std::string &sPwd, int iCacheSize)
:LvlDbInterface(sDbConnInfo, sDbName, sTableName, sUserName, sPwd, DBEngineType::mysql, iCacheSize)
{
	std::cout << "MYsql init success.";
	//LOG(TRACE) << "MYsql init success.";
}

Status LMysql::Delete(const WriteOptions& , const Slice& key)
{
	std::cout << "mysql delete " << key.ToString();
	//LOG(TRACE) << "mysql delete " << key.ToString();

	Status s;
	_dataCc->Erase(key);
	return s;
}

Status LMysql::Write(const WriteOptions& , WriteBatch* batch)
{
	leveldb::Status s = Status::Corruption("batch write");
	bool bSucc = false;
	int iTryTimes = 0;
	do
	{
		try
		{
			MysqlWriterBatch n;
			batch->Iterate(&n);

			std::string sql = n.toWriteSqlString(_sTableName);
			if (sql == "")
			{
				//std::cout << "there is no data in write batch." << std::endl;
				return Status::OK();
			}

			SACommand cmd(&con);
			SAString saSql(sql.c_str());
			cmd.setCommandText(saSql);

			cmd.Execute();
			con.Commit();

			n.setCache(_dataCc);
			s = Status::OK();
			bSucc = true;
		}
		catch (SAException &x)
		{
			//DB异常 sleep 50ms 然后进行重连
			sleep(50);
			// print error message
			LOG(ERROR) << "SAException: " << x.ErrText().GetMultiByteChars() << std::endl;
			// 写操作
			//暂时处理服务器丢失情况， 其余异常暂未处理
			if (x.ErrNativeCode() == CR_SERVER_LOST || x.ErrNativeCode() == CR_SERVER_GONE_ERROR)
			{
				if (con.isConnected())
				{
					con.Disconnect();
				}

				LvlDbInterface::Connect();
				std::cout << "SAException: lostServer  writeData：" << iTryTimes << "|" << x.ErrText().GetMultiByteChars() << "|" << x.ErrNativeCode() << std::endl;
				continue;
			}
			else{
				std::cout << "SAException: other reason  writeData：" << iTryTimes << "|" << x.ErrText().GetMultiByteChars() << "|" << x.ErrNativeCode() << std::endl;

			}
			iTryTimes++;

		}
		catch (std::exception& e){
			//todo 其他异常是否直接退出。需考虑
			LOG(ERROR) << "exception: " << e.what() << std::endl;
			break;
		}
		catch (...){
			//todo 其他异常是否直接退出。需考虑
			LOG(ERROR) << "unknown exception occured" << std::endl;
			break;
		}

	} while (!bSucc && iTryTimes <= 3);
	return s;
}

Status LMysql::Get(const ReadOptions& , const Slice& key, std::string* value)
{

	leveldb::Status s = leveldb::Status::NotFound("get");
	ostringstream ossql;
	try
	{
		Cache::Handle *handle = _dataCc->Lookup(key);
		if (handle == NULL) {

			//用占位符 字符串替换
			bool bBreak=false;
			bool bFind = false;
			int iTryTimes = 0;
			do{
				try{
					ossql << "select s_value from " << _sTableName << " where s_key = '" << toHex(bytesConstRef(key)) << "';";
					SACommand cmd(&con);
					SAString saSql(ossql.str().c_str());
					cmd.setCommandText(saSql);
					cmd.Execute();
					//std::cout << "LMysql DB querysql GET: " << ossql.str() << std::endl;

					while (cmd.FetchNext())
					{
						bFind = true;
						// 最后一个字段
						for (int i = 1; i <= cmd.FieldCount(); ++i)
						{
							*value = cmd[i].asString();
							//	std::cout << (const char*)cmd[i].Name() << "|" << (const char*)cmd[i].asBytes() << "|" << *value << endl;
						}
					}
					bBreak = true;
				}
				catch (SAException &x)
				{
					sleep(100);
					std::cout << "SAException: get data " << ossql.str() << "|" << x.ErrText().GetMultiByteChars() << "|" << x.ErrNativeCode() << std::endl;
					iTryTimes++;
					//可能连接闪断，需要尝试重新连接
					if (x.ErrNativeCode() == CR_SERVER_LOST || x.ErrNativeCode() == CR_SERVER_GONE_ERROR)
					{
						if (con.isConnected())
						{
							con.Disconnect();
						}
						
						LvlDbInterface::Connect();
						std::cout << "SAException: lostServer  getData: " <<iTryTimes << "|" << ossql.str() << "|" << x.ErrText().GetMultiByteChars() << "|" << x.ErrNativeCode()  << std::endl;
						
						continue;
					}
					else{
						
						//其余异常不知这么处理是否OK
						if (con.isConnected())
						{
							con.Disconnect();
						}

						LvlDbInterface::Connect();
						std::cout << "SAException: mysql other reason. getData: " << iTryTimes << "|" << ossql.str() << "|" << x.ErrText().GetMultiByteChars() << "|" << x.ErrNativeCode() << std::endl;

					}
				}
				catch (std::exception& e){
					//todo 其他异常是否直接退出。需考虑
					std::cout << "exception: " << e.what() << std::endl;
					break;
				}
				catch (...){
					//todo 其他异常是否直接退出。需考虑
					std::cout << "unknown exception occured" << std::endl;
					break;
				}
				//std::cout << " LMYSQL GET end :" << bFind << " |" << toHex(bytesConstRef(key)) << "|" << *value << endl;
			} while (!bBreak && iTryTimes <= 3);

			if (bFind)
			{
				char *cstr = new char[value->size() + 1];
				strcpy(cstr, value->c_str());
				//交给楠哥free
				_dataCc->Release(_dataCc->Insert(key, EncodeValue(cstr), value->size(), &Deleter));
				*value = asString(fromHex(*value));
			}
		
			s = Status::OK();
		}
		else{
			//std::cout << "LMYSQL CAHCE HAS DATA " << toHex(dev::bytesConstRef(key)) << std::endl;
			char * ch = reinterpret_cast<char *>(_dataCc->Value(handle));
			*value = ch;
			_dataCc->Release(handle);
			//std::cout << "LMYSQL GET datacache is hex data is : " << toHex(dev::bytesConstRef(key)) << "|" << ch << "|" << value <<"|" << value->size()<< std::endl;
			//std::cout << "LMYSQL GET datacache is " << key.ToString() << "|" << *value << std::endl;
			*value = asString(fromHex(*value));

		}
	}
	catch (SAException &x)
	{
		std::cout << "SAException: get data " << ossql.str() << "|" << x.ErrText().GetMultiByteChars() << "|" << x.ErrNativeCode ()<< std::endl;
	}
	catch (std::exception& e){
		std::cout << "exception: " << e.what() << std::endl;
	}
	catch (...){
		std::cout << "unknown exception occured" << std::endl;
	}

	return s;
}


Status  LMysql::Put(const WriteOptions& opt, const Slice& key, const Slice& value)
{
	leveldb::WriteBatch batch;
	batch.Put(key, value);

	return Write(opt, &batch);
}