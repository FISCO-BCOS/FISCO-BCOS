# FISCO BCOS区块链操作手册

[FISCO-BCOS Manual](doc/manual/README_EN.md)

## 前言

本文档是FISCO BCOS区块链开源平台的一部分。
FISCO BCOS平台是金融区块链合作联盟（深圳）（以下简称：金链盟）开源工作组以金融业务实践为参考样本，在BCOS开源平台基础上进行模块升级与功能重塑，深度定制的安全可控、适用于金融行业且完全开源的区块链底层平台。  

金链盟开源工作组获得金链盟成员机构的广泛认可，并由专注于区块链底层技术研发的成员机构及开发者牵头开展工作。其中首批成员包括以下单位（排名不分先后）：博彦科技、华为、深证通、神州数码、四方精创、腾讯、微众银行、越秀金科。   

FISCO BCOS平台基于现有的BCOS开源项目进行开发，聚焦于金融行业的分布式商业需求，从业务适当性、性能、安全、政策、技术可行性、运维与治理、成本等多个维度进行综合考虑，打造金融版本的区块链解决方案。

为了让大家更好的了解FISCO BCOS区块链开源平台的使用方法。本文档按照Step By Step的步骤详细介绍了FISCO BCOS区块链的构建、安装、启动，智能合约部署、调用等初阶用法，还包括多节点组网、系统合约等高阶内容的介绍。

本文档不介绍FISCO BCOS区块链设计理念及思路，详情请参看白皮书。

以下代码及操作命令以在Centos7.2/Ubuntu 16.04操作系统上为示例。

## 第一章 部署FISCO BCOS环境

本章主要介绍FISCO BCOS区块链环境的部署。包括机器配置，部署软件环境和编译源码。

### 1.1 机器配置

| 配置   | 最低配置   | 推荐配置                                 |
| ---- | ------ | ------------------------------------ |
| CPU  | 1.5GHz | 2.4GHz                               |
| 内存   | 1GB    | 4GB                                  |
| 核心   | 2核     | 4核                                   |
| 带宽   | 1Mb    | 5Mb                                  |
| 操作系统 |        | CentOS （7.2  64位）或Ubuntu（16.04  64位） |

### 1.2 部署软件环境

#### 1.2.1 安装依赖包

##### Centos安装

```shell
#安装依赖包
sudo yum install -y git openssl openssl-devel deltarpm cmake3 gcc-c++
#安装nodejs
sudo yum install -y nodejs
sudo npm install -g cnpm --registry=https://registry.npm.taobao.org
sudo cnpm install -g babel-cli babel-preset-es2017 ethereum-console
echo '{ "presets": ["es2017"] }' > ~/.babelrc
```
##### Ubuntu安装
```shell
#安装依赖包
sudo apt-get -y install git openssl libssl-dev libkrb5-dev cmake
#安装nodejs(注意： nodejs需要大于6以上的版本,ubuntu上面apt-get安装的默认版本为4.x版本,需要自己升级)
sudo apt-get -y install nodejs-legacy
sudo apt-get -y install npm
sudo npm install -g secp256k1
sudo npm install -g cnpm --registry=https://registry.npm.taobao.org
sudo cnpm install -g babel-cli babel-preset-es2017 ethereum-console
echo '{ "presets": ["es2017"] }' > ~/.babelrc
```

#### 1.2.2 安装FISCO BCOS的智能合约编译器

> 下载对应平台的solidity编译器, 直接下载后放入系统目录下。

```shell
[ubuntu]：
    wget https://github.com/FISCO-BCOS/fisco-solc/raw/master/fisco-solc-ubuntu
    sudo cp fisco-solc-ubuntu  /usr/bin/fisco-solc
    sudo chmod +x /usr/bin/fisco-solc
    
    
[centos]：
    wget https://github.com/FISCO-BCOS/fisco-solc/raw/master/fisco-solc-centos
    sudo cp fisco-solc-centos  /usr/bin/fisco-solc
    sudo chmod +x /usr/bin/fisco-solc
```

### 1.3 编译源码

#### 1.3.1 获取源码

> 假定在/mydata/下获取源码：

```shell
#生成mydata目录
sudo mkdir -p /mydata
sudo chmod 777 /mydata
cd /mydata

#clone源码
git clone https://github.com/FISCO-BCOS/FISCO-BCOS.git

#切换到源码根目录
cd FISCO-BCOS 
```

源码目录说明请参考<u>附录：11.1 源码目录结构说明</u>

#### 1.3.2 安装编译依赖

> 根目录下执行（若执行出错，请参考常见问题1）：

```shell
chmod +x scripts/install_deps.sh
./scripts/install_deps.sh
```

#### 1.3.3 编译

```shell
mkdir -p build
cd build/

[Centos] 
cmake3 -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF ..  #注意命令末尾的..
[Ubuntu] 
cmake  -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF ..  #注意命令末尾的..

make
```

> 若编译成功，则生成build/eth/fisco-bcos。

#### 1.3.4 安装

```shell
sudo make install
```



## 第二章 创建创世节点

创世节点是区块链中的第一个节点，搭建区块链，从创建创世节点开始。

### 2.1 创建节点环境

> 假定创世节点目录为/mydata/nodedata-1/，创建节点环境如下：

```shell
#创建目录结构
mkdir -p /mydata/nodedata-1/
mkdir -p /mydata/nodedata-1/data/ #存放节点的各种文件
mkdir -p /mydata/nodedata-1/log/ #存放日志
mkdir -p /mydata/nodedata-1/keystore/ #存放账户秘钥

#拷贝相关文件
cd /mydata/FISCO-BCOS/ 
cp genesis.json config.json log.conf start.sh stop.sh /mydata/nodedata-1/
```

### 2.2 配置god账号

god账号是区块链的最高权限，在启动区块链前必须配置。

#### 2.2.1 生成god账号

```shell
cd /mydata/FISCO-BCOS/web3lib
cnpm install #安装nodejs依赖, 在执行nodejs脚本之前, 该命令在该目录需要执行一次, 之后不需要再执行。
cd /mydata/FISCO-BCOS/tool #代码根目录下的tool文件夹
cnpm install #安装nodejs包，仅需运行一次，之后若需要再次在tool目录下使用nodejs，不需要重复运行此命令
node accountManager.js > godInfo.txt
cat godInfo.txt |grep address
```

> 可得到生成god账号的地址如下。godInfo.txt请妥善保存。

```log
address : 0x27214e01c118576dd5f481648f83bb909619a324
```

#### 2.2.2 配置god账号

> 将上述步骤生成的god的address配置入genesis.json的god字段：

```shell
vim /mydata/nodedata-1/genesis.json
```

> 修改后，genesis.json中的god字段如下：

```
"god":"0x27214e01c118576dd5f481648f83bb909619a324",
```

### 2.3  配置节点身份NodeId

NodeId唯一标识了区块链中的某个节点，在节点启动前必须进行配置。

#### 2.3.1 配置cryptomod.json文件

> 在cryptomod.json文件中配置NodeId生成路径：

```shell
vim /mydata/FISCO-BCOS/cryptomod.json
```

> 通常情况下，只需将rlpcreatepath配置为/mydata/nodedata-1/data/network.rlp即可。配置后的cryptomod.json如下：

```log
{
	"cryptomod":"0",
	"rlpcreatepath":"/mydata/nodedata-1/data/network.rlp",
	"datakeycreatepath":"",
	"keycenterurl":"",
	"superkey":""
}
```

cryptomod.json其它字段说明请参看<u>附录：11.2 cryptomod.json说明</u>

#### 2.3.2 生成节点身份文件

> 用上述修改好的cryptomod.json文件生成节点身份文件，生成路径为cryptomod.json中配置的路径。

```shell
cd /mydata/FISCO-BCOS/ 
fisco-bcos --gennetworkrlp  cryptomod.json #需要一段时间
ls /mydata/nodedata-1/data/
```

> 可看到节点身份文件（network.rlp和network.rlp.pub）生成到了/mydata/nodedata-1/data/下。
>
> 其中network.rlp是节点身份的私钥二进制文件。network.rlp.pub是节点身份的NodeId文件。

```shell
network.rlp  network.rlp.pub
```

#### 2.3.3 配置创世节点NodeId

（1）查看NodeId

```shell
cat /mydata/nodedata-1/data/network.rlp.pub
```

> 得到如下类似的NodeId

```log
2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878
```

（2）修改genesis.json

> 将NodeId配置入genesis.json的initMinerNodes字段，即指定此NodeId的节点为创世节点。

```shell
vim /mydata/nodedata-1/genesis.json
```

> 修改后，genesis.json中的initMinerNodes字段如下：

```log
"initMinerNodes":["2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878"]
```

（3）修改config.json

> 将NodeId配置入config.json中的NodeextraInfo的Nodeid字段。

```shell
vim /mydata/nodedata-1/config.json
```

> 修改后，config.json中的NodeextraInfo字段如下：

```log
"NodeextraInfo":[
	{
		"Nodeid":"2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878",
		"Nodedesc": "node1",
		"Agencyinfo": "node1",
		"Peerip": "0.0.0.0",
		"Identitytype": 1,
		"Port":30303,
		"Idx":0
	}
]
```

### 2.4 配置证书

区块链节点间的通信需要证书的认证。在节点运行前，需要为节点配置证书。证书包括：

- ca.crt：根证书公钥，整条区块链共用。
- ca.key：根证书私钥，私钥应保密，仅在生成节点证书公私钥时使用。
- server.crt：节点证书的公钥。
- server.key：节点证书的私钥，私钥应保密。

#### 2.4.1 生成根证书

> 将公私钥生成脚本拷贝到data目录，并执行命令生成根证书公私钥ca.key，ca.crt。

```shell
cp /mydata/FISCO-BCOS/genkey.sh /mydata/nodedata-1/data/ 
cd /mydata/nodedata-1/data/
chmod +x genkey.sh
./genkey.sh ca 365 #生成ca根证书有效期为365天
```

> 在data目录下生成了证书公私钥ca.key，ca.crt。ca.key应保密，并妥善保存，以便后续生成更多节点的公私钥。

#### 2.4.2 生成节点证书

> 用生成的根证书公私钥ca.crt和ca.key，生成节点自己的证书。

