# FISCO BCOS区块链操作手册

[FISCO-BCOS Manual](README_EN.md)

## 前言

本文档是FISCO BCOS区块链开源平台的一部分。
FISCO BCOS平台是金融区块链合作联盟（深圳）（以下简称：金链盟）开源工作组以金融业务实践为参考样本，在BCOS开源平台基础上进行模块升级与功能重塑，深度定制的安全可控、适用于金融行业且完全开源的区块链底层平台。  

金链盟开源工作组获得金链盟成员机构的广泛认可，并由专注于区块链底层技术研发的成员机构及开发者牵头开展工作。其中首批成员包括以下单位（排名不分先后）：博彦科技、华为、深证通、神州数码、四方精创、腾讯、微众银行、越秀金科。

FISCO BCOS平台基于现有的BCOS开源项目进行开发，聚焦于金融行业的分布式商业需求，从业务适当性、性能、安全、政策、技术可行性、运维与治理、成本等多个维度进行综合考虑，打造金融版本的区块链解决方案。

为了让大家更好的了解FISCO BCOS区块链开源平台的使用方法。本文档按照Step By Step的步骤详细介绍了FISCO BCOS区块链的构建、安装、启动，智能合约部署调用、多节点组网等内容的介绍。

本文档不介绍FISCO BCOS区块链设计理念及思路，详情请参看白皮书。

以下操作命令以在Centos7.2/Ubuntu 16.04操作系统为示例，手册目录如下

