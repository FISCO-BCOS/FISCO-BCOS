# AMDB使用指南

**使用说明：** 使用本文档之前，需要先利用FISCO-BCOS1.5搭链([FISCO BCOS使用说明书](https://github.com/FISCO-BCOS/FISCO-BCOS/blob/dev-1.5/doc/manual))，节点运行正常后开始使用AMDB。

## 介绍
AMDB是FISCO BCOS的数据代理服务，可以将智能合约对数据的读写操作转换为对数据库的读写操作。AMDB支持两种数据代理配置方式，即 amop 和 leveldb 方式。

## **1 节点配置**

### **1.1 节点的状态数据库配置**
FISCO-BCOS1.5版本中的节点配置文件为config.conf，其中[statedb]部分为AMDB的状态数据库配置，有两种配置方式。

* type配置为amop，可以连接mysql数据库。其中topic配置与AMDB的配置文件相一致，具体配置如下：
```ini
[statedb]
        ; statedb的类型默认为amop
        type=amop 
        ; amopdb关注的topic默认为DB
        topic=DB 
        ; amopdb的最大重试次数 超出次数后程序退推出默认为0(无限)
        maxRetry=0 
```
* type配置为leveldb，可以连接leveldb数据库，具体配置如下:
```ini
[statedb] 
        ; statedb的类型配置为leveldb
        type=leveldb
        ; leveldb存储数据的路径
        path=data/statedb
```
**注意：** 节点的配置文件是ini格式，如果有注释，注释（以分号开始）需要单独一行，不能放在配置项同一行，否则节点无法启动。

### **1.2 节点的监听端口配置**
节点配置文件[rpc]部分的listen_port是AMDB配置文件中使用的区块链节点的监听端口。
```ini
[rpc]
    listen_ip=0.0.0.0
    listen_port=30301
    http_listen_port=30302
    console_port=30303
```

### **1.3 mysql数据库配置**
节点配置为amop方式时，需要安装mysql数据库，在Ubuntu和CentOS服务器上安装方式如下。

Ubuntu：执行下面三条命令，安装过程中，配置root账户密码。
```bash
sudo apt-get install mysql-server
sudo apt install mysql-client
sudo apt install libmysqlclient-dev
```
CentOS: 执行下面三条命令，安装过程中，配置root账户密码。
```bash
wget https://dev.mysql.com/get/mysql57-community-release-el7-9.noarch.rpm
rpm -ivh mysql57-community-release-el7-9.noarch.rpm
yum install mysql-server
```
启动msyql服务并登陆:
```bash
sudo service msyql start
mysql -uroot -p
```
## **2.AMDB源码下载与编译**
源码地址：
```bash
https://github.com/FISCO-BCOS/AMDB.git
```
下载后，进入AMDB根目录，切换到dev分支并编译：
```bash
cd AMDB
git checkout dev
gradle build
```
编译成功后根目录下会生成dist目录，dist目录可以放在服务器任何位置运行。

## **3.AMDB配置**

### **3.1 AMDB配置节点证书**
《FISCO BCOS区块链操作手册》2.3节中会生成sdk证书目录，将sdk目录下所有文件（包括ca.crt和client.keystore）拷贝到dist/conf目录下。其中：
* ca.crt: 链证书
* client.keystore：web3sdk的SSL证书

### **3.2 AMDB配置文件设置**
AMDB的dist/conf目录下有配置文件applicationContext-sample.xml，重命名为applicationContext.xml，然后修改该文件配置的区块链节点ip、端口和topic，需要与连接的节点配置文件相一致。
```xml
<?xml version="1.0" encoding="UTF-8"?>
<beans xmlns="http://www.springframework.org/schema/beans"
	xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:p="http://www.springframework.org/schema/p"
	xmlns:cache="http://www.springframework.org/schema/cache"
	xmlns:aop="http://www.springframework.org/schema/aop" xmlns:context="http://www.springframework.org/schema/context"
	xmlns:jdbc="http://www.springframework.org/schema/jdbc" xmlns:task="http://www.springframework.org/schema/task"
	xsi:schemaLocation="http://www.springframework.org/schema/beans
	http://www.springframework.org/schema/beans/spring-beans.xsd
	http://www.springframework.org/schema/context
	http://www.springframework.org/schema/context/spring-context.xsd
	http://www.springframework.org/schema/jdbc
    http://www.springframework.org/schema/jdbc/spring-jdbc-3.0.xsd
    http://www.springframework.org/schema/cache
    http://www.springframework.org/schema/cache/spring-cache.xsd">

	<context:annotation-config />
	<context:component-scan base-package="org.bcos" />
	
	<!-- mybatis配置，固定 -->
	<bean class="org.springframework.beans.factory.config.PropertyPlaceholderConfigurer">
		<property name="locations" value="classpath:db.properties" />
	</bean>

	<!-- mybatis配置，固定 -->
	<bean class="org.mybatis.spring.mapper.MapperScannerConfigurer">
		<property name="basePackage" value="org.bcos.amdb.dao" />
	</bean>
	
	<!-- mybatis配置，固定 -->
	<bean id="sqlSessionFactory" class="org.mybatis.spring.SqlSessionFactoryBean">
		<property name="dataSource" ref="dataSource" />
		<property name="mapperLocations" value="classpath:/mappers/*.xml" />
	</bean>
	
	<!-- AMOP配置，用于与节点通讯 -->
	<bean id="DBChannelService" class="org.bcos.channel.client.Service">
		<property name="connectSeconds" value="3600" /> <!-- 启动前等待连接节点的时间 -->
		<property name="orgID" value="WB" />
		<property name="allChannelConnections">
			<map>
				<entry key="WB">
					<bean class="org.bcos.channel.handler.ChannelConnections">
						<property name="connectionsStr">
							<list>
								<value>WB@127.0.0.1:30301</value> <!-- 区块链节点的ip端口配置 -->
							</list>
						</property>
					</bean>
				</entry>
			</map>
		</property>
		<!-- 区块链topic设置，需要与节点的通讯topic配置一致 -->
		<property name="topics">
			<list>
				<value>DB</value>
			</list>
		</property>
		<property name="pushCallback" ref="DBHandler"/>
	</bean>
	
	<bean id="DBService" class="org.bcos.amdb.service.DBService">
	</bean>

	<!-- 数据库连接信息配置 -->
	 <bean id="dataSource" class="org.apache.commons.dbcp2.BasicDataSource">
		<property name="driverClassName" value="com.mysql.jdbc.Driver" />
		<property name="url" value="jdbc:mysql://${db.ip}:${db.port}/${db.database}?characterEncoding=UTF-8&amp;zeroDateTimeBehavior=convertToNull" />
		<property name="username" value="${db.user}" />
		<property name="password" value="${db.password}" />
	</bean>
</beans>  
```

### **3.3 数据库配置文件设置**
如果AMDB连接mysql数据库，则需要配置db.properties，具体配置内容为数据库服务所在的ip、端口、用户名、密码以及要连接的数据库名。
```ini
db.ip=127.0.0.1
db.port=3306
db.user=root
db.password=123456
db.database=bcos
```
**注意：**
* 每个节点都需要配置⼀个数据库，可以在同⼀台数据库服务器中创建多个数据库进行测试，每个节点分别连接其中的一个数据库。同时每个节点也需要配置各自的AMDB服务，并分别启动AMDB服务（可以将AMDB编译后的dist目录放在服务器的多个位置，更改相应配置，分别启动运行）。

* 如果AMDB配置连接的是leveldb，由于leveldb是内置数据库，因此不需要配置数据库以及db.properties文件。

## **4 AMDB启动**
如果节点配置为amop方式，要首先确保mysql启动成功，相关的数据库已建立好。AMDB配置完毕后，在dist目录下，运行如下命令即可启动AMDB服务：
```bash
java -cp "conf/:apps/*:lib/*" org.bcos.amdb.server.Main
```
AMDB启动之后，会在配置的数据库中创建系统表_sys_tables_和_sys_miners_，其中_sys_tables_表保存所有的建表字段信息，用于创建其他表；_sys_miners_表用于保存共识节点的信息。

## **5 AMDB使用**

### **5.1 智能合约开发**
访问AMDB需要使用AMDB专用的智能合约DB.sol接口，该接口是数据库合约，用来对表进行增删改查。

DB.sol文件代码如下:
```js
contract DBFactory {
    function openDB(string) public constant returns (DB);
    function createTable(string,string,string) public returns(DB);
}

//查询条件
contract Condition {
    function EQ(string, int);
    function EQ(string, string);
    
    function NE(string, int);
    function NE(string, string);
    
    function GT(string, int);
    function GE(string, int);
    
    function LT(string, int);
    function LE(string, int);
    
    function limit(int);
    function limit(int, int);
}

//单条数据记录
contract Entry {
    function getInt(string) public constant returns(int);
    function getAddress(string) public constant returns(address);
    function getBytes64(string) public constant returns(byte[64]);

    
    function set(string, int) public;
    function set(string, string) public;
}

//数据记录集
contract Entries {
    function get(int) public constant returns(Entry);
    function size() public constant returns(int);
}

//DB主类
contract DB {

    function select(string, Condition) public constant returns(Entries);
    function insert(string, Entry) public returns(int);
    function update(string, Entry, Condition) public returns(int);
    function remove(string, Condition) public returns(int);
    
    function newEntry() public constant returns(Entry);
    function newCondition() public constant returns(Condition);
}
```
提供一个合约案例DBTest.sol，代码如下：
``` js
import "DB.sol";

contract DBTest {
    event readResult(bytes32 name, int item_id, bytes32 item_name);
    event insertResult(int count);
    event updateResult(int count);
    event removeResult(int count);
    //创建表
    function create() public {
        DBFactory df = DBFactory(x1001);
        df.createTable("t_test", "name", "item_id,item_name");
    }
    //查数据
    function read(string name) public constant returns(bytes32[], int[], bytes32[]){
        DBFactory df = DBFactory(x1001);
        DB db = df.openDB("t_test");
        
        Condition condition = db.newCondition();
        //condition.EQ("name", name);
        
        Entries entries = db.select(name, condition);
        bytes32[] memory user_name_bytes_list = new bytes32[](uint256(entries.size()));
        int[] memory item_id_list = new int[](uint256(entries.size()));
        bytes32[] memory item_name_bytes_list = new bytes32[](uint256(entries.size()));
        
        for(int i=0; i<entries.size(); ++i) {
            Entry entry = entries.get(i);
            
            user_name_bytes_list[uint256(i)] = entry.getString("name").toBytes32();
            item_id_list[uint256(i)] = entry.getInt("item_id");
            item_name_bytes_list[uint256(i)] = entry.getString("item_name").toBytes32();
        }
        
        return (user_name_bytes_list, item_id_list, item_name_bytes_list);
    }
    //插入数据
    function insert(string name, int item_id, string item_name) public returns(int) {
        DBFactory df = DBFactory(x1001);
        DB db = df.openDB("t_test");
        
        Entry entry = db.newEntry();
        entry.set("name", name);
        entry.set("item_id", item_id);
        entry.set("item_name", item_name);
        
        int count = db.insert(name, entry);
        insertResult(count);
        
        return count;
    }
    //更新数据
    function update(string name, int item_id, string item_name) public returns(int) {
        DBFactory df = DBFactory(x1001);
        DB db = df.openDB("t_test");
        
        Entry entry = db.newEntry();
        entry.set("item_name", item_name);
        
        Condition condition = db.newCondition();
        condition.EQ("name", name);
        condition.EQ("item_id", item_id);
        
        int count = db.update(name, entry, condition);
        updateResult(count);
        
        return count;
    }
    //删除数据
    function remove(string name, int item_id) public returns(int){
        DBFactory df = DBFactory(x1001);
        DB db = df.openDB("t_test");
        
        Condition condition = db.newCondition();
        condition.EQ("name", name);
        condition.EQ("item_id", item_id);
        
        int count = db.remove(name, condition);
        removeResult(count);
        
        return count;
}
}
```
DBTest.sol调用了AMDB专用的智能合约DB.sol，实现的是创建用户表t_test，并对t_test表进行增删改查的功能。

**注意：** 
* 客户端需要调用转换为Java文件的合约代码，需要将DBTest.sol和DB.sol放入web3sdk的contract目录下，通过web3sdk的compile.sh脚本生成DBTest.java。web3sdk的配置和使用请参考[web3sdk使用指南](https://github.com/FISCO-BCOS/web3sdk/tree/web3sdk-doc)。


### **5.2 客户端调用**
客户端通过web3sdk调用合约向FISCO-BCOS发交易（web3sdk的使用请参考[web3sdk使用指南](https://github.com/FISCO-BCOS/web3sdk/tree/web3sdk-doc)）便可以通过AMDB操作数据库。