```shell
./genkey.sh server ./ca.key ./ca.crt 365 #注意key和crt前后顺序不能错；此步骤需按照提示输入一些信息；生成server证书有效期为365天
```

> 生成的server.key、server.crt即为节点证书文件

```shell
ls /mydata/nodedata-1/data/
```

> 此时目录下应存在有下述文件：

```log
ca.crt  network.rlp  network.rlp.pub  server.crt  server.key
```

### 2.5 配置相关配置文件

节点的启动依赖三个配置文件：

- 创世块文件：genesis.json
- 节点配置文件：config.json
- 日志配置文件：log.conf

#### 2.5.1 配置genesis.json（创世块文件）

genesis.json中配置创世块的信息，是节点启动必备的信息。

```shell
vim /mydata/nodedata-1/genesis.json
```

> 主要配置god和initMinerNodes字段，在之前的步骤中已经配置好，配置好的genesis.json如下：

```log
{
     "nonce": "0x0",
     "difficulty": "0x0",
     "mixhash": "0x0",
     "coinbase": "0x0",
     "timestamp": "0x0",
     "parentHash": "0x0",
     "extraData": "0x0",
     "gasLimit": "0x13880000000000",
     "god":"0x27214e01c118576dd5f481648f83bb909619a324",
     "alloc": {},
     "initMinerNodes":["2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878"]
}
```

genesis.json其它字段说明请参看<u>附录：11.3 genesis.json说明</u>

#### 2.5.2 配置config.json（节点配置文件）

config.json中配置节点的各种信息，包括网络地址，文件目录，节点身份等。

```shell
vim /mydata/nodedata-1/config.json
```

> 配置节点的信息，主要修改字段：
>
> - 网络连接相关：listenip、rpcport、p2pport、channelPort
> - 目录相关：wallet、keystoredir、datadir、logconf
> - 节点身份相关：NodeextraInfo中的Nodeid、Nodedesc、Agencyinfo、Peerip、Identitytype、Port、Idx（与网络连接相关对应上）

config.json其它字段说明请参看<u>附录：11.4 config.json说明</u>

> 配置好的config.json如下：

```log
{
        "sealEngine": "PBFT",
        "systemproxyaddress":"0x0",
        "listenip":"0.0.0.0",
        "cryptomod":"0",
        "ssl":"0",
        "rpcport": "8545",
        "p2pport": "30303",
        "channelPort": "30304",
        "rpcsslport":"-1",
        "wallet":"/mydata/nodedata-1/keys.info",
        "keystoredir":"/mydata/nodedata-1/keystore/",
        "datadir":"/mydata/nodedata-1/data/",
        "vm":"interpreter",
        "networkid":"12345",
        "logverbosity":"4",
        "coverlog":"OFF",
        "eventlog":"ON",
        "statlog":"OFF",
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
                "Nodeid":"2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878",
                "Nodedesc": "node1",
                "Agencyinfo": "node1",
                "Peerip": "0.0.0.0",
                "Identitytype": 1,
                "Port":30303,
                "Idx":0
                }
        ]
}
```

#### 2.5.3 配置log.conf（日志配置文件）

log.conf中配置节点日志生成的格式和路径。

```shell
vim /mydata/nodedata-1/log.conf 
```

> 主要配置日志文件的生成路径，配置好的log.conf 如下：

```log
* GLOBAL:
    ENABLED                 =   true
    TO_FILE                 =   true
    TO_STANDARD_OUTPUT      =   false
    FORMAT                  =   "%level|%datetime{%Y-%M-%d %H:%m:%s:%g}|%msg"
    FILENAME                =   "/mydata/nodedata-1/log/log_%datetime{%Y%M%d%H}.log"
    MILLISECONDS_WIDTH      =   3
    PERFORMANCE_TRACKING    =   false
    MAX_LOG_FILE_SIZE       =   209715200 ## 200MB - Comment starts with two hashes (##)
    LOG_FLUSH_THRESHOLD     =   100  ## Flush after every 100 logs

* TRACE:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-1/log/trace_log_%datetime{%Y%M%d%H}.log"

* DEBUG:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-1/log/debug_log_%datetime{%Y%M%d%H}.log"

* FATAL:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-1/log/fatal_log_%datetime{%Y%M%d%H}.log"

* ERROR:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-1/log/error_log_%datetime{%Y%M%d%H}.log"

* WARNING:
     ENABLED                 =   true
     FILENAME                =   "/mydata/nodedata-1/log/warn_log_%datetime{%Y%M%d%H}.log"

* INFO:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-1/log/info_log_%datetime{%Y%M%d%H}.log"

* VERBOSE:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-1/log/verbose_log_%datetime{%Y%M%d%H}.log"
```

log.conf其它字段说明请参看<u>附录：11.5 log.conf说明</u>

### 2.6 启动创世节点

节点的启动依赖下列文件，在启动前，请确认文件已经正确的配置：

- 证书文件（/mydata/nodedata-1/data）：ca.crt、server.crt、server.key
- 节点身份文件（/mydata/nodedata-1/data）：network.rlp、network.rlp.pub
- 配置文件（/mydata/nodedata-1/）：genesis.json、config.json、log.conf

> 启动节点

```shell
cd /mydata/nodedata-1/
chmod +x *.sh
./start.sh
#若需要退出节点
#./stop.sh
```

> 或手动启动

```shell
cd /mydata/nodedata-1/
fisco-bcos --genesis ./genesis.json --config ./config.json & #启动区块链节点
tail -f log/info* |grep ++++  #查看日志输出
#若需要退出节点
#ps -ef |grep fisco-bcos #查看进程号
#kill -9 13432 #13432是查看到的进程号
```

> 几秒后可看到不断刷出打包信息。

```log
INFO|2017-12-12 17:52:16:877|+++++++++++++++++++++++++++ Generating seal ondcae019af78cf04e17ad908ec142ca4e25d8da14791bda50a0eeea782ebf3731#1tx:0,maxtx:1000,tq.num=0time:1513072336877
INFO|2017-12-12 17:52:17:887|+++++++++++++++++++++++++++ Generating seal on3fef9b23b0733ac47fe5385072f80fc036b7517abae0a3e7762739cc66bc7dca#1tx:0,maxtx:1000,tq.num=0time:1513072337887
INFO|2017-12-12 17:52:18:897|+++++++++++++++++++++++++++ Generating seal onb5b38c7a380b13b2e46fecbdca0fac5473f4cbc054190e90b8bd4831faac4521#1tx:0,maxtx:1000,tq.num=0time:1513072338897
INFO|2017-12-12 17:52:19:907|+++++++++++++++++++++++++++ Generating seal on3530ff04adddd30508a4cb7421c8f3ad6421ca6ac3bb5f81fb4880fd72c57a8c#1tx:0,maxtx:1000,tq.num=0time:1513072339907
```

### 2.7 验证节点启动

#### 2.7.1 验证进程

```shell
ps -ef |grep fisco-bcos
```

> 看到进程启动

```log
app 19390     1  1 17:52 ?        00:00:05 fisco-bcos --genesis /mydata/nodedata-1/genesis.json --config /mydata/nodedata-1/config.json
```

#### 2.7.2 查看日志输出

> 执行命令，查看打包信息。

```shell
tail -f /mydata/nodedata-1/log/info* |grep ++++  #查看日志输出
```

> 可看到不断刷出打包信息。

```log
INFO|2017-12-12 17:52:16:877|+++++++++++++++++++++++++++ Generating seal ondcae019af78cf04e17ad908ec142ca4e25d8da14791bda50a0eeea782ebf3731#1tx:0,maxtx:1000,tq.num=0time:1513072336877
INFO|2017-12-12 17:52:17:887|+++++++++++++++++++++++++++ Generating seal on3fef9b23b0733ac47fe5385072f80fc036b7517abae0a3e7762739cc66bc7dca#1tx:0,maxtx:1000,tq.num=0time:1513072337887
INFO|2017-12-12 17:52:18:897|+++++++++++++++++++++++++++ Generating seal onb5b38c7a380b13b2e46fecbdca0fac5473f4cbc054190e90b8bd4831faac4521#1tx:0,maxtx:1000,tq.num=0time:1513072338897
INFO|2017-12-12 17:52:19:907|+++++++++++++++++++++++++++ Generating seal on3530ff04adddd30508a4cb7421c8f3ad6421ca6ac3bb5f81fb4880fd72c57a8c#1tx:0,maxtx:1000,tq.num=0time:1513072339907
```

若上述都正确输出，则表示创世节点已经正确启动！



## 第三章 部署合约、调用合约

智能合约是部署在区块链上的应用。开发者可根据自身需求在区块链上部署各种智能合约。

智能合约通过solidity语言实现，用fisco-solc进行编译。本章以HelloWorld.sol智能合约为例介绍智能合约的部署和调用。

### 3.1 配置

> 安装依赖环境

```shell
cd /mydata/FISCO-BCOS/web3lib
cnpm install #安装nodejs依赖, 在执行nodejs脚本之前该目录下面需要执行一次, 如果已经执行过, 则忽略。
```

> 切换到部署合约的目录下

```shell
cd /mydata/FISCO-BCOS/tool
cnpm install #该命令在该目录执行一次即可, 如果之前已经执行过一次, 则忽略。
```

> 设置区块链节点RPC端口

```shell
vim ../web3lib/config.js
```

> 仅需将proxy指向区块链节点的RPC端口即可。RPC端口在节点的config.json中查看（参考：<u>2.5.2 配置config.json（节点配置文件）</u>）。

```javascript
var proxy="http://127.0.0.1:8545";
```

### 3.2 部署合约

#### 3.2.1 实现合约

```shell
cd /mydata/FISCO-BCOS/tool
vim HelloWorld.sol
```

> HelloWorld.sol的实现如下：

