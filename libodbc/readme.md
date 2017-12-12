#使用sqlapi++库 库的源码在(doc\分布式存储\SQLAPI)
1、需要使用libmysqlclientl5.6 （libmysqlclient5.7的无法兼容）
2、现在的命名空间还是为leveldb，因为在overlaydb中使用了很多leveldb中的类
3、基类为LevelDB::DB。 然后实现的基类是LvlDbInterface。
4、各自类实现对应的LvlDbInterface接口。例 LMysql.h
	需实现接口:
		virtual Status Delete(const WriteOptions&, const Slice& key) override;
		virtual Status Write(const WriteOptions& options, WriteBatch* updates) override;
		virtual Status Get(const ReadOptions& options, const Slice& key, std::string* value) override;
		virtual Status Put(const WriteOptions& opt, const Slice& key, const Slice& value) override;
	在tiedb中 有blockdb的iterate方法。 暂时未明白在哪使用，可能需要考虑是否在子类中实现这个接口
	
5、在原eth中引用是根据libdevcore/db.h中来将ldb进行替换的
6、现在是将数据进行hex然后进行存储，取出时再转 fromHex
7、statedb是在overlaydb.h中实例化，extra和blockdb是在trieDb中实例化的
8、122装的mysql是 用户名是root 密码是zldev@2016
9、使用leveldb的LRUCahce
10、现在分三块数据存储在三个不同的表中，使用三个连接。
11、ODBC编译开关选项在cmake/EthOptions.cmake中

todo:
1、写入和插入是用的字符串替换。后续考虑改为使用变量替换
2、可能有部分异常未考虑。须认真测试。
3、各CmakeList中使用
	include_directories(../libodbc/include
		../libodbc/include/db2_linux
		../libodbc/include/ibase
		../libodbc/include/infomix
		../libodbc/include/mysql
		../libodbc/include/odbc_linux
		../libodbc/include/pgsql
		../libodbc/include/sqlbase
		../libodbc/include/sqllite
		../libodbc/include/ss_linux
		../libodbc/include/sybase)
4、考虑接入KC，现在db的配置文件在config.json中。密码是明文存储，可以考虑使用kc做管理
5、oracle只实现、未测试
		
		
------------------------------------------------------
一些相关文档在“doc/分布式存储”下
SQLAPI的源码在“doc\分布式存储\SQLAPI” 下 （中已包含搭好的.a文件 在doc\分布式存储\SQLAPI\lib下）

------------------------------------------------------
config.json配置描述：
{
...
	"dbconf":{
        "stateDbConf":{			//用于存储state状态的db。
			"dbInfo"	:	"<ip:port>@db_bc_test",	//db连接信息 ip:port@DBName
            "dbName"	:	"db_bc_test",						//db名字
            "tableName"	:	"t_bc_tb_state",					//使用表名字
            "userName"	:	"root",								//连接使用的用户名
            "pwd"		:	"zldev@2016",						//连接使用的密码
            "cacheSize"	:	104857600,							//cache的内存大小
			"engineType":	1 									//1-mysql 2-oracle
        },
        "blockDbConf":{			//用于存储block的状态。
            "dbInfo"	: 	"<ip:port>@db_bc_test",		
            "dbName"	: 	"db_bc_test",
            "tableName"	: 	"t_bc_tb_block",
            "userName"	: 	"root",
            "pwd"		:	"zldev@2016",
            "cacheSize"	:	104857600,
			"engineType":	1
        },
        "extrasDbConf":{		//用于存储extras的状态。
            "dbInfo"	: 	"<ip:port>@db_bc_test",
            "dbName"	: 	"db_bc_test",
            "tableName"	: 	"t_bc_tb_extras",
            "userName"	: 	"root",
            "pwd"		:	"zldev@2016",
            "cacheSize"	:	104857600,
			"engineType":	1
        }
    }
}
