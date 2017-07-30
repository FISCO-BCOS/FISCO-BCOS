# 前言

本文档是BCOS区块链开源平台的一部分。
BCOS区块链开源平台由深圳前海微众银行股份有限公司、万向区块链实验室和矩阵元Juzix共同发起。
本文档面向读者是了解区块链基本概念的区块链技术开发人员、测试人员、产品经理、区块链爱好者、金融行业从业者等等。

为了让大家更好的了解BCOS区块链开源平台的使用方法。本文档按照Step By Step的步骤详细介绍了BCOS区块链的构建、安装、启动，智能合约部署、调用等初阶用法，还包括多节点组网、系统合约等高阶内容的介绍。

本文档不介绍BCOS区块链设计理念及思路，详情请参看白皮书。

# 第一章、搭建BCOS区块链环境 
本章主要介绍运行BCOS区块链执行程序所需要的软硬件环境、以及从源码编译的环境准备和编译步骤。

## 1.1硬件环境

| 配置	| 最低配置	| 推荐配置 | 
|-------- |----------|---------|
|CPU	|1.5GHz	|2.4GHz|
|内存	  |512MB	|2GB|
|核心	|1核	|2核|
|带宽	|512Kb	|1Mb|
|操作系统|		|Ubuntu（16.04  64位）/CentOS （7.2  64位）|


以下代码及操作命令以在Centos操作系统上为示例。

## 1.2软件环境