```solidity
pragma solidity ^0.4.2;
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

#### 3.2.2 编译+部署合约

请先确认config.js已经正确指向区块链节点的RPC端口，且相应的区块链节点已经启动。

> 直接使用自动编译部署程序deploy.js，自动编译和部署合约：

```shell
babel-node deploy.js HelloWorld #注意后面HelloWorld后面没有".sol"
```

> 输出，可看到合约地址，部署成功：

```log
deploy.js  ........................Start........................
Soc File :HelloWorld
HelloWorldcomplie success！
send transaction success: 0xa8c1aeed8e85cc0308341081925d3dab80da394f6b22c76dc0e855c8735da481
HelloWorldcontract address 0xa807685dd3cf6374ee56963d3d95065f6f056372
HelloWorld deploy success!
```

### 3.3 调用合约

#### 3.3.1 编写合约调用程序

> 用nodejs实现，具体实现方法请直接看demoHelloWorld.js源码

```shell
vim demoHelloWorld.js
```

#### 3.3.2 调用合约 

> 执行合约调用程序

```shell
babel-node demoHelloWorld.js
```

> 可看到合约调用成功

```log
{ HttpProvider: 'http://127.0.0.1:8545',
  Ouputpath: './output/',
  privKey: 'bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd',
  account: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3' }
HelloWorldcontract address:0xa807685dd3cf6374ee56963d3d95065f6f056372
HelloWorld contract get function call first :Hi,Welcome!
send transaction success: 0x6463e0ea9db6c4aff1e3fc14d9bdb86b29306def73e6d951913a522347526435
HelloWorld contract set function call , (transaction hash ：0x6463e0ea9db6c4aff1e3fc14d9bdb86b29306def73e6d951913a522347526435)
HelloWorld contract get function call again :HelloWorld!
```



## 第四章 部署系统合约

系统合约是 FISCO BCOS 区块链的重要设计思路之一，也是控制网络节点加入和退出的重要方式，每条区块链仅需部署一次系统合约。系统合约的详细介绍，请参看<u>附录：11.7 系统合约介绍</u>

### 4.1 配置

> 切换到部署系统合约的目录下

```shell
cd /mydata/FISCO-BCOS/systemcontractv2
```

> 安装依赖环境

```shell
cnpm install
```

> 设置区块链节点RPC端口

```shell
vim ../web3lib/config.js
```

> 仅需将proxy指向区块链节点的RPC端口即可。RPC端口在节点的config.json中查看（参考：<u>2.5.2 配置config.json（节点配置文件）</u>）。

```javascript
var proxy="http://127.0.0.1:8545";
```

### 4.2 部署系统合约

> 直接运行deploy.js部署系统合约。注意，此deploy.js与tool目录的是不同的两个文件。

```shell
babel-node deploy.js 
```

> 部署成功，输出合约路由表。

```log
#.....省略很多编译warn信息.....
SystemProxycomplie success！
send transaction success: 0x56c9e34cf559b3a9aead8694a7bda7e6b5ea4af855d5ec6ef08fadf494accf08
SystemProxycontract address 0x210a7d467c3c43307f11eda35f387be456334fed
AuthorityFiltercomplie success！
send transaction success: 0x112b6ac9a61197920b6cbe1a71d8f8d4a6c0c11cd0ae3c1107d1626691bf1c35
AuthorityFiltercontract address 0x297e397a7534464a4e7448c224aae52f9614af77
Groupcomplie success！
send transaction success: 0x1be1fb1393e3a3f37f197188ea99de0a5dd1828cc9fc24638f678528f0e30c23
Groupcontract address 0xed0a1b82649bd22d947e5c3ca0b779aac8ee5edc
TransactionFilterChaincomplie success！
send transaction success: 0x704a614b10c5682c44a11e48305bad40a0809d1fc9e178ddec1218c52e7bc9d0
TransactionFilterChaincontract address 0x60d34569bc861b40a7552f89a198a89d8c99075e
CAActioncomplie success！
send transaction success: 0x75f890777b586060c3f94dc3396f5ad86c3e10f2eb9b8350bbc838beecf50ece
CAActioncontract address 0x6fbf3bef2f757c01e0c43fc1364207c1e8a19d08
NodeActioncomplie success！
send transaction success: 0x9d26304258608de5bf1c47ecb9b2ac79f5323e6b74cef7eddef1fb9893d5e98e
NodeActioncontract address 0xb5e0d2c6f1b9f40ea21fa8698a28d1662e8afa3e
send transaction success: 0x03c86b3dcd3d564a00a709b7cd6f1902cd4111cc30c71c62728deadc6e8d7511
ConfigActioncomplie success！
send transaction success: 0xa49205ff3ad697fda75019cb2bbf541a120c146b973f8c5d50b761fd5024b795
ConfigActioncontract address 0xb1e6d5f95c9cb39a9e4e3071b3765e08c30ea281
FileInfoManagercomplie success！
send transaction success: 0x10f8b3fa9efb129bb321cba26019f363aad1b1a162b9347f6638bf6d94de7c32
FileInfoManagercontract address 0x0c2422186429e81911b729fd25e3afa76231f9c7
FileServerManagercomplie success！
send transaction success: 0xd156ccb19fa9dc5313c124933a458b141a20cc2ce01334ce030940e9f907cb84
FileServerManagercontract address 0xb33485375d208a23e897144b6244e20d9c1e83d9
ConsensusControlMgrcomplie success！
send transaction success: 0xcfe0a0fc77910c127d31470e38707dfe70a7fb699abce3e9261ef55a4e50997c
ConsensusControlMgrcontract address 0x3414ef5c15848a07538a2fac57c09a549036b5e3
ContractAbiMgrcomplie success！
send transaction success: 0xc1e2c4e837edda0e215ca06aaa02eecb3a954acfafd498a049b7cf6cee410f5c
ContractAbiMgrcontract address 0xac919b98301804575bd2dc676330aa8f2637f7d5
#......省略若干行...........
send transaction success: 0x2a0f5f9eeb069fe61289e8c95cb4b6cf026859cd20e38e8e47c0788609d8aad1
send transaction success: 0xcd51375a90056e92a52869c63ec153f05722ab8ee56b5ae242b9114c4838e32b
send transaction success: 0x250c0fc5f34bfb73a6bc2a858b64287aa859f12651a3798d46d7269e7305bf6f
send transaction success: 0xff3aeddb55c9ac6868df0cde04466431c7286d93baa80e3826522a2a8ad9681a
send transaction success: 0x71d484aa4a90068e409a11800e9ae5df6143dd59e0cc21a06c1a0bbba4617307
send transaction success: 0x8bd093d44c99817685d21053ed468afa8f216bc12a1c3f5fe33e5cd2bfd045c0
send transaction success: 0x5b9acaab5252bf43b111d24c7ff3adac0121c58e59636f26dbe2ca71dd4af47d
send transaction success: 0x34fb9226604143bec959d65edb0fc4a4c5b1fe5ef6eead679648c7295099ac8b
send transaction success: 0xe9cac7507d94e8759fcc945e6225c873a422a78674a79b24712ad76757472018
register TransactionFilterChain.....
send transaction success: 0x814ef74b4123f416a0b602f25cc4d49e038826cf50a6c4fbc6a402b9a363a8d9
register ConfigAction.....
send transaction success: 0xdb4aaa57b01ca7b1547324bcbeeaaeaa591bf9962ea921d5ce8845b566460776
register NodeAction.....
send transaction success: 0x2c7f43c84e52e99178a92e6a63fb69b5dedf4993f7cbb62608c74b6256241b39
register CAAction.....
send transaction success: 0xcb792f508ca890296055a53c360d68c3c46f8bf801ce73818557e406cbd0618c
register ContractAbiMgr.....
send transaction success: 0xfdc0dd551eada0648919a4c9c5ffa182d042099d73fa802cf803bebf5068aec1
register ConsensusControlMgr.....
send transaction success: 0x7f6d95e6a49a1c1de257415545afb0ec7fdd5607c427006fe14a7750246b9d75
register FileInfoManager.....
send transaction success: 0xc5e16814085000043d28a6d814d6fa351db1cd34f7d950e5e794d28e4ff0da49
register FileServerManager.....
send transaction success: 0xbbbf66ab4acd7b5484dce365d927293b43b3904cd14063a7f60839941a0479a0
SystemProxy address :0x9fe9648f723bff29f940b8c18fedcc9c7ed2b91f
-----------------SystemProxy route ----------------------
get 0xb33485375d208a23e897144b6244e20d9c1e83d9
0 )TransactionFilterChain=>0x60d34569bc861b40a7552f89a198a89d8c99075e,false,250
1 )ConfigAction=>0xb1e6d5f95c9cb39a9e4e3071b3765e08c30ea281,false,251
2 )NodeAction=>0xb5e0d2c6f1b9f40ea21fa8698a28d1662e8afa3e,false,252
3 )CAAction=>0x6fbf3bef2f757c01e0c43fc1364207c1e8a19d08,false,253
4 )ContractAbiMgr=>0xac919b98301804575bd2dc676330aa8f2637f7d5,false,254
5 )ConsensusControlMgr=>0x3414ef5c15848a07538a2fac57c09a549036b5e3,false,255
6 )FileInfoManager=>0x0c2422186429e81911b729fd25e3afa76231f9c7,false,256
7 )FileServerManager=>0xb33485375d208a23e897144b6244e20d9c1e83d9,false,257
-----------------SystemProxy route ----------------------
```

> 上述输出内容中，重要的是系统代理合约地址，即SystemProxy合约地址。如：

```log
SystemProxycontract address 0x210a7d467c3c43307f11eda35f387be456334fed
```

### 4.3 配置系统代理合约地址

系统代理合约，是所有系统合约的路由，通过配置系统代理合约地址（SystemProxy），才能正确调用系统合约。给个区块链节点都应配置系统代理合约地址，才能正确调用系统合约。

> 修改所有区块链节点的config.json。将systemproxyaddress字段配置为，上述步骤输出的SystemProxy合约地址配置。

```shell
vim /mydata/nodedata-1/config.json
```

> 配置后，config.json中的systemproxyaddress字段如下：

```log
"systemproxyaddress":"0x210a7d467c3c43307f11eda35f387be456334fed",
```

> 重启被配置的节点：

```shell
cd /mydata/nodedata-1/
chmod +x *.sh
./stop.sh
./start.sh #执行此步骤后不断刷出打包信息，表明重启成功
```

自此，系统合约生效，为配置多个节点的区块链做好了准备。系统合约的详细介绍，请参看<u>附录：11.7 系统合约介绍</u>



## 第五章 创建普通节点

普通节点是区块链中除创世节点外的其它节点。

同一条链中的所有节点共用相同的genesis.json和相同的根证书ca.crt。

创建普通节点的步骤与创建创世节点的步骤类似。普通节点不需要再修改genesis.json和ca.crt，直接复制创世节点的genesis.json和ca.crt到节点的相应路径下即可。

### 5.1 创建节点环境

> 假定节点目录为/mydata/nodedata-2/，创建节点环境如下：

```shell
#创建目录结构
mkdir -p /mydata/nodedata-2/
mkdir -p /mydata/nodedata-2/data/ #存放节点的各种文件
mkdir -p /mydata/nodedata-2/log/ #存放日志
mkdir -p /mydata/nodedata-2/keystore/ #存放账户秘钥