- [前言](#前言)
- [第一章 FISCO BCOS快速部署](#第一章-fisco-bcos快速部署)
- [第二章 准备相关证书](#第二章-准备fisco-bcos软件)
- [第三章 部署单节点区块链网络](#第三章-手工部署单节点区块链网络)
- [第四章 使用控制台](#第四章-使用控制台)
- [第五章 部署合约、调用合约](#第五章-部署合约调用合约)
- [第六章 多记账节点组网](#第六章-多记账节点组网)
- [第七章 FISCO BCOS 特性](#第七章-fisco-bcos-特性)
- [第八章 附录](#第八章-附录)

## 第一章 FISCO BCOS快速部署

本章主要介绍FISCO BCOS区块链环境的部署。包括机器配置，快速构建，部署软件环境和编译源码。

### 1.1 机器配置

| 配置   | 最低配置   | 推荐配置                         |
| ---- | ------ | ------------------------------------ |
| CPU  | 1.5GHz | 2.4GHz                               |
| 内存   | 1GB    | 4GB                                |
| 核心   | 2核     | 4核                                |
| 带宽   | 1Mb    | 5Mb                                 |
| 操作系统|      | CentOS(7.2 64位)或Ubuntu(16.04  64位) |
| JAVA   |       | Java(TM) 1.8 && JDK 1.8 |

### 1.2 使用快速构建脚本

> 第二、三章是手动部署单节点网络的操作步骤，如果不想**手工操作**，可以尝试使用`FISCO-BCOS/scrtips/build_chain.sh`脚本，根据脚本提示设置参数，快速搭建区块链网络。

1. 配置环境

```bash
# 假设当前在FISCO-BCOS/build目录下
cd ../scripts
# 准备IPList.txt文件 按行分割，可以有多个IP
echo 127.0.0.1 > IPList.txt
# build_chain.sh 
# Usage:
# 	-f <IP list file>           [Required]
# 	-e <FISCO-BCOS binary path> Default download from github
# 	-n <Nodes per IP>           Default 1
# 	-a <CA Key>                 Default Generate a new CA
# 	-o <Output Dir>             Default ./output/
# 	-p <Start Port>             Default 30300
# 	-c <ClientCert Passwd>      Default 123456
# 	-k <Keystore Passwd>        Default 123456
# 	-s <StateDB type>           Default leveldb. if set -s, use amop
# 	-t <Cert config file>       Default auto generate
# 	-z Generate tar packet      Default no
# 	-h Help

# 生成节点配置文件
bash build_chain.sh -f IPList.txt -n 4 -o nodes
```

2. 启动节点

```bash
cd nodes
bash ./start_all.sh
```

到这里本机已经启动了4个节点组成的一条区块链，**接下来请参考[3.6 验证节点启动](#36-验证节点启动)检查节点运行状态，然后跳转到[第四章 使用控制台](#第四章-使用控制台)继续阅读**。

## 第二章 准备`fisco-bcos`软件

### 2.1 下载官方二进制包[推荐]

```bash
# FISCO-BCOS/ 目录下操作
curl -o fisco-bcos https://raw.githubusercontent.com/FISCO-BCOS/FISCO-BCOS/dev-1.5/bin/fisco-bcos
```

### 2.2 源码编译

1. 获取源码

> 假定在用户目录下获取源码：

```bash
# clone源码
git clone https://github.com/FISCO-BCOS/FISCO-BCOS.git

# 切换到源码根目录
cd FISCO-BCOS 
git checkout dev-1.5
```

源码目录说明请参考[附录：源码目录结构说明](#81-源码目录结构说明)

2. 安装编译依赖

> 项目根目录下执行（若执行出错，请参考[常见问题1](#常见问题)）：

```shell
bash ./scripts/install_deps.sh
```

3. 编译

若编译成功，则生成`build/eth/fisco-bcos`可执行程序。
```bash
mkdir build && cd build
# [Centos] 注意命令末尾的..
cmake3  ..  
# [Ubuntu]
cmake  ..  
# 编译 多核可尝试make -j$(nproc)
make
```

## 第三章 手工部署单节点区块链网络

FISCO-BCOS网络采用面向CA的准入机制，保障信息保密性、认证性、完整性、不可抵赖性。**FISCO-BCOS支持多级证书体系，可以是链、机构和节点的三级体系，也可以链和节点两级体系。**
以链和节点两级证书体系为例，一条链拥有一个链证书及对应的链私钥，链私钥由链管理员拥有。并对每个参与该链的节点签发节点证书。节点证书是节点身份的凭证，并使用该证书与其他节点间建立SSL连接进行加密通讯。本章介绍手动搭建一个单节点的区块链网络，包括链证书、节点私钥、节点证书、配置文件的生成和配置。生成方法如下：

### 3.1 生成链证书

```bash
# 假设当前在FISCO-BCOS/build目录下 切换到FISCO-BCOS/scripts目录下
cd ../scripts
mkdir nodes && cd nodes
bash ../chain.sh  #会提示输入相关证书信息，默认可以直接回车
```
> FISCO-BCOS/scripts/nodes目录下将生成链证书相关文件。
> **注意：ca.key 链私钥文件请妥善保存**

### 3.2 生成节点密钥和证书

```bash
# 假设节点名为 node-0
# 如需要生成多个节点，则重复执行 ./node.sh 节点名称 即可
bash ../node.sh node-0
```

> **注意：node.key 节点私钥文件请妥善保存**。`FISCO-BCOS/scripts/nodes/`目录下将生成节点目录`node-0`。`node-0/data`目录下将有链证书`ca.crt`、节点私钥`node.key`、节点证书`node.crt`相关文件。

### 3.3 生成SDK证书 [可选]

``` shell
# 为节点node0生成sdk所需文件，这里需要设置密码
bash ../sdk.sh node-0
cd ../  # 回到scripts目录
```

> FISCO-BCOS/build/nodes/node-0目录下将生成sdk目录，并将sdk目录下所有文件拷贝到SDK端的证书目录下。

### 3.4 准备节点环境

> 假定节点目录为FISCO-BCOS/scripts/nodes/node-0，按如下步骤操作：

```shell
# 假定当前目录为FISCO-BCOS/scripts/ 拷贝相关文件
cp ../fisco-bcos ../genesis.json ../config.conf ../log.conf start.sh stop.sh nodes/node-0
```

#### 3.4.1 获取NodeID

NodeID唯一标识了区块链中的某个节点，在节点启动前必须进行配置。

> nodes/node-0/data/node.nodeid文件中已计算好NodeID，也可以使用下面命令获取

```bash
# nodes/node-0/data/node.key为节点私钥
openssl ec -in nodes/node-0/data/node.key -text 2> /dev/null | perl -ne '$. > 6 and $. < 12 and ~s/[\n:\s]//g and print' | perl -ne 'print substr($_, 2)."\n"'
# 得到类似下面的NodeID
# d23a6bad030a395b4ca3f2c4fa9a31ad58411fe8b6313472881d88d1fa3feaeab81b0ff37156ab3b1a69350115fd68cc2e4f2490ce01b1d7b4d8e22de00aea71
```

#### 3.4.2 参数配置文件

配置使用INI管理，配置文件模板如下，本例中使用单个节点，需要将`nodes/node-0/config.conf`中的miner.0修改为上一步获取的NodeID。

```bash
# 假设当前目录为FISCO-BCOS/scripts
# 获取NodeID
cat nodes/node-0/data/node.nodeid
# 修改 nodes/node-0/config.conf中miner.0=${NodeID}
vim nodes/node-0/config.conf
```

```bash
;rpc配置 web3sdk连接用
[rpc]
    ;listen_ip应该设置为内网IP
    listen_ip=0.0.0.0
    listen_port=30301
    ;原rpc端口
    http_listen_port=30302
    console_port=30303
	
;p2p配置 区块链节点间互联用
[p2p]
    listen_ip=0.0.0.0
    listen_port=30300
    ;配置区块链节点列表 格式为ip:端口 以node.开头
    node.0=127.0.0.1:30300
	
; PBFT共识配置 同一个共识网络内的所有节点的[pbft]分类配置必须一致
[pbft]
    ; 配置共识节点列表 用NodeID标识 以miner.开头
    miner.0=d23a6bad030a395b4ca3f2c4fa9a31ad58411fe8b6313472881d88d1fa3feaeab81b0ff37156ab3b1a69350115fd68cc2e4f2490ce01b1d7b4d8e22de00aea71
	
; 通用配置
[common]
	; 配置数据目录 变量${DATAPATH} == data_path
	data_path=data/
	log_config=log.conf
	
; 安全配置
[secure]
	key=${DATAPATH}/node.key
	; 配置节点的证书 
	cert=${DATAPATH}/node.crt
	; 配置节点的CA证书
	ca_cert=${DATAPATH}/ca.crt

; 状态数据库配置
[statedb]
	; statedb的类型 可选leveldb/amop
	type=leveldb
	path=${DATAPATH}/statedb
	; amopdb关注的topic 默认为DB
	topic=DB
```

#### 3.4.3 配置log.conf（日志配置文件）

log.conf中配置节点日志生成的格式和路径，使用默认即可。
log.conf其它字段说明请参看[附录：log.conf说明](#85-logconf说明)

### 3.5 启动节点

节点的启动依赖下列文件，在启动前，请确认文件已经正确的配置：

- 创世块文件：genesis.json
- 节点配置文件：config.conf
- 日志配置文件：log.conf
- 证书文件（nodes/node-0/data/）：ca.crt、node.key、node.crt

> genesis.json其它字段说明请参看[附录：genesis.json说明](#83-genesisjson说明genesisjson)

1. 脚本启动

```shell
# 启动节点
cd nodes/node-0
bash start.sh
# 若需要停止节点
# bash stop.sh
```

2. 手动启动

```shell
cd nodes/node-0
# 启动区块链节点
./fisco-bcos --config config.conf --genesis genesis.json &
# 查看日志输出
tail -f log/info* |grep ++++  
# 若需要退出节点
# ps -ef |grep fisco-bcos #查看进程号
# kill -9 13432 #13432是查看到的进程号
```

> 几秒后可看到不断刷出打包信息。

```log
INFO|2017-12-12 17:52:16:877|+++++++++++++++++++++++++++ Generating seal ondcae019af78cf04e17ad908ec142ca4e25d8da14791bda50a0eeea782ebf3731#1tx:0,maxtx:1000,tq.num=0time:1513072336877
INFO|2017-12-12 17:52:17:887|+++++++++++++++++++++++++++ Generating seal on3fef9b23b0733ac47fe5385072f80fc036b7517abae0a3e7762739cc66bc7dca#1tx:0,maxtx:1000,tq.num=0time:1513072337887
INFO|2017-12-12 17:52:18:897|+++++++++++++++++++++++++++ Generating seal onb5b38c7a380b13b2e46fecbdca0fac5473f4cbc054190e90b8bd4831faac4521#1tx:0,maxtx:1000,tq.num=0time:1513072338897
```

### 3.6 验证节点启动

#### 3.6.1 验证进程

```shell
ps -ef |grep fisco-bcos
```

> 看到进程启动

```log
app 19390     1  1 17:52 ?        00:00:05 fisco-bcos --config config.json --genesis genesis.json 
```

#### 3.6.2 查看日志输出

> 执行命令，查看打包信息。

```shell
tail -f log/info* |grep ++++  #查看日志输出
```

> 可看到不断刷出打包信息。若上述都正确输出，则表示节点已经正确启动！

```log
INFO|2017-12-12 17:52:16:877|+++++++++++++++++++++++++++ Generating seal ondcae019af78cf04e17ad908ec142ca4e25d8da14791bda50a0eeea782ebf3731#1tx:0,maxtx:1000,tq.num=0time:1513072336877
INFO|2017-12-12 17:52:17:887|+++++++++++++++++++++++++++ Generating seal on3fef9b23b0733ac47fe5385072f80fc036b7517abae0a3e7762739cc66bc7dca#1tx:0,maxtx:1000,tq.num=0time:1513072337887
INFO|2017-12-12 17:52:18:897|+++++++++++++++++++++++++++ Generating seal onb5b38c7a380b13b2e46fecbdca0fac5473f4cbc054190e90b8bd4831faac4521#1tx:0,maxtx:1000,tq.num=0time:1513072338897
```

## 第四章 使用控制台

### 4.1 连接控制台

查看节点的配置文件config.conf中的ip和console port：
```ini
[rpc]
   listen_ip=0.0.0.0
   listen_port=30301
   http_listen_port=30302
   console_port=30303
```

节点启动之后，输入以下命令连接节点的控制台：
```bash
telnet 127.0.0.1 30303
# 连接成功之后，显示信息如下：
Trying 127.0.0.1...
Connected to 127.0.0.1.
Escape character is '^]'.
```

### 4.2 控制台命令使用

#### 4.2.1 help命令

首先输入help，查看可用的命令：

```bash
help
======================================================================
status                 Show the blockchain status.
p2p.list               Show the peers information.
p2p.update             Update static nodes.
amdb.select            Query the table data.
miner.list             Show the miners information.
miner.add              Add miner node.
miner.remove           Remove miner node.
quit                   Quit the blockchain console.
help                   Provide help information for blockchain console.
======================================================================
```

#### 4.2.2 status命令

运行status命令，查看节点的状态，块高和view:

```bash
status
----------------------------------------------------------------------
Status: sealing
Block number:16 at view:1110
----------------------------------------------------------------------
```

#### 4.2.3 p2p.list命令

运行p2p.list命令，查看与本节点链接的其他节点:

```bash
p2p.list
----------------------------------------------------------------------
Peers number: 1
----------------------------------------------------------------------
Nodeid: b69fceef5cafad360bc1fad750d9c5ef71627da21527d8621e566a1f4184408a42248492f4ee721240eb151b1245fdba3e4d2cc6aa3f5b6aa35b82224805eedb
Ip: 127.0.0.1
Port:30304
Connected: 1
----------------------------------------------------------------------
```

#### 4.2.4 p2p.update命令

运行p2p.update命令，动态加载配置文件中的P2P信息，与新加入的节点建立连接:

```bash
Add staticNode:
----------------------------------------------------------------
node.0 : 127.0.0.1:30300
----------------------------------------------------------------
update successfully！
----------------------------------------------------------------
```

#### 4.2.5 miner.list命令

运行miner.list命令，查看网络中的共识节点:

```bash
miner.list
----------------------------------------------------------------------
Miners number: 2
----------------------------------------------------------------------
Nodeid: 73238478cd75c754d291d01d04c7dc5dacfdd9418318efb08125e359d6466efa65f0cec026f677343d84d8d981bacd04e5ea9344692286d83ab604964838a168
Nodeid: b69fceef5cafad360bc1fad750d9c5ef71627da21527d8621e566a1f4184408a42248492f4ee721240eb151b1245fdba3e4d2cc6aa3f5b6aa35b82224805eedb
----------------------------------------------------------------------
```

#### 4.2.6 miner.add命令

运行miner.add命令添加共识节点，该指令需要NodeID作为参数，新添加的共识节点将在下个块开始生效。操作完该指令后，请启动新添加的共识节点，并在新添加的共识节点配置文件`P2P.node.x`中配置已有节点的IP和P2P端口。
```bash
miner.add 44d80249498aaa4384b5d1393b93b586415a9333e1d85a8dd18ab1618e391c86e7acb9b5082e12cdfd352e49bd724f83d4b8267f5de552aa420b43a8b834e48e
add miner successfully: 44d80249498aaa4384b5d1393b93b586415a9333e1d85a8dd18ab1618e391c86e7acb9b5082e12cdfd352e49bd724f83d4b8267f5de552aa420b43a8b834e48e
----------------------------------------------------------------------
```

#### 4.2.7 miner.remove命令

运行miner.add命令移除共识节点，该指令需要NodeID作为参数，被移除的节点将转为观察节点。
```bash
miner.remove 44d80249498aaa4384b5d1393b93b586415a9333e1d85a8dd18ab1618e391c86e7acb9b5082e12cdfd352e49bd724f83d4b8267f5de552aa420b43a8b834e48e
remove miner successfully: 44d80249498aaa4384b5d1393b93b586415a9333e1d85a8dd18ab1618e391c86e7acb9b5082e12cdfd352e49bd724f83d4b8267f5de552aa420b43a8b834e48e
----------------------------------------------------------------------
```

#### 4.2.8 amdb.select命令

运行amdb.select命令，需要两个参数，即表名和主键字段值。现在查询t_test表，主键字段值为fruit的记录，查询结果显示如下

```bash
amdb.select t_test fruit
------------------------------------------------------------------------
Number of entry: 2
------------------------------------------------------------------------
_hash_: 8be12da5e82b858742a3b5b3dec84b2b43e55c9ae858f4f4a09bacc09bb841dc
_id_: 1
_num_: 3
_status_: 0
item_id: 1
item_name: apple
name: fruit
------------------------------------------------------------------------
_hash_: e85a9578bfdd2e54953b9222af494390b794567984eed95222e0e07819317075
_id_: 2
_num_: 6
_status_: 0
item_id: 1
item_name: orange
name: fruit
------------------------------------------------------------------------
```
entry中的key解释如下：
* \_hash\_ ：记录的区块hash，属于系统字段，系统自动建立。
* \_id\_ ：记录的id，表的自增字段，属于系统字段，系统自动建立。
* \_num\_ ：记录的区块高度，属于系统字段，系统自动建立。
* \_status\_ ：记录的状态，值为0表示正常状态，值为1表示删除状态，属于系统字段，系统自动建立。
* item_id ：t_test表自定义字段。
* item_name ：t_test表自定义字段。
* name ：t_test表自定义字段，name字段是t_test表的主键。


#### 4.2.9 quit命令

运行quit命令，退出控制台:
```bash
quit
Connection closed by foreign host.
```

## 第五章 部署合约、调用合约

智能合约是部署在区块链上的应用。开发者可根据自身需求在区块链上部署各种智能合约。

智能合约通过solidity语言实现，用fisco-solc进行编译。本章以HelloWorld.sol智能合约为例介绍智能合约的部署和调用。

### 5.1 配置

> 安装依赖环境

1. 下载对应平台的solidity编译器, 直接下载后放入系统目录下。

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

2. 安装nodejs

```shell
#CentOS安装nodejs
sudo yum install -y nodejs
```

```shell
#Ubuntu安装nodejs 需要6以上的版本
curl -sL https://deb.nodesource.com/setup_6.x | sudo -E bash -
sudo apt-get install -y nodejs npm
```

3. 安装node依赖

```shell
# 安装 cnpm
sudo npm install -g secp256k1
sudo npm install -g cnpm --registry=https://registry.npm.taobao.org
sudo cnpm install -g babel-cli babel-preset-es2017 ethereum-console
echo '{ "presets": ["es2017"] }' > ~/.babelrc

cd FISCO-BCOS/web3lib
cnpm install #安装nodejs依赖, 在执行nodejs脚本之前该目录下面需要执行一次, 如果已经执行过, 则忽略。

cd ../tool
cnpm install #该命令在该目录执行一次即可, 如果之前已经执行过一次, 则忽略。
```

> 设置区块链节点http_listen_port端口

```shell
vim ../web3lib/config.js
```

> 仅需将proxy指向区块链节点的http_listen_port端口即可。http_listen_port端口在节点的config.conf中查看。

```javascript
var proxy="http://127.0.0.1:30302";
```

### 5.2 部署合约

#### 5.2.1 实现合约

```shell
cd FISCO-BCOS/tool
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

#### 5.2.2 编译+部署合约

请先确认config.js已经正确指向区块链节点的http_listen_port端口，且相应的区块链节点已经启动。

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

### 5.3 调用合约

#### 5.3.1 编写合约调用程序

> 用nodejs实现，具体实现方法请直接看demoHelloWorld.js源码

```shell
vim demoHelloWorld.js
```

#### 5.3.2 调用合约

> 执行合约调用程序

```shell
babel-node demoHelloWorld.js
```

> 可看到合约调用成功

```log
{ HttpProvider: 'http://127.0.0.1:30302',
  Ouputpath: './output/',
  privKey: 'bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd',
  account: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3' }
HelloWorldcontract address:0xa807685dd3cf6374ee56963d3d95065f6f056372
HelloWorld contract get function call first :Hi,Welcome!
send transaction success: 0x6463e0ea9db6c4aff1e3fc14d9bdb86b29306def73e6d951913a522347526435
HelloWorld contract set function call , (transaction hash ：0x6463e0ea9db6c4aff1e3fc14d9bdb86b29306def73e6d951913a522347526435)
HelloWorld contract get function call again :HelloWorld!
```

### 5.4 监控连接和块高

monitor.js脚本监控节点的连接情况和块高。在运行前，请确认：

（1）被监控的区块链节点已经启动。

（2）config.js正确配置，proxy字段指向了需要监控的区块链节点的http_listen_port端口。

> 配置config，并执行monitor.js

```shell
cd /mydata/FISCO-BCOS/systemcontract/
vim ../web3lib/config.js
babel-node monitor.js
```

> 可看到不断刷出连接信息和块高。已连接节点数表示的是被查询的节点与其它节点连接的个数。此例子中，网络中有2个节点。被查询的节点是创世节点，与创世节点连接的节点只有一个，所以已连接节点数为1。

```log
current blocknumber 29691
the number of connected nodes：1
...........Node 0.........
NodeId:838a187e32e72e3889330c2591536d20868f34691f1822fbcd43cb345ef437c7a6568170955802db2bf1ee84271bc9cba64fba87fba84e0dba03e5a05de88a2c
Host:127.0.0.1:30403
--------------------------------------------------------------
```

## 第六章 多记账节点组网

### 6.1 创建初始有2个节点的区块链网络

1. 准备2个节点的环境

请参考[第三章](#第三章-手工部署单节点区块链网络)内容操作准备`node-0`和`node-1`两个节点文件夹以及相关配置文件，或者使用1.2.4节介绍的`FISCO-BCOS/scripts/build_chain.sh`脚本。手动操作完成`node-0`和`node-1`环境准备后，修改两个节点的`config.conf`文件中的`[pbft].miner.0,[pbft].miner.1,[p2p].node.0,[p2p].node.1`以及相关的端口号后，再分别启动两个节点。示例如下：
  - **node-0/config.conf**
```bash
[rpc]
	listen_ip=0.0.0.0
	listen_port=30301
	http_listen_port=30302
	console_port=30303
[p2p]
	listen_ip=0.0.0.0
	listen_port=30300
	node.0=192.168.2.1:30300
	node.1=192.168.2.2:30304
[pbft]
	miner.0=d23a6bad030a395b4ca3f2c4fa9a31ad58411fe8b6313472881d88d1fa3feaeab81b0ff37156ab3b1a69350115fd68cc2e4f2490ce01b1d7b4d8e22de00aea71
	miner.1=84fb96c26a919cd654107464f49ac08ca6301cdb4ba276a7193f1475f5733f714f0c3ac1cb80cd7279613755782fd590f0f0a758046801acc691475efae0830b
[common]
	data_path=data/
	log_config=log.conf
[secure]
	key=${DATAPATH}/node.key
	cert=${DATAPATH}/node.crt
	ca_cert=${DATAPATH}/ca.crt
[statedb]
	type=leveldb
	path=${DATAPATH}/statedb
	topic=DB
```

  - **node-1/config.conf**（隐去相同内容）
```bash
[rpc]
	listen_ip=0.0.0.0
	listen_port=30305
	http_listen_port=30306
	console_port=30307
[p2p]
	listen_ip=0.0.0.0
	listen_port=30304
	node.0=192.168.2.1:30300
	node.1=192.168.2.2:30304
[pbft]
	miner.0=d23a6bad030a395b4ca3f2c4fa9a31ad58411fe8b6313472881d88d1fa3feaeab81b0ff37156ab3b1a69350115fd68cc2e4f2490ce01b1d7b4d8e22de00aea71
	miner.1=84fb96c26a919cd654107464f49ac08ca6301cdb4ba276a7193f1475f5733f714f0c3ac1cb80cd7279613755782fd590f0f0a758046801acc691475efae0830b	
```

3. 启动全部节点并检查

```bash
# 在节点0的目录下操作
cd node-0
./start.sh
# 在节点1的目录下操作
cd node-1
./start.sh
```

### 6.2 使用控制台增删共识节点

在已经运行的网络中添加共识节点，参考以下步骤操作：
1. 参考[第三章](#第三章-手工部署单节点区块链网络)生成新节点的配置
1. 使用控制台`miner.add`指令添加新节点的NodeID到网络中
1. 在新节点的`config.conf`中`[p2p].node`列表中添加已有节点的IP和p2p端口。
1. 启动新节点
1. 检查节点的运行状态，参考[3.5 验证节点启动](#34-验证节点启动)

## 第七章 FISCO BCOS 特性

FISCO BCOS的特性，请直接参看相关特性说明文档：

1. [AMOP（链上链下）](../amop使用说明文档.md)
1. [AMDB使用说明文档.md](../AMDB使用说明文档.md)
1. [Contract_Name_Service](../CNS_Contract_Name_Service_服务使用说明文档.md)
1. EthCall [设计文档](../EthCall设计文档.md) [说明文档](../EthCall说明文档.md)
1. [web3sdk](../web3sdk使用说明文档.md)
1. [并行计算](../并行计算使用说明文档.md)
1. [分布式文件系统](../分布式文件系统使用说明.md)
1. [监控统计日志](../监控统计日志说明文档.md)
1. [同态加密](../同态加密说明文档.md)
1. [机构证书准入](../CA机构身份认证说明文档.md)
1. [可监管的零知识证明](../可监管的零知识证明说明.md)
1. [群签名环签名](../启用_关闭群签名环签名ethcall.md)
1. [弹性联盟链共识框架](../弹性联盟链共识框架说明文档.md)

## 第八章 附录

### 8.1 源码目录结构说明


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
|libinitializer     | 初始化所有组件|
| libp2p            | 区块链P2P网络主目录。如握手、网络包编解码、会话管理等等            |
| libpaillier       | 同态加密算法目录                                 |
|libprecompiled     |Precompiled框架|
| libpbftseal       | PBFT共识插件实现目录                             |
| libraftseal       | RAFT共识插件实现目录                             |
| libstatistics     | 访问频率统计与控制实现目录                            |
|libstorage         |分布式存储|
| libweb3jsonrpc    | web3 RPC实现目录                             |
| sample            | 一键安装与部署                                  |
| scripts           | 相关的脚本                                 |
| systemproxy    | 系统合约实现目录                                 |

### 8.2 cryptomod.json说明

FISCO BCOS区块链节点支持加密通信，在工具配置文件（cryptomod.json）中配置区块链节点间的加密通信方式。

| 配置项               | 说明                                       |
| ----------------- | ---------------------------------------- |
| cryptomod         | NodeId生成模式  默认填0                         |
| rlpcreatepath     | NodeId生成路径,文件名必须为network.rlp 如：/mydata/nodedata-1/data/network.rlp |
| datakeycreatepath | datakey生成路径 填空                           |
| keycenterurl      | 远程加密服务 填空                                |
| superkey          | 本地加密服务密码 填空                              |

### 8.3 [genesis.json说明](../../genesis.json)

创世块文件genesis.json关键字段说明如下：

| 配置项            | 说明                                       |
| -------------- | ---------------------------------------- |
| timestamp      | 创世块时间戳(毫秒)                               |
| god            | 内置链管理员账号地址|
| alloc          | 内置合约数据                                   |
| initMinerNodes | 节点NodeId |

### 8.4 [config.conf说明](../../config.conf)

节点配置文件config.json关键字段说明如下：

**注意：rpcport 和 channelPort 仅限于被机构内的监控、运维、sdk等模块访问，切勿对外网开放。**

| 配置项       | 默认值|说明                      |
| ------------ |-------|------------------------- |
|common.data_path|data/|数据存储目录，变量${DATAPATH} == data_path|
|common.log_config|log.conf|日志配置文件路径|
|common.ext_header|0|1.3兼容需要字段|
|secure.key|${DATAPATH}/node.key|节点私钥|
|secure.cert|${DATAPATH}/node.crt|节点证书|
|secure.ca_cert|${DATAPATH}/ca.crt|链证书|
|secure.ca_path|空|可加载指定路径下的ca相关文件|
|statedb.type|leveldb|可选leveldb或amop|
|statedb.path|${DATAPATH}/statedb|leveldb存储路径|
|statedb.retryInterval|1|重连AMDB间隔时间|
|statedb.maxRetry|0|amopdb的最大重试次数 超出次数后程序退出 默认为0(无限)|
|statedb.topic|DB|amopdb关注的topic|
|pbft.block_interval|1000|出块间隔 单位ms|
|pbft.miner.0|共识节点NodeID|共识节点列表`miner.x`，**所有联网节点配置文件中该列表必须相同**|
|rpc.listen_ip|127.0.0.1|监听IP，建议设置为内网IP，禁止外网访问|
|rpc.listen_port|30301|AMDB/web3sdk使用的端口|
|rpc.http_listen_port|30302|RPC请求使用的端口|
|rpc.console_port|30303|控制台监听端口|
|p2p.listen_ip|0.0.0.0|p2p监听IP|
|p2p.listen_port|30300|p2p端口|
|p2p.idle_connections|100|p2p最大连接数 默认为100|
|p2p.reconnect_interval|60|p2p重连间隔 单位秒 默认为60s|
|p2p.node.0|127.0.0.1:30300|配置区块链节点列表 格式为ip:端口 以node.开头|

### 8.5 log.conf说明

日志配置文件[log.conf](../../log.conf)关键字段说明如下：

| 配置项                 | 说明                                       |
| ------------------- | ---------------------------------------- |
| FORMAT              | 日志格式，典型如%level                           |
| FILENAME            | 例如/mydata/nodedata-1/log/log_%datetime{%Y%M%d%H}.log |
| MAX_LOG_FILE_SIZE   | 最大日志文件大小                                 |
| LOG_FLUSH_THRESHOLD | 超过多少条日志即可落盘                              |


### 8.6 常见问题

#### 8.6.1 脚本执行时，格式错误

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