①　安装nodejs(安装后版本nodejs版本大于6）、babel-node环境
```bash
sudo yum install nodejs 
sudo npm config set registry https://registry.npm.taobao.org
sudo npm install -g babel-cli babel-preset-es2017
echo '{ "presets": ["es2017"] }' > ~/.babelrc
```

②　安装智能合约solidity编译器
方式一：直接下载solc二进制执行程序
```bash
wget https://github.com/ethereum/solidity/releases/download/v0.4.11/solc-static-linux
sudo cp solc-static-linux  /usr/bin/solc
sudo chmod +x /usr/bin/solc
```

方式二：
从 [https://github.com/ethereum/solidity/releases](https://github.com/ethereum/solidity/releases) 上获取安装包自行安装

③　安装控制台
```bash
sudo yum install git
sudo npm install -g ethereum-console
```

## 1.3源码编译

### 1.3.1获取源代码

```bash
git clone https://github.com/bcosorg/bcos.git
```

### 1.3.2目录结构说明

|目录|	说明|
|---|---|
|eth |	主入口目录，其中main.cpp包含main函数入口
|libdevcore 	|基础通用组件实现目录，如工具类函数、基础数据类型结构定义、IO操作函数、读写锁、内存DB、TrieDB、SHA3实现、RLP编解码实现、Worker模型等等
|libethcore	|区块链核心数据结构目录。如ABI、秘钥管理、区块头、预编译、交易结构等等
|libethereum	|区块链主框架逻辑目录。如交易池、系统合约、节点管理、块链、块、链参数等等
|libevm	|虚拟机主目录。如解释器、JIT等等
|libevmcore	|OPCODE指令集定义、定价
|libp2p	|区块链P2P网络主目录。如握手、网络包编解码、会话管理等等
|libpbftseal|	PBFT共识插件实现目录
|libraftseal|	RAFT共识插件实现目录
|libweb3jsonrpc|	web3 RPC实现目录
|systemproxyv2	|系统合约实现目录
|tool	|工具脚本目录

### 1.3.3编译前准备

①　检查cmake 版本（cmake3 --version 必须>3.4.3）
如果版本不满足要求，需要升级。
方式一：从源升级
```bash
sudo yum -y install cmake3
```
方式二：到官网https://cmake.org/download/ 下载最新版本安装。

②　安装依赖的开发库

```bash
sudo yum install openssl openssl-devel
chmod +x scripts/install_deps.sh
./scripts/install_deps.sh
```

#### 1.3.4编译可执行程序
```bash
mkdir -p build
cd build/
cmake -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF .. #注意命令末尾的..
make -j2
```

编译成功，eth/eth 即是区块链可执行程序。

# 第二章、启动创世节点

区块链节点的启动有两个必选参数。一个是创世块文件，另一个是运行参数配置文件。

其中创世块文件包含内置管理员账号，需要启动前提前生成。
其中运行参数配置文件包含节点身份NodeId,需要启动前提前生成。

本章开始先介绍程序启动所需的创世块文件、配置文件、日志配置文件的字段含义。接着介绍NodeId的生成方法和管理员外部账号公私钥对的生成方法，及创世块文件、配置文件、日志配置文件的样例。
最后介绍启动创世节点的命令方法。

## 2.1．创世块文件genesis.json说明
创世块文件是对区块链第一个块的描述。是唯一的，不可变更的。
它是节点启动的一个必选参数。


源码目录下有示例创世块文件genesis.json，可以根据需要进行修改，关键字段说明如下：

|配置项	|说明|
|----|----|
|timestamp|	创世块时间戳(毫秒)|
|god|	内置链管理员账号地址（生成方法可参看生成公私钥对）|
|alloc	|内置合约数据|
|initMinerNodes	|创世块节点NodeId（生成方法可参看生成NodeId）|

在 多节点组网 章节之前，只需要变更god、initMinerNodes字段，其他保持默认即可。 

## 2.2．运行参数配置文件config.json说明

配置文件是对区块链运行时参数的设置。配置值可变更，重启时生效。
它是节点启动的一个必选参数。

源码目录下有示例创世块文件config.json，可以根据需要进行修改，关键字段说明如下：

|配置项	|说明|
|----|----|
|sealEngine	|共识算法（可选PBFT、RAFT、SinglePoint）
|systemproxyaddress	|系统路由合约地址（生成方法可参看部署系统合约）
|listenip	|监听IP（建议内网IP）
|rpcport	|RPC监听端口）（若在同台机器上部署多个节点时，端口不能重复）
|p2pport|	P2P网络监听端口（若在同台机器上部署多个节点时，端口不能重复）
|wallet	|钱包文件路径
|keystoredir|	账号文件目录路径
|datadir|	节点数据目录路径 
|vm|	vm引擎 （默认 interpreter ）
|networkid|	网络ID
|logverbosity|	日志级别（级别越高日志越详细，>8 TRACE日志，4<=x<8 DEBU G日志，<4 INFO日志）
|coverlog	|覆盖率插件开关（ON或OFF）
|eventlog|	合约日志开关（ON或OFF）
|logconf|	日志配置文件路径（日志配置文件可参看日志配置文件说明）
|NodeextraInfo|	节点连接配置列表[{NodeId,Ip,port,nodedesc,agencyinfo,identitytype}]（ 节点ID、外网IP、P2P网络端口、节点描述、节点信息、节点类型）
|dfsNode	|分布式文件服务节点ID ，与节点身份NodeID一致 （可选功能配置参数）
|dfsGroup	|分布式文件服务组ID （10 - 32个字符）（可选功能配置参数）
|dfsStorage|	指定分布式文件系统所使用文件存储目录（可选功能配置参数）

在 多节点组网 章节之前，只需要更新rpcport、p2pport、wallet、keystoredir、datadir、NodeextraInfo字段，其他保持默认即可。

在接下来的说明中，假设节点数据目录（注意当前用户对目录需要有写入权限）路径为/mydata/nodedata-1/。

## 2.3．日志配置文件log.conf说明
日志配置文件是对区块链运行过程日志输出的设置。配置值可变更，重启时生效。

其中log.conf的关键字段说明如下：

|配置项	|说明|
|----|----|
|FORMAT|	日志格式，典型如%level|%datetime{%Y-%M-%d %H:%m:%s}|%msg
|FILENAME	|日志文件输出的文件路径典型如/mydata/nodedata-1/log/log_%datetime{%Y%M%d%H}.log
|MAX_LOG_FILE_SIZE|	最大日志文件大小
|LOG_FLUSH_THRESHOLD	|超过多少条日志即可落盘

详细示例看参看创世块文件、运行参数配置文件、日志配置文件示例

## 2.4．生成节点身份NodeId
节点身份NodeId是一个公钥，代表节点的唯一身份标识。
```
./build/eth/eth --gennetworkrlp  /mydata/nodedata-1/network.rlp
```
将在/mydata/nodedata-1/ 目录下生成两个文件 network.rlp 和 network.rlp.pub。
network.rlp是节点身份的私钥二进制文件。
network.rlp.pub是节点身份的NodeId文件。

读取network.rlp.pub 将其更新到创世块文件genesis.json的initMinerNodes字段及	config.json中的NodeextraInfo字段，同时更新NodeextraInfo中的Nodedesc、	Agencyinfo、Peerip、Port为对应值。（Peerip为外网IP，Port为P2P网络监听端口）
## 2.5．生成管理员公私钥对

管理员公钥代表着对区块链的管理权限,生成的公钥和私钥是一一对应的,私钥代表着对公钥的所有权,私钥应妥善保管。

```bash
cd tool
node accountManager.js
```

将公钥address更新到创世块文件genesis.json的god字段。

## 2.6．创世块文件、运行参数配置文件、日志配置文件示例
创世块文件示例如下：
```json
{
     "nonce": "0x0",
     "difficulty": "0x0",
     "mixhash": "0x0",
     "coinbase": "0x0",
     "timestamp": "0x0",
     "parentHash": "0x0",
     "extraData": "0x0",
     "gasLimit": "0x13880000000000",
     "god":"0x4d23de3297034cdd4a58db35f659a9b61fc7577b",
     "alloc": {}, 	
     "initMinerNodes":["de0fa385816b505799433e54b88788e21cb42092a6ff5bcaa2285d7ace906e5e6ce8ef2b30134ff276a5834d58721291acc5864e07e6d52469b79b28e699dfde"]
}
```

运行参数配置文件示例如下：
```json
{
        "sealEngine": "PBFT",
        "systemproxyaddress":"0x0",
        "listenip":"127.0.0.1",
        "rpcport":      "8545",
        "p2pport":      "30303",
        "wallet":"/mydata/nodedata-1/keys.info",
        "keystoredir":"/mydata/nodedata-1/keystore/",
        "datadir":"/mydata/nodedata-1/",
        "vm":"interpreter",
        "networkid":"12345",
        "logverbosity":"4",
        "coverlog":"OFF",
        "eventlog":"ON",
        "logconf":"/mydata/nodedata-1/log.conf",
        "params": {
                "accountStartNonce": "0x0",
        	  "maximumExtraDataSize": "0x0",
        	 "tieBreakingGas": false,
                "blockReward": "0x0",
                "networkID" : "0x0"
        },
        "NodeextraInfo":[
        {
         					"Nodeid":"de0fa385816b505799433e54b88788e21cb42092a6ff5bcaa2285d7ace906e5e6ce8ef2b30134ff276a5834d58721291acc5864e07e6d52469b79b28e699dfde", 
            "Nodedesc": "node1",
            "Agencyinfo": "node1",
            "Peerip": "127.0.0.1",
            "Identitytype": 1,
            "Port":30303,
            "Idx":0
        }
        ]
}
```

日志配置文件示例如下：
```bash
* GLOBAL:  
    ENABLED                 =   true  
    TO_FILE                 =   true  
    TO_STANDARD_OUTPUT      =   false  
    FORMAT                  =   "%level|%datetime{%Y-%M-%d %H:%m:%s}|%msg"   
    FILENAME                =   "/mydata/nodedata-1/log/log_%datetime{%Y%M%d%H}.log"  
    MILLISECONDS_WIDTH      =   3  
    PERFORMANCE_TRACKING    =   false  
    MAX_LOG_FILE_SIZE       =   209715200 ## 200MB - Comment starts with two hashes (##)
    LOG_FLUSH_THRESHOLD     =   100  ## Flush after every 100 logs
      
* TRACE:  
    FILENAME                =   "/mydata/nodedata-1/log/trace_log_%datetime{%Y%M%d%H}.log"  
      
* DEBUG:  
    FILENAME                =   "/mydata/nodedata-1/log/debug_log_%datetime{%Y%M%d%H}.log"  
    FORMAT                  =   "%level|%datetime|%file|%func|%line|%msg" 

* FATAL:  
    ENABLED                 =   false  
      
* ERROR:  
    FILENAME                =   "/mydata/nodedata-1/log/error_log_%datetime{%Y%M%d%H}.log"  
      
* WARNING: 
     ENABLED                 =   false
 
* INFO:  
    FILENAME                =   "/mydata/nodedata-1/log/info_log_%datetime{%Y%M%d%H}.log"  
      
* VERBOSE:  
    ENABLED                 =   false
    
```
## 2.7．创世节点启动
命令行：区块链执行程序路径 --genesis 创世块文件 --config 配置文件

``` ./build/eth/eth --genesis ./genesis.json --config ./config.json & ```

查看日志是否正常出块 

``` tail -f /mydata/nodedata-1/log/info* | grep Report ```

日志示例如下：

![log example](./images/i2.1.png)

日志中每行blk=563 都在正常增长，即是在正常出块。

至此，一个完整的区块链节点已经跑起来了！
# 第三章、开发说明

## 3.1 日志输出接口使用说明
区块链运行日志文件都位于 日志配置文件说明 config.json所配置的log.conf 所指路径下。
各个级别日志都有各自的日志文件。如TRACE日志写到trace_开头对应的文件中。
其中TRACE日志为全量日志、INFO日志为关键信息、DEBUG为调试信息。
查看区块链主线程日志可执行：
```bash
tail -f /mydata/nodedata-1/log/trace_log* | grep "|eth"
```
输出示例图如下：

![示例输出](./images/i3.2.png)

日志输出的”Imported and best 3721” 代表块3721已经上链。

## 3.2 分布式文件系统使用说明

### 3.2.1 搭建服务节点群

1.参照多节点组网章节，进行多节点组网，并部署系统合约；系统合约部署成功以后将输出系统代理合约（SystemProxy）、FileInfoManager和FileServerManager三个合约地址, 示例输出如下：

![系统合约部署][systemContract]

2.配置config.json文件中的dfsGroup，dfsNode，dfsStorage三个参数，并更新systemproxyaddress参数为上一步得到的系统代理合约地址，具体参数定义请参看参数配置文件章节；在群组和节点的关系定义上，通常一个节点只允许加入一个群组，多个节点如果配置一样的群组ID，节点启动以后将加入到同一个群组当中，反之亦然。

3.重启群组所有节点即可用启动DFS服务。DFS服务启动后，即可以通过JAVA SDK提供的方法进行文件存取操作。

### 3.2.2使用说明
JAVA SDK提供的接口功能包括文件上传，文件删除，文件下载，查询文件服务节点，添加服务节点，删除服务节点等。所提供接口为异步回调节接口，用户需要自定义回调监听处理类。SDK的核心类为DfsEngine,位于仓库的tool/java目录，为eclipse工程，依赖于同级目录的web3j项目。

#### 3.2.2.1接口说明
##### (1). 初始化服务

- 接口名称：init
- 接口功能说明：初始化服务
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|host|String|主机地址|
|port|int|JSONRPC端口|
|listener|IJuListener|监听回调接口|
|walletPasswd|String|加密口令|
|walletFile|String|钱包文件|
|fileContract|String|文件合约地址|
|serverContract|String|文件服务合约地址|
|**输出参数**|**参数说明**|**参数说明**|
|返回值|int|0 ：成功    其他：失败|

##### (2). 添加服务节点（所有启动的节点都需要添加）

- 接口名称：addServer
- 接口功能说明：添加服务节点
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|newServer|FileServer|分布式文件服务节点|
|**输出参数**|**参数类型**|**参数说明**|
|返回值|int|0 ：成功    其他：失败|

- 接口名称：addServerCallback
- 接口功能说明：添加文件服务节点对应回调接口（即注册监听器回调函数）
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|server|FileInfo |文件信息|
|ret|int|返回值|
|**输出参数**|**参数类型**|**参数说明**|
|NA|NA|NA|


##### (3). 文件类操作类接口

###### a. 上传文件

- 接口名称：uploadFile
- 接口功能说明：上传文件
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|fileInfo|FileInfo |文件信息|
|file|String |文件路径|
|**输出参数**|**参数类型**|**参数说明**|
|返回值|int|0 ：成功    其他：失败|

- 接口名称：uploadFileCallback
- 接口功能说明：上传文件对应回调接口（即注册监听器回调函数）
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|fileInfo|FileInfo|文件信息|
|ret|int|返回值|
|**输出参数**|**参数类型**|**参数说明**|
|NA|NA|NA|

###### b. 下载文件

- 接口名称：downloadFile
- 接口功能说明：下载文件
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|fileServer|FileServer|文件服务节点信息|
|fileid|String |文件ID|
|store_path|String |文件存储完整路径|
|ret|int|返回值|
|**输出参数**|**参数类型**|**参数说明**|
|NA|NA|NA|


- 接口名称：downloadFileCallback
- 接口功能说明：下载文件对应回调接口（即注册监听器回调函数）
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|fileInfo|FileInfo|文件信息|
|ret|int|返回值|
|**输出参数**|**参数类型**|**参数说明**|
|NA|NA|NA|

###### c. 删除文件

- 接口名称：deleteFile
- 接口功能说明：删除文件
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|fileServer|FileServer|文件服务节点信息|
|fileid|String |文件ID|
|**输出参数**|**参数类型**|**参数说明**|
|返回值|int|0 ：成功    其他：失败|

- 接口名称：deleteFileCallback
- 接口功能说明：删除文件对应回调接口（即注册监听器回调函数）
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|fileInfo|FileInfo|文件信息|
|ret|int|返回值|
|**输出参数**|**参数类型**|**参数说明**|
|NA|NA|NA|

##### (4). 文件服务器信息节点操作类接口

###### a. 添加文件服务器节点

- 接口名称：addServer
- 接口功能说明：添加文件服务器节点
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|fileServer|FileServer|文件服务节点信息|
|**输出参数**|**参数类型**|**参数说明**|
|返回值|int|0 ：成功    其他：失败|

- 接口名称：addServerCallback
- 接口功能说明：添加文件服务器节点对应回调接口（即注册监听器回调函数）
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|server|FileInfo |文件信息|
|ret|int|返回值|
|**输出参数**|**参数类型**|**参数说明**|
|NA|NA|NA|


- 接口名称：addServer
- 接口功能说明：添加文件服务器节点
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|server|FileServer|文件服务节点信息|
|ret|int|返回值|
|**输出参数**|**参数类型**|**参数说明**|
|NA|NA|NA|

###### b. 删除文件服务器节点

- 接口名称：deleteServer
- 接口功能说明：删除文件服务器节点
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|serverid|String |文件服务节点ID|
|**输出参数**|**参数类型**|**参数说明**|
|返回值|int|0 ：成功    其他：失败|

- 接口名称：deleteServerCallback
- 接口功能说明：删除文件服务器节点对应回调接口（即注册监听器回调函数）
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|server|FileServer |文件服务节点信息|
|ret|int|返回值|
|**输出参数**|**参数类型**|**参数说明**|
|NA|NA|NA|

###### c. 查询文件服务器节点列表

- 接口名称：listServers
- 接口功能说明：获取文件服务节点列表
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|NA|NA|NA|
|**输出参数**|**参数类型**|**参数说明**|
|返回值|int|0 ：成功    其他：失败|

- 接口名称：listServersCallback
- 接口功能说明：获取文件服务节点列表对应回调接口（即注册监听器回调函数）
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|fileservers|Vector<FileServer>|文件服务节点信息|
|ret|int|返回值|
|**输出参数**|**参数类型**|**参数说明**|
|NA|NA|NA|

###### d. 按组查询文件服务器节点信息列表

- 接口名称：listFilesByGroup
- 接口功能说明：按节点群组列举出所有文件信息
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|group|String |服务节点群ID|
|**输出参数**|**参数类型**|**参数说明**|
|返回值|int|0 ：成功    其他：失败|

- 接口名称：listServersByGroupCallback
- 接口功能说明：按节点群组列举出所有文件信息对应回调接口（即注册监听器回调函数）
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|fileservers|Vector<FileServer>|文件信息列表|
|ret|int|返回值|
|**输出参数**|**参数类型**|**参数说明**|
|NA|NA|NA|

###### e. 操作异常或错误下的回调接口

- 接口名称：errorCallback
- 接口功能说明：按节点群组列举出所有文件信息
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|code|int |错误码|
|error|String |错误信息|
|**输出参数**|**参数类型**|**参数说明**|
|NA|NA|NA|

#### 3.2.2.2使用举例
下面是一个完整展示如何使用分布式文件存储和访问功能的实例。示例代码可以参看源码工程，源代码具体位置为com.sample包的HelloDfs类。

1.定义回调监听类

```cpp
public class HelloListener implements IDfsListener{
//回调接口实现
...
...
}
```
2.初始化SDK

```cpp
String walletPassword = "1111";//钱包口令
String keyPath = "E:\\keys\\2429feaf-0e46-ddd5-5b72-0594e0a4ab00.json";//钱包文件
String fileContract = "0x9e86c1b4c4d82607298faac27da7d78364434c58";//文件信息合约地址
String fileServerContract = "0x0d76426479e438fe0d261dbb165cd3adcc4a1dd7";//文件服务合约地址
Strign host = "10.10.8.232";//服务节点主机
int port = 8545;//JSONRPC端口
init(host, port, TestListener, walletPassword, keyPath, fileContract, fileServerContract);//初始化
```

3.添加文件服务节点

```cpp
FileServer fileServer = new FileServer();
fileServer.setGroup("group1");
fileServer.setId("0c1585a170e75c14564427f2faa8d74850c65765a10ce9aa4fcc55c7cdd60ab7768dd7fa066145d0ebada2214705fca7f464186e61ea163a18e6ebb9c99c7627");
fileServer.setHost("10.10.8.232");
fileServer.setPort(8545);
fileServer.setInfo("the master node");
fileServer.setOrganization("weiwan");
fileServer.setEnable(1);
Calendar calendar = Calendar.getInstance();
fileServer.setUpdateTime((int)calendar.getTime().getTime()/1000);   
addServer(fileServer);//添加文件服务节点，等待回调add_server_back成功
```

4.上传文件

```cpp
FileInfo fileInfo = new FileInfo();
fileInfo.setOwner("weiwan");
fileInfo.setPriviliges("rwx");
String file = "E:"+ File.separatorChar + "test.txt";
uploadFile(fileInfo, file);//上传文件，等待回调upload_file_back
//...回调成功
//回调返回文件ID，可以使用文件ID下载和操作文件
//另外在配置文件目录，可以查看文件
```

## 3.3 加法同态接口使用说明
对本版本所提供的加法同态运算功能，提供如下使用说明：
1.Java的API实现的同态加密算法，可以完成对需要运算的数据完成加解密操作,相关的代码工程路径为：{git_root}\tool\java\Pailler;

2.Solidity合约语言提供一个Library，实现对两个加密后数据实现加法同态运算，相关的合约路径为：{git_root}\systemcontractv2\LibPaillier.sol;

3.提供C语言同态加法算法实现，最终开放接口为智能合约同态加法运算API，相关代码路径为：{git_root}\libpailler;

4.扩展VM中的sha3指令用以支持合约与C代码底层能力的桥接和适配，相关代码路径为：{git_root}\libevm.

### 3.3.1 Java API说明

#### 3.3.1.1. 同态密钥管理对象（PaillierKeyPair）

- 接口名称：generateKeyPair
- 接口功能说明：生成同态加密的密钥对
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
||||
|**输出参数**|**参数类型**|**参数说明**|
|返回值|KeyPair|生成的密钥对 （其他 ：成功    null：失败）|

#### 3.3.1.2. 同态算法对象（PaillierCipher）

- 接口名称：encryption
- 接口功能说明：对数据进行同态加密
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|m|BigInteger|待加密的操作数|
|publickey|PublicKey|加密公钥，可以使用同态密钥管理对象获取|
|**输出参数**|**参数类型**|**参数说明**|
|返回值|String|加密后的输出 （其他 ：成功    null：失败）|

- 接口名称：decryption
- 接口功能说明：对加密数据进行解密还原操作数
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
||||
|**输出参数**|**参数类型**|**参数说明**|
|返回值|||

- 接口名称：encryption
- 接口功能说明：对数据进行同态加密
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|ciphertext|String|加密的密文数据|
|privateKey|PrivateKey|加密公钥对应的私钥，可以使用同态密钥管理对象获取|
|**输出参数**|**参数类型**|**参数说明**|
|返回值|BigInteger|加密后的操作数 （其他 ：成功    null：失败）|

- 接口名称：ciphertextAdd
- 接口功能说明：加法同态实现
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|Ciphertext1|String|同态加密后的操作数1|
|Ciphertext2|String|同态加密后的操作数2|
|**输出参数**|**参数类型**|**参数说明**|
|返回值|String|加法同态后的结果数 （其他 ：成功    null：失败） |

#### 3.3.1.3. 使用样例

```
// generate the key pair for encrypt and decrypt
KeyPair keypair = PaillierKeyPair.generateKeyPair();
RSAPublicKey pubKey = (RSAPublicKey)keypair.getPublic();
RSAPrivateKey priKey = (RSAPrivateKey)keypair.getPrivate();

// encrypt the first number with public key
BigInteger i1 = BigInteger.valueOf(1000000);
String c1 = PaillierCipher.encryption(i1, pubKey);

// encrypt the second number with same public key
BigInteger i2 = BigInteger.valueOf(2012012012);
String c2 = PaillierCipher.encryption(i2, pubKey);

// paillier add with numbers
String c3 = PaillierCipher.ciphertextAdd(c1,c2);

// decrypt the result
BigInteger o3 = PaillierCipher.decryption(c3, priKey);
```


### 3.3.2 Solidity API说明

#### 3.3.2.1. 同态加法库（LibPaillier.sol）

- 接口名称：pai_add
- 接口功能说明：加法同态的合约实现
- 参数说明

|输入参数|参数类型|参数说明|
|:-----|:-----|:-----|
|D1|String|加密后的第一个加法操作数|
|D2|String|加密后的第二个加法操作数|
|**输出参数**|**参数类型**|**参数说明**|
|返回值|String|同态加法的结果其他 ：成功    空串：失败 |

*注：该合约实现复用了sha3的机制，采用特定的前缀进行扩展算法实现*

#### 3.3.2.2. 使用样例

```
import "LibPaillier.sol";

contract Test {
　　using LibPaillier for *;
　　
　　string balance;
　　
　　function addBalance(string n) public returns (int) {
　　    string memory result = LibPaillier.pai_add(balance, n);
　　    if (bytes(result).length == 0) {
　　        return 1;
　　　　}
　　　　
　　　　balance = result;
　　　　return 0;
　　}
}
```

[systemContract]:images/i3.1.png "系统合约部署示例"

# 第四章、部署合约
本章以一个HelloWorld.sol智能合约为例介绍部署合约的方法。
该合约中有一个name成员变量。同时有更新接口set和读取接口get,合约源码如下：
```javascript
contract HelloWorld{
    string name;
    function HelloWorld(){
       name="Hi,Welcome!";
    }
    function get()constant returns(string){
        return name;
    }
    function set(string n){
    	name=n;
    }
}
```
工具目录下已有部署脚本。更新目录下config.js中的Ip（内网监听IP）和端口（RPC监听端口），及privKey（注意没有0x前缀）和account（注意有0x前缀）	（生成方法参看生成公私钥对）后直接使用即可。
```bash
cd tool
vim config.js  
babel-node deploy.js HelloWorld
```
输出示例如下：

![示例输出](./images/i4.1.png)

输出即是HelloWorld.sol智能合约地址。
# 第五章、调用合约
本章以第四章中的HelloWorld.sol智能合约为例介绍调用合约的方法。
调用更新接口set和读取接口get。
```bash
cd tool
babel-node demoHelloWorld.js
```
输出示例如下：

![示例输出](./images/i5.1.png)

输出即是调用合约set接口后的结果。
以上仅为示例，可以新建智能合约文件或查看、修改HelloWorld.sol，及对应的调用脚本	以符合需要。
智能合约Solidity语言教程请参考http://solidity.readthedocs.io/en/latest/
# 第六章、使用控制台
控制台是通过IPC方式连接区块链节点进程的一种方式。
控制台可以查看区块信息、查看交易信息、查看节点连接信息。
打开控制台命令行：ethconsole geth.ipc文件路径
```bash
ethconsole /mydata/nodedata-1/geth.ipc
```
## 6.1. 查看区块
命令行 ：web3.eth.getBlock(块号,console.log)
如输出显示块号1的块信息
```bash
web3.eth.getBlock(1,console.log)
```
示例输出如下（字段含义请参看区块头结构）：
![示例输出](./images/i6.1.png)
## 6.2. 查看交易
命令行 ：web3.eth.getTransaction(交易哈希,console.log)

如输入
```bash
web3.eth.getTransaction('0x32e7970afe197053b6b18771dea01e392da9cf9173b0a9a22ce3778de0a109d3',console.log)
```
示例输出如下（字段含义请参看交易结构）：

![示例输出](./images/i6.2.png)

## 6.3. 查看交易回执
命令行 ：web3.eth.getTransactionReceipt（交易哈希，console.log）

如输入
```bash
web3.eth.getTransactionReceipt('0x32e7970afe197053b6b18771dea01e392da9cf9173b0a9a22ce3778de0a109d3',console.log)console.log)
```
示例输出如下（字段含义请参看交易回执结构）：
![示例输出](./images/i6.3.png)
## 6.4. 查看合约
命令行 ：web3.eth.getCode(合约地址，console.log)

如输入
```bash
web3.eth.getCode('0x932169080dd008768f4bf1fbae7ca706d85525b9',console.log)
```
示例输出如下（输出为合约二进制编码，非空即是表示合约存在）：
![示例输出](./images/i6.4.png)
## 6.5. 查看节点连接
命令行 ：web3.admin.getPeers(console.log)

如输入
```bash
web3.admin.getPeers(console.log)
```
示例输出如下（字段含义请参看节点连接信息）：
![示例输出](./images/i6.5.png)
# 第七章、多节点组网

本章详细介绍在一个创始节点已正常运行的前提下，如何一步步操作，加入新的区块链节 点，    组成区块链网络的操作流程。

系统合约是 OpenDL 区块链的重要设计思路之一，也是控制网络节点加入和退出的重要方式，

因此[部署系统合约](#7.1 部署系统合约)是多节点组网的前提条件。并且仅需执行一次。

新节点加入网络的过程主要有三个步骤[新建非创世节点](#7.2 新建非创世节点)、[非创世节点加入网络](#7.3 非创世节点加入网络)、[非创世节点启动](#7.4 非创世节点启动)。

## 7.1 部署系统合约

执行以下命令：

```shell
cd systemcontractv2/output
tar xvf linux.tar
cd ..
vim config.js 	#更新内网监听Ip和RPC监听端口，及privKey和account（可复用第四章部署合约 中的或重新生成）
babel-node deploy.js
```

示例输出如下：

![7.1](images/i7.1.png)

1.从上面输出中获得系统代理合约地址，更新到创世节点运行参数配置文件```config.json```中的```systemproxyaddress``` 字段。然后重启创世节点区块链进程。

2.编辑系统合约目录下的```node1.json```。将其中的字段内容更新为创世节点的```config.json```中的```NodeextraInfo```部分自身的节点信息。（id=NodeId、ip=Peerip、port=Port、desc=Nodedesc、category=Identitytype、CAhash留空字符串即可、agencyinfo=Agencyinfo、idx=Idx注意字段值必须完全一样！）

   示例如下：

   ![7.2](images/i7.2.png)

3.添加创世节点入网 

```shell
babel-node tool.js NodeAction registerNode node1.json
```

输出示例如下：

![7.3](images/i7.3.png)

“发送交易成功”即表示交易请求已发出。

4.查看创世节点是否已在节点列表中

```shell
babel-node tool.js NodeAction all
```

输出示例如下：

![7.4](images/i7.4.png)

输出中有```NodeIdsLength = 1```即是代表节点列表中有一个节点了。

## 7.2 新建非创世节点

1.新建节点目录，下文设定新节点目录为```/mydata/nodedata-2/ ```

2.参看[生成节点身份NodeId](#2.4 生成节点身份NodeId) ，并将新的```network.rlp network.rlp.pub``` 文件输出到 ```/mydata/nodedata-2/ ```

3.拷贝创世块文件```genesis.json```和创世节点的配置文件 ```config.json``` 及日志配置文件``` log.conf``` 到新节点目录```/mydata/nodedata-2/ ```

4.更新```/mydata/nodedata-2/ ```的```config.json```文件中的```rpcport、p2pport、wallet、keystoredir、datadir```字段（注意：若多个节点同在一台服务器，需要配置rpcport、 p2pport均不互相冲突）。

5.更新```/mydata/nodedata-2/ ```的log.conf中的日志路径。

6.追加非创世节点信息到 ```/mydata/nodedata-2/``` 的```config.json```文件中的 ```NodeextraInfo``` 字段。

示例如下：

```json
"NodeextraInfo":[
  {	"Nodeid":"de0fa385816b505799433e54b88788e21cb42092a6ff5bcaa2285d7ace906e5e6ce8ef2b30134ff276a5834d58721291acc5864e07e6d52469b79b28e699dfde",
     "Nodedesc": "node1",
     "Agencyinfo": "node1",
     "Peerip": "119.29.57.154",
     "Identitytype": 1,
     "Port":30303,
     "Idx":0
  },
  {   "Nodeid":"2a0cb7425bd63835d346ae20ec6107d28f64009a9dcca7d485713f1dfe7c4edf6e79b7e08a70cb8220beb133327361313e6bc793433311ba997a81e14800a85f",
      "Nodedesc": "node2",
      "Agencyinfo": "node2",
      "Peerip": "xxxx",
      "Identitytype": 1,
      "Port":30304,
      "Idx":1
  }
]
```

其中字段说明如下：

| **字段**       | **说明**            |
| ------------ | ----------------- |
| Nodeid       | 节点ID              |
| Nodedesc     | 节点描述              |
| Agencyinfo   | 节点信息              |
| Peerip       | P2P网络监听端口（建议外网IP） |
| Identitytype | 节点类型              |
| Port         | P2P网络端口           |

## 7.3 非创世节点加入网络

执行以下命令：

```shell
cd systemcontractv2/
vim node2.json
```

1.编辑系统合约目录下的节点信息文件```node2.json```。将其中的字段内容更新为```/mydata/nodedata-2/``` 的```config.json```中的```NodeextraInfo```部分的```node2```的节点信息。（注意字段值必须完全一样！）

2.添加节点入网 

   ```
   babel-node tool.js NodeAction registerNode node2.json
   ```

   输出示例如下：

   ![7.5](images/i7.5.png)

3.查看是否已经入网

   ```shell
   babel-node tool.js NodeAction all
   ```

   输出示例如下：

   ![7.6](images/i7.6.png)

   输出中有 ```NodeIdsLength = 2``` 即是代表节点列表中已有个节点了。

## 7.4 非创世节点启动

执行以下命令启动区块链节点进程：

```shell
./build/eth/eth --genesis /mydata/nodedata-2/genesis.json --config /mydata/nodedata-2/config.json
```

查看块高变化及连接节点信息：

```shell
babel-node monitor.js
```

输出示例如下：

![7.7](images/i7.7.png)

输出中“已连接节点数：1”代表已连接上的对端节点数。

输出中“当前块高11690”代表当前块高，块高不断增长说明在正常运行。

 至此，两个节点的区块链网络已组成功！

若需要继续添加新节点入网，重复本章的[新建非创世节点](#7.2 新建非创世节点)、[非创世节点加入网络](#7.3 非创世节点加入网络)、[非创世节点启动](#7.4 非创世节点启动) 即可。

## 7.5 节点退出网络

命令行：```babel-node tool.js NodeAction cancelNode ``` 节点信息json文件

最后一个参数“节点信息json文件” 即是[非创世节点加入网络](#7.3 非创世节点加入网络) 中节点加入网络时对应的节点信息文件。

执行删除命令后，可以通过执行以下命令来查看当前节点列表：

```shell
babel-node tool.js NodeAction all
```

# 第八章、机构证书准入介绍

## 8.1 机构证书的作用说明

机构身份认证功能，为机构备案公钥证书到区块链，作为准入机制，使得区块链上节点能 够与现实身份对应，节点接入联盟链时需验证该节点身份，节点身份验证通过才能接入联    盟链，节点行为可追溯。

## 8.2  注册新证书

生成私钥 private.key 和公钥 public.key 

```shell
openssl genrsa -out private.key 1024
openssl rsa -in private.key -pubout -out public.key
```

## 8.3 启用机构证书验证

### 8.3.1 准备工作

1.假设本地有 public key 和 private key，然后放到如下位置目录：

   ```shell
   /mydata/nodedata/CA/
   ```

   public.key的内容如下(内容已省略):

   ```
   -----BEGIN PUBLIC KEY-----
   ***
   ***
   -----END PUBLIC KEY-----
   ```

   private.key的内容如下(内容已省略)

   ```
   -----BEGIN RSA PRIVATE KEY-----
   ***
   ***
   -----END RSA PRIVATE KEY-----
   ```

2.把公钥public key写入合约

   ```shell
   babel-node tool.js CAAction update ca.json
   ```

   ca.json结构：

   ```json
   {
       "hash":"xxx",
       "pubkey":"xxxx",
       "orgname":"BCOS银行",
       "notbefore":20170223,
       "notafter":20180223,
       "status":1,
       "whitelist":"192.168.1.1;192.168.1.2;192.168.1.3",
       "blacklist":"192.168.1.11;192.168.1.12;192.168.1.13"
   }
   ```
其中字段说明如下：

| **字段**       | **说明**            |
| ------------ | ----------------- |
| hash      | 对证书的sha256sum后的hash，这里的证书是指能导出public.key的原有证书 |
| pubkey    | public.key的整个文本信息，注意对换行符的替换              |
| orgname   | 机构名称                                     |
| notbefore | 证书生效时间                                   |
| notafter  | 证书过期时间                                   |
| status    | 证书状态 0：不可用 1：可用                          |
| whitelist | ip白名单，只允许哪些ip可以连接当前节点，默认为空字符串，允许所有ip     |
| blacklist | ip黑名单，不允许哪些ip连接当前节点，默认为空字符串，代表没有禁止ip     |

### 8.3.2 打开开关

开关打开后，会对当前所有节点进行CA验证，如果节点在目录  ```/mydata/nodedata/CA/``` 没有相关的证书，那么会被对方节点拒绝。

开启开关前，先确保当前所有节点的证书已经上链。

开启全局CAVerify开关

```shell
babel-node tool.js ConfigAction set CAVerify true
```

查看开关是否生效

```shell
babel-node tool.js ConfigAction get CAVerify
```

若出现以下信息，则代表成功：

![](images/i8.png)

### 8.3.3 查看日志

开启开关并且通过CA模块验证后，通过以下命令找出相关记录

```shell
grep "verify ok" /mydata/nodedata-1/log/*
```

### 8.4 更新证书状态

如果证书过期或者被注销，可以通过以下命令来剔除用该证书的机构所有连接节点

```shell
babel-node tool.js CAAction updateStatus ca.json
```

其中，ca.json的 status 要设置为0。检查日志操作是否生效

```shell
grep "blockNumber is 0 or status is false" /mydata/nodedata-1/log/*
```

### 8.5  证书注意事项

1.妥善保管 private.key，切勿泄露，同时应把证书，public.key 和 private.key 备份到安全位置

2.开启全局 CA 开关 CAVerify 开关前，先确保当前所有连接节点已有相应的证书在指定目录，同时注意文件的名字 public.key，private.key。

3.一旦把 CAVerify 开关开启后，不建议再把 CAVerify 关闭。

# 第九章、系统合约介绍
系统合约是BCOS区块链的内置智能合约。
主要分为以下几种：系统代理合约、节点管理合约、机构证书合约、权限管理合约、全网配置合约。

系统合约一经部署，全网生效后，一般不轻易变更。
若需重新部署变更升级，需全网所有节点都认可且重新更新运行参数配置文件的systemproxyaddress字段后重启。
系统合约原则上只允许管理员账号调用。

以下依次介绍各个系统合约的源码路径、已实现接口说明、调用例子、工具使用方法。



## 9.1．系统代理合约
系统代理合约是系统合约的统一入口。
它提供了路由名称到合约地址的映射关系。

源码路径：systemcontractv2/SystemProxy.sol

### ①　接口说明
|接口名	|输入	|输出|	备注|
|----|----|----|----|
|获取路由信息 getRoute	|路由名称	|路由地址、缓存标志位、生效块号	|无|
|注册路由信息setRoute|	路由名称、路由地址、缓存标志位	|无	|若该路由名称已存在，则覆盖|

web3调用示例如下（可参看systemcontractv2/deploy.js）：
```js
console.log("注册NodeAction.....");
func = "setRoute(string,address,bool)";
params = ["NodeAction", NodeAction.address, false];
receipt = await web3sync.sendRawTransaction(config.account, config.privKey, SystemProxy.address, func, params);
```

### ②　工具使用方法
查看所有系统合约信息：

```  babel-node tool.js SystemProxy ```


示例输出如下：

![output example](./images/i9.1.png)

输出中即是当前系统路由表的所有路由信息。


## 9.2．节点管理合约
节点管理合约主要功能是维护网络中节点列表。
网络中节点加入或退出都需要与节点管理合约进行交互。

源码路径：systemcontractv2/NodeAction.sol
### ①　接口说明
|接口名	|输入|	输出|	备注|
|----|----|----|----|
|节点入网 registerNode	|节点ID、IP、端口、节点类型、节点描述、节点CA哈希、节点agency、节点序号	|布尔结果	|若该节点ID已存在，则忽略|
|节点出网 cancelNode	|节点ID	|布尔结果	|若该路由名称不存在，则忽略

web3调用示例如下（可参看systemcontractv2/tool.js）：
```js
var instance=getAction("NodeAction");
var func = "registerNode(string,string,uint256,uint8,string,string,string,uint256)";
var params = [node.id,node.ip,node.port,node.category,node.desc,node.CAhash,node.agencyinfo,node.idx]; 
var receipt = web3sync.sendRawTransaction(config.account, config.privKey, instance.address, func, params);
```

### ②　工具使用方法
请参看 添加非创世节点入网、节点出网

## 9.3．机构证书合约
机构证书合约主要功能是维护机构证书信息。

源码路径：systemcontractv2/CAAction.sol

### ①　接口说明
|接口名	|输入	|输出|	备注|
|----|----|----|----|
|更新证书信息 update	|证书哈希、公钥、组织名称、证书有效开始时间、证书失效时间、证书状态、白名单列表、黑名单列表	|布尔结果	|若该证书信息不存在，则新建|
|更新证书状态 updateStatus	|证书哈希、证书状态	|布尔结果	|若该路由名称不存在，则忽略
|查询证书信息 get	|证书哈希	|证书哈希、公钥、组织名称、证书有效开始时间、证书失效时间、证书状态、块号	|无
|查询证书黑白名单列表 getIp	|证书哈希	|白名单列表、黑名单列表	|无

web3调用示例如下（可参看systemcontractv2/tool.js）：
```js
var instance=getAction("CAAction");
var func = "update(string,string,string,uint256,uint256,uint8,string,string)";
var params = 
[ca.hash,ca.pubkey,ca.orgname,ca.notbefore,ca.notafter,ca.status,ca.whitelist,ca.blacklist]; 
var receipt = 
web3sync.sendRawTransaction(config.account, config.privKey, instance.address, func, params);
```

### ②　工具使用方法
查看证书列表

```babel-node tool.js CAAction all```

更新证书信息

```babel-node tool.js CAAction update```

更新证书状态

```babel-node tool.js CAAction updateStatus```


## 9.4．权限管理合约
权限管理合约是对区块链权限模型的实现。
一个外部账户只属于一个角色，一个角色拥有一个权限项列表。
一个权限项由合约地址加上合约接口来唯一标识。

源码路径：systemcontractv2/AuthorityFilter.sol	交易权限Filter
 systemcontractv2/Group.sol	角色


### ①　接口说明

|合约	|接口名	|输入|	输出|	备注|
|----|----|----|----|----|
|角色	|设置用户权限组 权限项 setPermission|	合约地址、合约接口、权限标记|	无	|无|
|  |	获取权限标记 getPermission	|合约地址、合约接口	|权限标记|	无|
|交易权限Filter	|设置用户所属角色 setUserGroup	|用户外部账户、用户所属角色合约	|无	|无|
| |	交易权限检查 process	|用户外部账户、交易发起账户、合约地址、合约接口、交易输入数据	|	|无|

web3调用示例如下（可参看systemcontractv2/deploy.js）：
```js
func = "setUserGroup(address,address)";
params = [config.account, Group.address];
receipt = await web3sync.sendRawTransaction(config.account, config.privKey, AuthorityFilter.address, func, params);
console.log("授予"+config.account+"角色"+Group.address);
```

### ②　工具使用方法
检查用户外部账户权限

```babel-node tool.js AuthorityFilter 用户外部账户、合约地址、合约接口```



### ③　自主定制
继承TransactionFilterBase实现新的交易Filter合约。并通过addFilter接口将新Filter注册入TransactionFilterChain即可。

## 9.5．全网配置合约
全网配置合约维护了区块链中部分全网运行配置信息。
目标是为了通过交易的全网共识来达成全网配置的一致更新。

源码路径：systemcontractv2/ConfigAction.sol

### ①　全网配置项说明
|配置项|	说明	|默认值|	推荐值|
|----|----|----|----|
|maxBlockHeadGas	|块最大GAS （16进制）|	200000000	|20000000000|
|intervalBlockTime|	块间隔(ms) （16进制）|	1000|	1000|
|maxBlockTranscations	|块最大交易数（16进制）	|1000|	1000|
|maxNonceCheckBlock	|交易nonce检查最大块范围（16进制）|	1000|	1000|
|maxBlockLimit|	blockLimit超过当前块号的偏移最大值（16进制）	|1000|	1000|
|maxTranscationGas|	交易的最大gas（16进制）|	20000000	|20000000|
|CAVerify|	CA验证开关|	FALSE|	FALSE|

### ②　接口说明
|接口名|	输入|	输出|	备注|
|----|----|----|----|
|设置配置项 set|	配置项、配置值	|无	|若配置表中已存在，则覆盖|
|查询配置值 get|	配置项|	配置值、块号|	无|

web3调用示例如下（可参看systemcontractv2/tool.js）：
```js
var func = "set(string,string)";
var params = [key,value];
var receipt = web3sync.sendRawTransaction(config.account, config.privKey, instance.address, func, params);
console.log("配置项:"+key+","+value);
```

### ③　使用方法
查询配置项

```babel-node tool.js ConfigAction get配置项```

设置配置项

```babel-node tool.js ConfigAction set配置项 配置值```
# 第十章、分布式数据库
区块链进程支持将区块、状态数据存储到数据库，在config.json中增加如下配置：

```
"dbconf": {
        "stateDbConf": {
                "dbInfo": "x.x.x.x:x@db",
                "dbName": "db",
                "tableName": "t_state",
                "userName": "****",
                "pwd": "******",
                "cacheSize": 104857600,
                "engineType": 1 //1-mysql 2-oracle
        },
        "blockDbConf": {
                "dbInfo": "x.x.x.x:x@db",
                "dbName": "db",
                "tableName": "t_block",
                "userName": "****",
                "pwd": "******",
                "cacheSize": 104857600,
                "engineType": 1 //1-mysql 2-oracle
        },
        "extrasDbConf": {
                "dbInfo": "x.x.x.x:x@db",
                "dbName": "db",
                "tableName": "tb_extras",
                "userName": "*****",
                "pwd": "******",
                "cacheSize": 104857600,
                "engineType": 1 //1-mysql 2-oracle
        }
}
```

配置中，一共指定三个表，分别是状态存储、区块存储和额外存储；

重新编译区块链进程，cmake时增加-DETH_ODBC选项，即可开启数据库存储。


# 第十一章、其他可选工具介绍

本章介绍两个可选工具的使用场景与使用方法

## 11.1 创世块工具
创世块工具主要是为了将当前区块链中现有的所有合约数据作为快照导出。

一旦新链以此快照作为创世块文件启动，新链就可以快速拥有了当前区块链的所有合约数据。

命令行：

区块链执行程序路径 --genesis 创世块文件 --config 配置文件 --export-genesis 导出新创世块文件路径

示例如下：

```
./build/eth/eth --genesis ./genesis.json --config ./config.json 	
--export-genesis ./genesis.file
```

执行输出的genesis.file即是新创世块文件。


## 11.2 监控脚本工具

为了方便快速的查看当前节点的网络连接及块高情况。可使用本工具。

```
cd tool
babel-node monitor.js
```

示例输出如下:

![](images/i10.1.jpg)

输出中“已连接节点数”代表当前节点已连接上的区块链节点数。

输出中“当前块高”代表当前节点的区块链高度。如果在不断增长，代表区块链在正常运行。


# 第十二章、问题反馈
欢迎任何人对本文档指出不足、提出优化建议。

欢迎任何人参与BCOS区块链开源平台的使用、优化、改进。

欢迎任何人反馈BCOS区块链开源平台的任何问题。

若有问题反馈请尽可能包含以下信息：

1.平台源码版本（如有）
2.所在的操作系统或平台
3.问题重现的方法或步骤
4.问题期望的表现
5.源码文件及行数（如有）

随时联系我们：hi@bcos.org.cn


# 第十三章、附录

## 13.1 区块头结构
关键字段 | 说明
------------ | ------------- 
hash | 块哈希
difficulty | 块难度（默认1）
parentHash | 父块哈希
timestamp | 块时间戳
gasLimit | 块GAS最大限制
gasused | 块已使用GAS
number | 块号
sha3uncles | 叔伯块哈希
stateroot | 世界状态树根
transcationroot | 交易树根
receiptsroot | 交易收据树根
genidx | 记账节点编号
nodelist | 记账节点公钥列表

## 13.2 交易结构
关键字段 | 说明
------------ | ------------- 
sender | 交易发送者外部账号
gas | 交易GAS上限
receiveaddress | 合约地址
data | 交易数据
sign | 交易签名
randomid | 交易随机数
blocklimit | 交易最大块号限制


## 13.3 交易回执结构
关键字段 | 说明
------------ | ------------- 
blockNumber | 交易所在块号
contractAddress | 合约地址
cumulativeGasUsed | 交易预估消耗GAS
gasUsed | 交易实际消耗GAS
logs | EventLog列表
transactionHash | 交易哈希
transactionIndex | 交易序号

## 13.4 节点连接信息
关键字段 | 说明
------------ | ------------- 
id | 节点身份NodeId
name | 节点软件版本信息
network | 节点网络IP信息