#拷贝创世节点相关文件
cd /mydata/nodedata-1/ 
cp genesis.json config.json log.conf start.sh stop.sh /mydata/nodedata-2/
```

### 5.2  配置节点身份NodeId

同样需要为普通节点分配NodeId。NodeId唯一标识了区块链中的某个节点，在节点启动前必须进行配置。

#### 5.2.1 配置cryptomod.json文件

> 在cryptomod.json文件中配置NodeId生成路径：

```shell
cd /mydata/FISCO-BCOS/ 
vim cryptomod.json
```

> 此处将rlpcreatepath配置为/mydata/nodedata-2/data/network.rlp。配置后的cryptomod.json如下：

```log
{
	"cryptomod":"0",
	"rlpcreatepath":"/mydata/nodedata-2/data/network.rlp",
	"datakeycreatepath":"",
	"keycenterurl":"",
	"superkey":""
}
```

cryptomod.json其它字段说明请参看<u>附录：11.2 cryptomod.json说明</u>

#### 5.2.2 生成节点身份文件

> 用上述修改好的cryptomod.json文件生成节点身份文件，生成路径为cryptomod.json中配置的路径。

```shell
fisco-bcos --gennetworkrlp  cryptomod.json #需要一段时间
ls /mydata/nodedata-2/data/
```

> 可看到节点身份文件（network.rlp和network.rlp.pub）生成到了/mydata/nodedata-2/data/下。
>
> 其中network.rlp是节点身份的私钥二进制文件。network.rlp.pub是节点身份的NodeId文件。

```shell
network.rlp  network.rlp.pub
```

#### 5.2.3 配置节点NodeId

（1）查看NodeId

```shell
cd /mydata/nodedata-2/data/
cat network.rlp.pub
```

> 得到如下类似的NodeId

```log
838a187e32e72e3889330c2591536d20868f34691f1822fbcd43cb345ef437c7a6568170955802db2bf1ee84271bc9cba64fba87fba84e0dba03e5a05de88a2c
```

（2）修改config.json

> 在NodeextraInfo字段中追加新的节点信息。

```shell
vim /mydata/nodedata-2/config.json
```

> 追加后，config.json中的NodeextraInfo字段如下。新追加的内容要与现有的内容有区别，Nodeid为刚生成的NodeId，Port是p2pport端口，Idx为上一条记录加1。注意，若新部署的节点在另外一台机器上，则需要将NodeextraInfo中其它节点的Peerip改成其它节点对应的机器IP，让新部署的节点能够找到其它机器上的节点。

```log
"NodeextraInfo":[
    {
	    "Nodeid":"2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878",
	    "Nodedesc": "node1",
	    "Agencyinfo": "node1",
	    "Peerip": "0.0.0.0",
	    "Identitytype": 1,
	    "Port":30303,
	    "Idx":0
    },
    {
	    "Nodeid":"838a187e32e72e3889330c2591536d20868f34691f1822fbcd43cb345ef437c7a6568170955802db2bf1ee84271bc9cba64fba87fba84e0dba03e5a05de88a2c",
	    "Nodedesc": "node2",
	    "Agencyinfo": "node2",
	    "Peerip": "0.0.0.0",
	    "Identitytype": 1,
	    "Port":30403,
	    "Idx":1
    }
]
```

### 5.3 配置证书

区块链节点间的通信需要证书的认证。在节点运行前，需要为节点配置证书。其中ca.crt是整个区块链的根证书公钥，所有节点共用。

#### 5.3.1 配置根证书

> 一条链上的所有区块链节点都共用一个根证书ca.crt。直接从创世节点目录拷贝。

```shell
cp /mydata/nodedata-1/data/ca.crt /mydata/nodedata-2/data/
```

#### 5.3.2 生成节点证书

> 生成普通节点证书（server.key、server.crt）的步骤与创世节点相同。在生成前，需要把生成脚本和根证书私钥（ca.key）配置到data目录下。

```shell
cd /mydata/nodedata-2/data/
cp /mydata/nodedata-1/data/ca.key . #假设根证书私钥放在node1的data目录下
cp /mydata/nodedata-1/data/genkey.sh .
```

> 用根证书公私钥生成节点证书公私钥（server.key、server.crt）

```shell
./genkey.sh server ./ca.key ./ca.crt 365 #生成server证书有效期为365天
```

>  生成后，此时目录下应存在下述文件：

```log
ca.crt  network.rlp  network.rlp.pub  server.crt  server.key
```

> 注意，ca.key在生成了节点证书后，应立即删除。

### 5.4 配置相关配置文件

普通节点的启动依赖三个配置文件：

- 创世块文件：genesis.json（与创世节点相同，直接复制，无需修改）
- 节点配置文件：config.json
- 日志配置文件：log.conf

#### 5.4.1 配置config.json（节点配置文件）

config.json中可配置节点的各种信息，包括网络地址，文件目录，节点身份等。

```shell
vim /mydata/nodedata-2/config.json
```

> 配置本节点的信息，主要修改字段，注意与NodeextraInfo中的内容对应：
>
> - 网络连接相关：listenip、rpcport、p2pport、channelPort
> - 目录相关：wallet、keystoredir、datadir、logconf
> - 节点身份相关：NodeextraInfo中的Nodeid、Nodedesc、Agencyinfo、Peerip、Identitytype、Port、Idx（与网络连接相关对应上）

config.json其它字段说明请参看<u>附录：11.4 config.json说明</u>

> 配置好的config.json如下：

```log
{
        "sealEngine": "PBFT",
        "systemproxyaddress":"0x210a7d467c3c43307f11eda35f387be456334fed",
        "listenip":"0.0.0.0",
        "cryptomod":"0",
        "ssl":"0",
        "rpcport": "8546",
        "p2pport": "30403",
        "channelPort": "30404",
        "wallet":"/mydata/nodedata-2/keys.info",
        "keystoredir":"/mydata/nodedata-2/keystore/",
        "datadir":"/mydata/nodedata-2/data/",
        "vm":"interpreter",
        "networkid":"12345",
        "logverbosity":"4",
        "coverlog":"OFF",
        "eventlog":"ON",
        "statlog":"OFF",
        "logconf":"/mydata/nodedata-2/log.conf",
        "params": {
                "accountStartNonce": "0x0",
                "maximumExtraDataSize": "0x0",
                "tieBreakingGas": false,
                "blockReward": "0x0",
                "networkID" : "0x0"
        },
        "NodeextraInfo":[
                {
                "Nodeid":"2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878",
                "Nodedesc": "node1",
                "Agencyinfo": "node1",
                "Peerip": "0.0.0.0",
                "Identitytype": 1,
                "Port":30303,
                "Idx":0
                },
                {
                "Nodeid":"838a187e32e72e3889330c2591536d20868f34691f1822fbcd43cb345ef437c7a6568170955802db2bf1ee84271bc9cba64fba87fba84e0dba03e5a05de88a2c",
                "Nodedesc": "node2",
                "Agencyinfo": "node2",
                "Peerip": "0.0.0.0",
                "Identitytype": 1,
                "Port":30403,
                "Idx":1
                }
        ]
}
```

#### 5.4.2 配置log.conf（日志配置文件）

log.conf中配置节点日志生成的格式和路径。

```shell
vim /mydata/nodedata-2/log.conf 
```

> 主要配置日志文件的生成路径，配置好的log.conf 如下：

```log
* GLOBAL:
    ENABLED                 =   true
    TO_FILE                 =   true
    TO_STANDARD_OUTPUT      =   false
    FORMAT                  =   "%level|%datetime{%Y-%M-%d %H:%m:%s:%g}|%msg"
    FILENAME                =   "/mydata/nodedata-2/log/log_%datetime{%Y%M%d%H}.log"
    MILLISECONDS_WIDTH      =   3
    PERFORMANCE_TRACKING    =   false
    MAX_LOG_FILE_SIZE       =   209715200 ## 200MB - Comment starts with two hashes (##)
    LOG_FLUSH_THRESHOLD     =   100  ## Flush after every 100 logs

* TRACE:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-2/log/trace_log_%datetime{%Y%M%d%H}.log"

* DEBUG:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-2/log/debug_log_%datetime{%Y%M%d%H}.log"

* FATAL:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-2/log/fatal_log_%datetime{%Y%M%d%H}.log"

* ERROR:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-2/log/error_log_%datetime{%Y%M%d%H}.log"

* WARNING:
     ENABLED                 =   true
     FILENAME                =   "/mydata/nodedata-2/log/warn_log_%datetime{%Y%M%d%H}.log"

* INFO:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-2/log/info_log_%datetime{%Y%M%d%H}.log"

* VERBOSE:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-2/log/verbose_log_%datetime{%Y%M%d%H}.log"
```

log.conf其它字段说明请参看<u>附录：11.5 log.conf说明</u>

### 5.5 启动节点

节点的启动依赖下列文件，在启动前，请确认文件已经正确的配置：

- 证书文件（/mydata/nodedata-2/data）：ca.crt、network.rlp、network.rlp.pub、server.crt、server.key
- 配置文件（/mydata/nodedata-2/）：genesis.json、config.json、log.conf

> 启动节点，此时节点未被注册到区块链中，启动时只能看到进程，不能刷出打包信息。要让此节点正确的运行，请进入<u>第六章 多节点组网</u> 。

```shell
cd /mydata/nodedata-2/
chmod +x *.sh
./start.sh #此时节点未被注册到区块链中，等待10秒，不会刷出打包信息
ctrl-c 退出
ps -ef |grep fisco-bcos #可查看到节点进程存在
```

> 可看到进程已经在运行

```log
app  9656     1  4 16:10 ?        00:00:01 fisco-bcos --genesis /mydata/nodedata-2/genesis.json --config /mydata/nodedata-2/config.json
```

> 关闭节点，待注册后再重启

```shell
./stop.sh 
```



## 第六章 多节点组网

FISCO BCOS区块链中的节点，只有被注册过，表示加入了网络，才能正常的运行。

> 多节点组网依赖系统合约，在进行多节点组网前，请确认：
>
> （1）系统合约已经被正确的部署。
>
> （2）所有节点的config.json的systemproxyaddress字段已经配置了相应的系统代理合约地址。
>
> （3）节点在配置了systemproxyaddress字段后，已经重启使得系统合约生效。
>
> （4）/mydata/FISCO-BCOS/systemcontractv2/下的config.js已经正确的配置了节点的RPC端口。

### 6.1 注册节点

所有的节点注册流程都相同。在注册节点时，**需先注册创世节点，再注册普通节点**。

#### 6.1.1 编写注册配置文件

> 以注册创世节点为例。

```shell
cd /mydata/FISCO-BCOS/systemcontractv2/
vim node1.json
```

> 内容需要与节点config.json中NodeextraInfo字段的内容对应。若在不同机器上组网时，ip填写的是外网ip。编写好的注册配置文件如下：

```json
{    "id":"2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878",
    "ip":"127.0.0.1",
    "port":30303,
    "category":1,
    "desc":"node1",
    "CAhash":"",
    "agencyinfo":"node1",
    "idx":0
}
```

#### 6.1.2 注册

在注册前，请确认已注册的所有节点，都已经启动。

> 用编写好的注册配置文件（node1.json）注册节点。（创世节点在被注册时，应启动起来）

```shell
babel-node tool.js NodeAction registerNode node1.json
```

> 可看到注册信息

```log
{ HttpProvider: 'http://127.0.0.1:8545',
  Ouputpath: './output/',
  privKey: 'bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd',
  account: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3' }
Soc File :NodeAction
Func :registerNode
SystemProxy address 0x210a7d467c3c43307f11eda35f387be456334fed
node.json=node1.json
NodeAction address 0xcc46c245e6cca918d43bf939bbb10a8c0988548f
send transaction success: 0x9665417c16b636a2a83e13e82d1674e4db72943bae2095cb030773f0a0ba1eef
```

#### 6.1.3 查看入网情况

> 查看节点是否已经在节点列表中

```shell
babel-node tool.js NodeAction all
```

> 可看到被注册的节点信息，节点已经加入了网络

```log
{ HttpProvider: 'http://127.0.0.1:8545',
  Ouputpath: './output/',
  privKey: 'bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd',
  account: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3' }
Soc File :NodeAction
Func :all
SystemProxy address 0x210a7d467c3c43307f11eda35f387be456334fed
NodeAction address 0xcc46c245e6cca918d43bf939bbb10a8c0988548f
NodeIdsLength= 1
----------node 0---------
id=2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878
ip=127.0.0.1
port=30501
category=1
desc=node1
CAhash=
agencyinfo=node1
blocknumber=427
Idx=0
```

#### 6.1.4 注册更多的节点

在注册更多的节点前，**请确认创世节点是第一个被注册的**，并且网络中所有被注册过的节点都已经启动。

> 重复上述过程，注册更多节点。

```shell
vim node2.json #修改内容与config.json内容对应
babel-node tool.js NodeAction registerNode node2.json
cd /mydata/nodedata-2/
./start.sh #将被注册的节点启动起来，此时节点已经被注册，可刷出打包信息
```

>再次查看入网情况：

```log
cd /mydata/FISCO-BCOS/systemcontractv2/
babel-node tool.js NodeAction all
```

> 可看到输出了节点信息（node1），节点加入网络

```log
{ HttpProvider: 'http://127.0.0.1:8545',
  Ouputpath: './output/',
  privKey: 'bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd',
  account: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3' }
Soc File :NodeAction
Func :all
SystemProxy address 0x210a7d467c3c43307f11eda35f387be456334fed
NodeAction address 0xcc46c245e6cca918d43bf939bbb10a8c0988548f
NodeIdsLength= 2
----------node 0---------
id=2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878
ip=127.0.0.1
port=30501
category=1
desc=node1
CAhash=
agencyinfo=node1
blocknumber=427
Idx=0
----------node 1---------
id=838a187e32e72e3889330c2591536d20868f34691f1822fbcd43cb345ef437c7a6568170955802db2bf1ee84271bc9cba64fba87fba84e0dba03e5a05de88a2c
ip=127.0.0.1
port=30502
category=1
desc=node2
CAhash=
agencyinfo=node2
blocknumber=429
Idx=1
```

### 6.2 查看节点连接

>执行脚本

```shell
cd /mydata/FISCO-BCOS/systemcontractv2/
babel-node monitor.js
```

> 可看到不断刷出连接信息和块高。已连接节点数表示的是被查询的节点与其它节点连接的个数，即被注册的节点总数减1。此例子中，网络中有2个节点。被查询的节点是创世节点，与创世节点连接的节点只有一个，所以已连接节点数为1。

```log
--------------------------------------------------------------
current blocknumber 429
the number of connected nodes：0
...........Node 0.........
NodeId:838a187e32e72e3889330c2591536d20868f34691f1822fbcd43cb345ef437c7a6568170955802db2bf1ee84271bc9cba64fba87fba84e0dba03e5a05de88a2c
Host:127.0.0.1:30403
--------------------------------------------------------------
```

### 6.3 节点退出网络

> 要让某节点退出网络，需执行以下脚本。执行时，指定相应节点的注册配置文件。此处让node2退出为例。

```shell
babel-node tool.js NodeAction cancelNode node2.json
```

> 执行后有如下输出：

```log
{ HttpProvider: 'http://127.0.0.1:8545',
  Ouputpath: './output/',
  privKey: 'bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd',
  account: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3' }
Soc File :NodeAction
Func :cancelNode
SystemProxy address 0x210a7d467c3c43307f11eda35f387be456334fed
node.json=node2.json
NodeAction address 0xcc46c245e6cca918d43bf939bbb10a8c0988548f
send transaction success: 0x031f29f9fe3b607277d96bcbe6613dd4d2781772ebd0c810a31a8d680c0c49c3
```

>查看节点连接，看不到相应节点的信息，表示节点已经退出了网络。

```log
babel-node tool.js NodeAction all
#......节点输出信息......
{ HttpProvider: 'http://127.0.0.1:8545',
  Ouputpath: './output/',
  privKey: 'bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd',
  account: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3' }
Soc File :NodeAction
Func :all
SystemProxy address 0x210a7d467c3c43307f11eda35f387be456334fed
NodeAction address 0xcc46c245e6cca918d43bf939bbb10a8c0988548f
NodeIdsLength= 1
----------node 0---------
id=2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878
ip=127.0.0.1
port=30501
category=1
desc=node1
CAhash=
agencyinfo=node1
blocknumber=427
Idx=0
```


## 第七章 机构证书准入

FISCO BCOS提供了证书准入的功能。在节点加入网络后，节点间是否能够通信，还可通过证书进行控制。在FISCO BCOS中，节点的证书代表了此节点属于某个机构。FISCO BCOS区块链中的管理者，可以通过配置机构的证书，控制相应证书的节点是否能够与其它节点通信。

> 机构证书的准入依赖系统合约，在进行机构证书准入操作前，再次请确认：
>
> （1）系统合约已经被正确的部署。
>
> （2）所有节点的config.json的systemproxyaddress字段已经配置了相应的系统代理合约地址。
>
> （3）节点在配置了systemproxyaddress字段后，已经重启使得系统合约生效。
>
> （4）/mydata/FISCO-BCOS/systemcontractv2/下的config.js已经正确的配置了节点的RPC端口。

### 7.1 配置节点证书

节点的证书存放目录在节点文件目录的data文件夹下。包括：

- ca.crt：根证书公钥，整条区块链共用。
- ca.key：根证书私钥，私钥应保密，仅在生成节点证书公私钥时使用。
- server.crt：节点证书的公钥。
- server.key：节点证书的私钥，私钥应保密。

证书文件应严格按照上述命名方法命名。

FISCO BCOS通过授权某节点对应的公钥server.crt，控制此节点是否能够与其它节点正常通信。

具体配置过程请参考<u>2.4 配置证书</u> 。在配置节点的步骤中已涵盖相关证书的配置方法，若按照步骤部署了节点，则可直接进入下一步骤。

### 7.2 开启所有节点的SSL验证功能

在进行节点证书授权管理前，需开启区块链上**每个节点**的SSL验证功能。

> 此处以创世节点为例，其它节点也应采用相同的操作。

```shell
cd /mydata/nodedata-1/
vim config.json
```

> 将ssl字段置为1，效果如下。

```json
"ssl":"1",
```

> 修改完成后重启节点，使其生效。

```shell
./stop.sh
./start.sh
```

> 其它节点也采用相同的操作，开启SSL验证功能。

**注意：必须所有的节点都开启ssl功能，才能继续下一步骤。**

### 7.3 配置机构证书信息

将节点的证书写入系统合约，为接下来的证书准入做准备。每张证书都应该写入系统合约中。节点的证书若不写入系统合约，相应的节点将不允许通信。

#### 7.3.1 获取证书序列号

> 获取server.crt的序列号

```shell
cd /mydata/nodedata-2/data
openssl x509 -noout -in server.crt -serial
```

> 可得到证书序列号

```log
serial=8A4B2CDE94348D22
```

#### 7.3.2 编写证书准入状态文件

> 在systemcontractv2目录下编写。

```shell
cd /mydata/FISCO-BCOS/systemcontractv2
vim ca.json
```

> 将序列号填入hash字段。配置status，0表示不可用，1表示可用。其它字段默认即可。如下，让node2的证书可用。即status置1。

```json
{
        "hash" : "8A4B2CDE94348D22",
        "status" : 1,
        "pubkey":"",
        "orgname":"",
        "notbefore":20170223,
        "notafter":20180223,
        "whitelist":"",
        "blacklist":""
}
```

证书准入状态文件其它字段说明请参考<u>11.6 证书准入状态文件说明</u>

#### 7.3.3 将证书准入状态写入系统合约

> 执行以下命令，指定证书准入状态文件ca.json，将证书状态写入系统合约。

```shell
babel-node tool.js CAAction update ca.json
```

### 7.4 设置证书验证开关

证书验证开关能够控制是否采用证书准入机制。开启后，将根据系统合约里的证书状态（status）控制节点间是否能够通信。不在系统合约中的证书对应的节点，将不允许通信。

> 在打开此开关前，请确认：
>
> （1）所有的节点都正确的配置了相应机构的证书（即server.key、server.crt）。
>
> （2）所有节点的SSL验证已经打开。（标志位已经设置，设置后节点已经重启）。
>
> （3）所有机构的证书信息都已经配置入系统合约。
>
> 上述条件未达到，会出现节点无法连接，节点无法共识，合约操作无法进行的情况。若出现上述情况，请先关闭所有节点的SSL验证功能，做了正确的配置后，再打开SSL功能。

#### 7.4.1 开启全局开关

> 执行命令，CAVerify设置为true

```shell
babel-node tool.js ConfigAction set CAVerify true
```

> 查看开关是否生效

```shell
babel-node tool.js ConfigAction get CAVerify
```

> 输出true，表示开关已打开

```log
CAVerify=true,29
```

#### 7.4.2 关闭全局开关

开关关闭后，节点间的通信不再验证证书。

> 执行命令，CAVerify设置为false

```shell
babel-node tool.js ConfigAction set CAVerify false
```

### 7.5 修改节点证书准入状态

已经写入系统合约的证书状态，允许修改（可用/不可用）

#### 7.5.1 修改证书准入状态文件

> 修改相应证书对应的证书准入状态文件ca.json

```shell
/mydata/FISCO-BCOS/systemcontractv2
vim ca.json
```

> 配置status，0表示不可用，1表示可用。其它字段默认即可。如下，让node2的证书不可用。即status置0。

```json
{
        "hash" : "8A4B2CDE94348D22",
        "status" : 0,
        "pubkey":"",
        "orgname":"",
        "notbefore":20170223,
        "notafter":20180223,
        "whitelist":"",
        "blacklist":""
}
```

证书准入状态文件其它字段说明请参考<u>11.6 证书准入状态文件说明</u>

#### 7.5.2 更新证书准入状态

> 执行以下命令，指定证书准入状态文件ca.json，更新证书准入状态。

```shell
babel-node tool.js CAAction updateStatus ca.json
```

> 查看证书状态

```shell
babel-node tool.js CAAction all
```

>可看到证书状态

```log
----------CA 0---------
hash=8A4B2CDE94348D22
pubkey=
orgname=
notbefore=20170223
notafter=20180223
status=0
blocknumber=36
whitelist=
blacklist=

```
### 7.6 证书常见问题FAQ  

**Q:证书验证的原理？**    
A:假设A节点与B节点连接，相互连接的节点把本地的server.crt文件发送到对方节点，对方节点使用本地的ca.crt文件验证server.crt证书文件有效性，验证成功建立连接否则断连。所以要想成功建立连接确保节点的server.crt由同一ca颁发。

**Q.CAAction合约验证的原理？**  
A:打开CAVerify开关,节点连接时在CAAction合约中查找节点证书是否在合约中，如果存在节点正常连接否则连接失败。打开CAVerify开关时确保CAAction合约中已经写入区块链上所有节点的证书信息，否则未写入合约的节点无法正常连接。

**Q:打开ssl节点无法连接**  
A:1.确保所有节点的data目录下config.json配置文件中的ssl已配置为1。
  2.打开CAVerify开关前确保所有节点的证书信息是否已写到CAACtion合约中。

**Q:已打开CAVerify开关未写入节点证书信息到CAACtion合约中导致节点无法连接**  
A:在所有节点的data目录下config.json配置文件中ssl设置为0并重启节点，节点正常连接后把CAVerify设置为false，再重新把所有节点的config.json配置文件中的ssl设置为1。再重新把节点证书写入CAACtion合约中再打开CAVerify开关即可。

**Q:证书有效期无法设置**  
A:生成证书脚本中ca根证书固定为10年，用户证书固定为1年，如有修改证书有效期需求可修改genkey.sh脚本中的日期即可。

**Q:java客户端无法与节点通信问题**  
A:java客户端需使用与节点相同的ca.crt证书。如节点证书自生成则使用以下指令生成java客户端证书：  
1、openssl pkcs12 -export -name client -in server.crt -inkey server.key -out keystore.p12  
2、keytool -importkeystore -destkeystore client.keystore -srckeystore keystore.p12 -srcstoretype pkcs12 -alias client  
3、Attention！ Password must be ”123456”  

## 第八章 使用控制台

控制台能够以IPC的方式直接连接区块链节点进程。登录控制台后，能够直接查看区块链的各种信息。

### 8.1 登录控制台

> 连接节点的data目录下的geth.ipc文件。

```shell
ethconsole /mydata/nodedata-1/data/geth.ipc
```

> 登录成功，可看到successful字样。

```log
Connecting to node at /mydata/nodedata-1/data/geth.ipc
Connection successful.
Current block number: 37
Entering interactive mode.
> 
```

### 8.2 控制台操作

#### 8.2.1 查看区块

> 如查看块高为2的区块

```js
web3.eth.getBlock(2,console.log)
```

> 可得到相应的区块信息

```log
> null { author: '0x0000000000000000000000000000000000000000',
  difficulty: { [String: '1'] s: 1, e: 0, c: [ 1 ] },
  extraData: '0xd78095312e302b2b302d524c696e75782f672b2b2f496e74',
  gasLimit: 2000000000,
  gasUsed: 33245,
  genIndex: '0x0',
  hash: '0xa3c2b1eda74f26c688e78bffcc71c8561e49dc70fbfbd71b85c3b79a2c16bc81',
  logsBloom: '0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000',
  miner: '0x0000000000000000000000000000000000000000',
  number: 2,
  parentHash: '0x96d095047e68d4db8ae0c74638cf6365d5ee7fbc329d50a5a78a85189e1b105e',
  receiptsRoot: '0xb668c383b2ee76f9e2d0a8b0d9b301fb825896a9a597268ca3cbdd979c4a53da',
  sha3Uncles: '0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347',
  size: 71,
  stateRoot: '0x20993fb96c5a26eb9f158c194bcd4da845afde2c2095e359259001279de23ec2',
  timestamp: 1513087469641,
  totalDifficulty: { [String: '2'] s: 1, e: 0, c: [ 2 ] },
  transactions: [ '0x63749a62851b52f9263e3c9a791369c7380acc5a9b6ee55dabd9c1013634e355' ],
  transactionsRoot: '0xc9c0b09f236fba3bad81f6fb92b9ca03b6ccb1aaf53ef600621c0a0fd0fd4bb1',
  uncles: [] }
```

#### 8.2.2 查看交易

> 根据交易哈希查看交易。如，使用之前调用HelloWord的合约的交易哈希。

```js
web3.eth.getTransaction('0x63749a62851b52f9263e3c9a791369c7380acc5a9b6ee55dabd9c1013634e355',console.log)
```

> 可得到相应的交易信息

```log
> null { blockHash: '0xa3c2b1eda74f26c688e78bffcc71c8561e49dc70fbfbd71b85c3b79a2c16bc81',
  blockNumber: 2,
  from: '0x04804c06677d2009e52ca96c825d38056292cab6',
  gas: 30000000,
  gasPrice: { [String: '0'] s: 1, e: 0, c: [ 0 ] },
  hash: '0x63749a62851b52f9263e3c9a791369c7380acc5a9b6ee55dabd9c1013634e355',
  input: '0x4ed3885e0000000000000000000000000000000000000000000000000000000000000020000000000000000000000000000000000000000000000000000000000000000b48656c6c6f576f726c6421000000000000000000000000000000000000000000',
  nonce: 67506452,
  to: '0x1d2047204130de907799adaea85c511c7ce85b6d',
  transactionIndex: 0,
  value: { [String: '0'] s: 1, e: 0, c: [ 0 ] } }
```

#### 8.2.3 查看交易回执

> 根据交易哈希查看交易回执。如，使用之前调用HelloWord的合约的交易哈希。

```js
web3.eth.getTransactionReceipt('0x63749a62851b52f9263e3c9a791369c7380acc5a9b6ee55dabd9c1013634e355',console.log)
```

> 可得到相应的交易回执

```log
> null { blockHash: '0xa3c2b1eda74f26c688e78bffcc71c8561e49dc70fbfbd71b85c3b79a2c16bc81',
  blockNumber: 2,
  contractAddress: '0x0000000000000000000000000000000000000000',
  cumulativeGasUsed: 33245,
  gasUsed: 33245,
  logs: [],
  transactionHash: '0x63749a62851b52f9263e3c9a791369c7380acc5a9b6ee55dabd9c1013634e355',
  transactionIndex: 0 }
```

#### 8.2.4 查看合约代码

> 根据交易合约地址查看合约。如，用之前部署的HelloWorld合约地址。

```js
web3.eth.getCode('0x1d2047204130de907799adaea85c511c7ce85b6d',console.log)
```

> 可以得到HelloWorld的合约二进制代码

```log
> null '0x60606040526000357c0100000000000000000000000000000000000000000000000000000000900463ffffffff1680634ed3885e146100485780636d4ce63c146100a557600080fd5b341561005357600080fd5b6100a3600480803590602001908201803590602001908080601f01602080910402602001604051908101604052809392919081815260200183838082843782019150505050505091905050610133565b005b34156100b057600080fd5b6100b861014d565b6040518080602001828103825283818151815260200191508051906020019080838360005b838110156100f85780820151818401526020810190506100dd565b50505050905090810190601f1680156101255780820380516001836020036101000a031916815260200191505b509250505060405180910390f35b80600090805190602001906101499291906101f5565b5050565b610155610275565b60008054600181600116156101000203166002900480601f0160208091040260200160405190810160405280929190818152602001828054600181600116156101000203166002900480156101eb5780601f106101c0576101008083540402835291602001916101eb565b820191906000526020600020905b8154815290600101906020018083116101ce57829003601f168201915b5050505050905090565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f1061023657805160ff1916838001178555610264565b82800160010185558215610264579182015b82811115610263578251825591602001919060010190610248565b5b5090506102719190610289565b5090565b602060405190810160405280600081525090565b6102ab91905b808211156102a757600081600090555060010161028f565b5090565b905600a165627a7a7230582021681cb0ea5b1c4364a4f5e37f12e84241310a74ee462b1a19658b3acc26c6cb0029'
```

#### 8.2.5 查看节点连接

> 执行

```js
web3.admin.getPeers(console.log)
```

> 可看到连接了其它的节点

```log
> null [ { caps: [ 'eth/62', 'eth/63', 'pbft/63' ],
    height: '0x25',
    id: 'b5adf6440bb0fe7c337eccfda9259985ee42c1c94e0d357e813f905b6c0fa2049d45170b78367649dd0b8b5954ee919bf50c1398a373ca777e6329bd0c4b82e8',
    lastPing: 0,
    name: 'eth/v1.0/Linux/g++/Interpreter/RelWithDebInfo/0/',
    network: { remoteAddress: '127.0.0.1:30403' },
    notes: { ask: 'Nothing', manners: 'nice', sync: 'holding & needed' } } ]
```



## 第九章 其它可选工具

### 9.1 导出创世块工具

FISCO BCOS支持把现有区块链中所有的合约全部导出到一个文件中。新的区块链可使用此文件作为创世块文件启动，进而继承原来区块链中所有的合约。

#### 9.1.1 停止当前区块链节点

> 在导出前，要将当前操作目录对应的区块链节点停掉

```shell
cd /mydata/nodedata-1
./stop.sh
```

#### 9.1.2 导出创世块

> 用--export-genesis字段指定导出的创世块文件名。

```shell
fisco-bcos --genesis ./genesis.json --config ./config.json 	--export-genesis ./new_genesis.json
```

> 一段时间后，生成new_genesis.json。此文件即是导出的创世块，可作为新的区块链的创世块使用。

### 9.2 监控连接和块高

monitor.js脚本监控节点的连接情况和块高。在运行前，请确认：

（1）被监控的区块链节点已经启动。

（2）config.js正确配置，proxy字段指向了需要监控的区块链节点的RPC端口。

> 配置config，并执行monitor.js

```shell
cd /mydata/FISCO-BCOS/tool/
vim ../web3lib/config.js
babel-node monitor.js
```

> 不断刷出节点连接情况和块高

```log
current blocknumber 429
the number of connected nodes：0
...........Node 0.........
NodeId:838a187e32e72e3889330c2591536d20868f34691f1822fbcd43cb345ef437c7a6568170955802db2bf1ee84271bc9cba64fba87fba84e0dba03e5a05de88a2c
Host:127.0.0.1:30403
--------------------------------------------------------------
current blocknumber 429
the number of connected nodes：0
...........Node 0.........
NodeId:838a187e32e72e3889330c2591536d20868f34691f1822fbcd43cb345ef437c7a6568170955802db2bf1ee84271bc9cba64fba87fba84e0dba03e5a05de88a2c
Host:127.0.0.1:30403

```



## 第十章 FISCO BCOS 特性

FISCO BCOS的特性，请直接参看相关特性说明文档：

1. [AMOP（链上链下）](../amop使用说明文档.md)
2. [Contract_Name_Service](../CNS_Contract_Name_Service_服务使用说明文档.md)
3. EthCall [设计文档](../EthCall设计文档.md) [说明文档](../EthCall说明文档.md)
4. [web3sdk](../web3sdk使用说明文档.md)
5. [并行计算](../并行计算使用说明文档.md) 
6. [分布式文件系统](../分布式文件系统使用说明.md)
7. [监控统计日志](../监控统计日志说明文档.md)
8. [同态加密](../同态加密说明文档.md)
9. [机构证书准入](../CA机构身份认证说明文档.md)
10. [可监管的零知识证明](../可监管的零知识证明说明.md)
11. [群签名环签名](../启用_关闭群签名环签名ethcall.md)
12. [弹性联盟链共识框架](../弹性联盟链共识框架说明文档.md)



## 第十一章 附录

### 11.1 源码目录结构说明


| 目录                | 说明                                       |
| ----------------- | ---------------------------------------- |
| abi               | CNS(合约命名服务)模块代码                          |
| eth               | 主入口目录，其中main.cpp包含main函数入口               |
| libchannelserver  | AMOP(链上链下通信协议)实现目录                       |
| libdevcore        | 基础通用组件实现目录，如工具类函数、基础数据类型结构定义、IO操作函数、读写锁、内存DB、TrieDB、SHA3实现、RLP编解码实现、Worker模型等等 |
| libdiskencryption | 落盘存储加密实现目录                               |
| libethcore        | 区块链核心数据结构目录。如ABI、秘钥管理、区块头、预编译、交易结构等等     |
| libethereum       | 区块链主框架逻辑目录。如交易池、系统合约、节点管理、块链、块、链参数等等     |
| libevm            | 虚拟机主目录。如解释器、JIT等等                        |
| libevmcore        | OPCODE指令集定义、定价                           |
| libp2p            | 区块链P2P网络主目录。如握手、网络包编解码、会话管理等等            |
| libpaillier       | 同态加密算法目录                                 |
| libpbftseal       | PBFT共识插件实现目录                             |
| libraftseal       | RAFT共识插件实现目录                             |
| libstatistics     | 访问频率统计与控制实现目录                            |
| libweb3jsonrpc    | web3 RPC实现目录                             |
| sample            | 一键安装与部署                                  |
| scripts           | 与安装相关的脚本                                 |
| systemproxyv2     | 系统合约实现目录                                 |

### 11.2 cryptomod.json说明

FISCO BCOS区块链节点支持加密通信，在工具配置文件（cryptomod.json）中配置区块链节点间的加密通信方式。

| 配置项               | 说明                                       |
| ----------------- | ---------------------------------------- |
| cryptomod         | NodeId生成模式  默认填0                         |
| rlpcreatepath     | NodeId生成路径,文件名必须为network.rlp 如：/mydata/nodedata-1/data/network.rlp |
| datakeycreatepath | datakey生成路径 填空                           |
| keycenterurl      | 远程加密服务 填空                                |
| superkey          | 本地加密服务密码 填空                              |

### 11.3 genesis.json说明

创世块文件genesis.json关键字段说明如下：

| 配置项            | 说明                                       |
| -------------- | ---------------------------------------- |
| timestamp      | 创世块时间戳(毫秒)                               |
| god            | 内置链管理员账号地址（填入<u>2.2 生成god账号</u> 小节中生成的地址） |
| alloc          | 内置合约数据                                   |
| initMinerNodes | 创世块节点NodeId（填入<u>2.3 生成节点身份NodeId</u>小节中生成的NodeId） |

### 11.4 config.json说明

节点配置文件config.json关键字段说明如下：

| 配置项                | 说明                                       |
| ------------------ | ---------------------------------------- |
| sealEngine         | 共识算法（可选PBFT、RAFT、SinglePoint）            |
| systemproxyaddress | 系统路由合约地址（生成方法可参看部署系统合约）                  |
| listenip           | 节点监听IP                                   |
| cryptomod          | 加密模式默认为0（与cryptomod.json文件中cryptomod字段保持一致） |
| ssl                | 是否启用SSL证书通信（0：非SSL通信 1：SSL通信 需在datadir目录下放置证书文件） |
| rpcport            | RPC监听端口）（若在同台机器上部署多个节点时，端口不能重复）          |
| p2pport            | P2P网络监听端口（若在同台机器上部署多个节点时，端口不能重复）         |
| channelPort        | 链上链下监听端口（若在同台机器上部署多个节点时，端口不能重复）          |
| wallet             | 钱包文件路径                                   |
| keystoredir        | 账号文件目录路径                                 |
| datadir            | 节点数据目录路径                                 |
| vm                 | vm引擎 （默认 interpreter ）                   |
| networkid          | 网络ID                                     |
| logverbosity       | 日志级别（级别越高日志越详细，>8 TRACE日志，4<=x<8 DEBU G日志，<4 INFO日志） |
| coverlog           | 覆盖率插件开关（ON或OFF）                          |
| eventlog           | 合约日志开关（ON或OFF）                           |
| statlog            | 统计日志开关（ON或OFF）                           |
| logconf            | 日志配置文件路径（日志配置文件可参看日志配置文件说明）              |
| NodeextraInfo      | 节点连接配置列表[{NodeId,Ip,port,nodedesc,agencyinfo,identitytype}]（节点身份NodeID、外网IP、P2P网络端口、节点描述、节点信息、节点类型），其中NodeId填入<u>2.3 生成节点身份NodeId</u>小节中生成的NodeId。此处必须配置节点自身的信息。在配置其它节点信息时，确保节点间能够构成一个连通图。为了稳定性，推荐配置多个除自身以外的其它节点信息。 |
| dfsNode            | 分布式文件服务节点ID ，与节点身份NodeID一致 （可选功能配置参数）    |
| dfsGroup           | 分布式文件服务组ID （10 - 32个字符）（可选功能配置参数）        |
| dfsStorage         | 指定分布式文件系统所使用文件存储目录（可选功能配置参数）             |

### 11.5 log.conf说明

日志配置文件log.conf关键字段说明如下：

| 配置项                 | 说明                                       |
| ------------------- | ---------------------------------------- |
| FORMAT              | 日志格式，典型如%level                           |
| FILENAME            | 例如/mydata/nodedata-1/log/log_%datetime{%Y%M%d%H}.log |
| MAX_LOG_FILE_SIZE   | 最大日志文件大小                                 |
| LOG_FLUSH_THRESHOLD | 超过多少条日志即可落盘                              |

### 11.6 证书准入状态文件说明

证书授权配置文件关键字段说明如下：

| **字段**    | **说明**          |
| --------- | --------------- |
| hash      | 公钥证书序列号         |
| pubkey    | 公钥证书，填空即可       |
| orgname   | 机构名称，填空即可       |
| notbefore | 证书生效时间          |
| notafter  | 证书过期时间          |
| status    | 证书状态 0：不可用 1：可用 |
| whitelist | ip白名单，填空即可      |
| blacklist | ip黑名单，填空即可      |

### 11.7 系统合约介绍

系统合约是FISCO BCOS区块链的内置智能合约。

主要分为以下几种：系统代理合约、节点管理合约、机构证书合约、权限管理合约、全网配置合约。

系统合约一经部署，全网生效后，一般不轻易变更。

若需重新部署变更升级，需全网所有节点都认可且重新更新运行参数配置文件的systemproxyaddress字段后重启。
系统合约原则上只允许管理员账号调用。

以下依次介绍各个系统合约的源码路径、已实现接口说明、调用例子、工具使用方法。

#### 11.7.1 系统代理合约

系统代理合约是系统合约的统一入口。

它提供了路由名称到合约地址的映射关系。

源码路径：systemcontractv2/SystemProxy.sol

**（1）接口说明**

| 接口名             | 输入              | 输出              | 备注            |
| --------------- | --------------- | --------------- | ------------- |
| 获取路由信息 getRoute | 路由名称            | 路由地址、缓存标志位、生效块号 | 无             |
| 注册路由信息setRoute  | 路由名称、路由地址、缓存标志位 | 无               | 若该路由名称已存在，则覆盖 |

web3调用示例如下（可参看systemcontractv2/deploy.js）：
```js
 console.log("register NodeAction.....");
 func = "setRoute(string,address,bool)";
 params = ["NodeAction", NodeAction.address, false];
 receipt = await web3sync.sendRawTransaction(config.account, config.privKey, SystemProxy.address, func, params);
```

**（2）工具使用方法**

查看所有系统合约信息：

```shell
babel-node tool.js SystemProxy
```


示例输出如下：

```log
{ HttpProvider: 'http://127.0.0.1:8701',
  Ouputpath: './output/',
  privKey: 'bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd',
  account: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3' }
Soc File :SystemProxy
Func :undefined
SystemProxy address 0x210a7d467c3c43307f11eda35f387be456334fed
-----------------SystemProxy route----------------------
0 )TransactionFilterChain=>0xea8425697a093606309eb85e4447d6f333cff2fe,false,395
1 )ConfigAction=>0x09e4f1b4fa1713339f5aa17b40fa6f9920c7b278,false,396
2 )NodeAction=>0xcc46c245e6cca918d43bf939bbb10a8c0988548f,false,397
3 )CAAction=>0x8ab1175c6e7edb40dd0ed2a52ceaa94afb135a64,false,398
4 )ContractAbiMgr=>0x707024221d2433067b768c4be3a005c5ece8df40,false,399
5 )ConsensusControlMgr=>0x007f2c2751bbcd6c9a630945a87a3bc2af38788c,false,400
6 )FileInfoManager=>0xe0caa8103ea05b5ce585c05d8112051a0b213acf,false,401
7 )FileServerManager=>0xe585cc5b8ca7fb174a0560bf79eea7398efaf014,false,402
```

输出中即是当前系统路由表的所有路由信息。

#### 11.7.2 节点管理合约

节点管理合约主要功能是维护网络中节点列表。
网络中节点加入或退出都需要与节点管理合约进行交互。

源码路径：systemcontractv2/NodeAction.sol

**（1）接口说明**

| 接口名               | 输入                                       | 输出   | 备注            |
| ----------------- | ---------------------------------------- | ---- | ------------- |
| 节点入网 registerNode | 节点ID、IP、端口、节点类型、节点描述、节点CA哈希、节点agency、节点序号 | 布尔结果 | 若该节点ID已存在，则忽略 |
| 节点出网 cancelNode   | 节点ID                                     | 布尔结果 | 若该路由名称不存在，则忽略 |

web3调用示例如下（可参看systemcontractv2/tool.js）：
```js
var instance=getAction("NodeAction");
var func = "registerNode(string,string,uint256,uint8,string,string,string,uint256)";
var params = [node.id,node.ip,node.port,node.category,node.desc,node.CAhash,node.agencyinfo,node.idx];
var receipt = web3sync.sendRawTransaction(config.account, config.privKey, instance.address, func, params);
```

**（2）工具使用方法**

请参看 添加非创世节点入网、节点出网

#### 11.7.3 机构证书合约

机构证书合约主要功能是维护机构证书信息。

源码路径：systemcontractv2/CAAction.sol

**（1）接口说明**

| 接口名                 | 输入                                       | 输出                                   | 备注            |
| ------------------- | ---------------------------------------- | ------------------------------------ | ------------- |
| 更新证书信息 update       | 证书哈希、公钥、组织名称、证书有效开始时间、证书失效时间、证书状态、白名单列表、黑名单列表 | 布尔结果                                 | 若该证书信息不存在，则新建 |
| 更新证书状态 updateStatus | 证书哈希、证书状态                                | 布尔结果                                 | 若该路由名称不存在，则忽略 |
| 查询证书信息 get          | 证书哈希                                     | 证书哈希、公钥、组织名称、证书有效开始时间、证书失效时间、证书状态、块号 | 无             |
| 查询证书黑白名单列表 getIp    | 证书哈希                                     | 白名单列表、黑名单列表                          | 无             |

web3调用示例如下（可参看systemcontractv2/tool.js）：
```js
 var instance=getAction("CAAction");
 var func = "update(string,string,string,uint256,uint256,uint8,string,string)";
 var params = [ca.hash,ca.pubkey,ca.orgname,ca.notbefore,ca.notafter,ca.status,ca.whitelist,ca.blacklist];
 var receipt = web3sync.sendRawTransaction(config.account, config.privKey, instance.address, func, params);

```

**（2）工具使用方法**

查看证书列表

```shell
babel-node tool.js CAAction all
```

更新证书信息

```shell
babel-node tool.js CAAction update
```

更新证书状态

```shell
babel-node tool.js CAAction updateStatus
```

#### 11.7.4 权限管理合约

权限管理合约是对区块链权限模型的实现。

一个外部账户只属于一个角色，一个角色拥有一个权限项列表。

一个权限项由合约地址加上合约接口来唯一标识。

源码路径：systemcontractv2/AuthorityFilter.sol	交易权限Filter

 systemcontractv2/Group.sol

**（1）接口说明**

| 合约         | 接口名                       | 输入                             | 输出   | 备注   |
| ---------- | ------------------------- | ------------------------------ | ---- | ---- |
| 角色         | 设置用户权限组 权限项 setPermission | 合约地址、合约接口、权限标记                 | 无    | 无    |
|            | 获取权限标记 getPermission      | 合约地址、合约接口                      | 权限标记 | 无    |
| 交易权限Filter | 设置用户所属角色 setUserGroup     | 用户外部账户、用户所属角色合约                | 无    | 无    |
|            | 交易权限检查 process            | 用户外部账户、交易发起账户、合约地址、合约接口、交易输入数据 |      | 无    |

web3调用示例如下（可参看systemcontractv2/deploy.js）：
```js
var GroupReicpt= await web3sync.rawDeploy(config.account, config.privKey, "Group");
var Group=web3.eth.contract(getAbi("Group")).at(GroupReicpt.contractAddress);
#......省略若干行......
abi = getAbi0("Group");
params  = ["Group","Group","",abi,GroupReicpt.contractAddress];
receipt = await web3sync.sendRawTransaction(config.account, config.privKey, ContractAbiMgrReicpt.contractAddre
ss, func, params);
```

**（2）工具使用方法**

检查用户外部账户权限

```shell
babel-node tool.js AuthorityFilter 用户外部账户、合约地址、合约接口
```

**（3）自主定制**

继承TransactionFilterBase实现新的交易Filter合约。并通过addFilter接口将新Filter注册入TransactionFilterChain即可。

#### 11.7.5 全网配置合约

全网配置合约维护了区块链中部分全网运行配置信息。

目标是为了通过交易的全网共识来达成全网配置的一致更新。

源码路径：systemcontractv2/ConfigAction.sol

**（1）全网配置项说明**

| 配置项                  | 说明                           | 默认值       | 推荐值         |
| -------------------- | ---------------------------- | --------- | ----------- |
| maxBlockHeadGas      | 块最大GAS （16进制）                | 200000000 | 20000000000 |
| intervalBlockTime    | 块间隔(ms) （16进制）               | 1000      | 1000        |
| maxBlockTranscations | 块最大交易数（16进制）                 | 1000      | 1000        |
| maxNonceCheckBlock   | 交易nonce检查最大块范围（16进制）         | 1000      | 1000        |
| maxBlockLimit        | blockLimit超过当前块号的偏移最大值（16进制） | 1000      | 1000        |
| maxTranscationGas    | 交易的最大gas（16进制）               | 20000000  | 20000000    |
| CAVerify             | CA验证开关                       | FALSE     | FALSE       |

**（2）接口说明**

| 接口名       | 输入      | 输出     | 备注           |
| --------- | ------- | ------ | ------------ |
| 设置配置项 set | 配置项、配置值 | 无      | 若配置表中已存在，则覆盖 |
| 查询配置值 get | 配置项     | 配置值、块号 | 无            |

web3调用示例如下（可参看systemcontractv2/tool.js）：
```js
var func = "set(string,string)";
var params = [key,value];
var receipt = web3sync.sendRawTransaction(config.account, config.privKey, instance.address, func, params);
console.log("config :"+key+","+value);
```

**（3）使用方法**

查询配置项

```shell
babel-node tool.js ConfigAction get 配置项
```

设置配置项

```shell
babel-node tool.js ConfigAction set 配置项 配置值
```



## 常见问题

### 1 脚本执行时，格式错误

**现象**

执行脚本（如install_deps.sh，build.sh等）时，报如下格式错误。是windows格式和linux格式不一致导致。

``` log
xxxxx.sh: 行x： $'\r':未找到命令
xxxxx.sh: 行x： $'\r':未找到命令
xxxxx.sh: 行x： $'\r':未找到命令
xxxxx.sh: 行x： $'\r':未找到命令
```

**解决方法**

用dos2unix工具转换一下格式

``` shell
sudo yum -y install dos2unix
dos2unix xxxxx.sh
```







