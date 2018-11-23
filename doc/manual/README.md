# FISCO BCOS **v1.5.0**区块链操作手册 

[FISCO-BCOS Manual](README_EN.md)

## 前言

本文档是FISCO BCOS区块链开源平台的一部分。
FISCO BCOS平台是金融区块链合作联盟（深圳）（以下简称：金链盟）开源工作组以金融业务实践为参考样本，在BCOS开源平台基础上进行模块升级与功能重塑，深度定制的安全可控、适用于金融行业且完全开源的区块链底层平台。  

金链盟开源工作组获得金链盟成员机构的广泛认可，并由专注于区块链底层技术研发的成员机构及开发者牵头开展工作。其中首批成员包括以下单位（排名不分先后）：博彦科技、华为、深证通、神州数码、四方精创、腾讯、微众银行、越秀金科。

FISCO BCOS平台基于现有的BCOS开源项目进行开发，聚焦于金融行业的分布式商业需求，从业务适当性、性能、安全、政策、技术可行性、运维与治理、成本等多个维度进行综合考虑，打造金融版本的区块链解决方案。

为了让大家更好的了解FISCO BCOS区块链开源平台的使用方法。本文档按照Step By Step的步骤详细介绍了FISCO BCOS区块链的构建、安装、启动，智能合约部署调用、多节点组网等内容的介绍。

本文档不介绍FISCO BCOS区块链设计理念及思路，详情请参看白皮书。

### 版本特性

新版本的设计原则是用户体验优先，在个这个原则下大幅度简化了1.3.x版本的操作步骤，减少了1.3.x版本的依赖，尤其是添加了二进制发行版，让用户可以跳过复杂的编译阶段。另一方面新版本添加了一些新的极其好用的功能，简要概括如下：
- [AMDB（Advanced Mass Database）](../AMDB使用说明文档.md)以支持海量数据存储
- 新增[初版控制台](#第三章-使用控制台)以方便查看节点信息，不再依赖nodejs脚本
- 更改配置文件格式为`ini`以方便查错
- 简化1.3.x部署系统合约相关步骤

在用户体验优先的原则下，新版本为了简化操作步骤导致与1.3.x版本有所差异，新版本与1.3.x版本在数据上兼容，而在连接层不兼容。1.3.x作为已经稳定的版本，会继续维护和优化（[1.3.x版本文档](https://fisco-bcos-documentation.readthedocs.io)）但不会加入AMDB特性。如开发和生产上已经使用了1.3.x的版本，可继续保持在1.3.x的版本上运行，如需使用AMDB支持海量数据存储等特性，可使用1.5版本。

**1.3.x版本获取链接 [请点这里](https://github.com/FISCO-BCOS/FISCO-BCOS/releases/tag/v1.3.4)，使用文档 [请点这里](https://fisco-bcos-documentation.readthedocs.io)**

> **连接层不兼容**：协议层不兼容是因为1.5版本的p2p去掉了所有协议包头的RLPFrameCodec，以前这个包头用来保存加密信息，现在用ssl取代，不再需要该包头。

### 目录

以下操作命令以在Centos7.2/Ubuntu 16.04操作系统为示例，手册目录如下

- [前言](#前言)
- [第一章 FISCO BCOS快速部署](#第一章-fisco-bcos快速部署)
- [第二章 手工部署单节点区块链网络](#第二章-手工部署单节点区块链网络)
- [第三章 使用控制台](#第三章-使用控制台)
- [第四章 节点管理](#第四章-节点管理)
- [第五章 部署合约、调用合约](#第五章-部署合约调用合约)
- [第六章 FISCO BCOS 特性](#第六章-fisco-bcos-特性)
- [第七章 附录](#第七章-附录)

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
| **JAVA**   |       | Java(TM) 1.8 && JDK 1.8 |

### 1.2 使用快速构建脚本

> 第二章是手动部署单节点网络的操作步骤，如果不想**手工操作**，可以尝试使用`FISCO-BCOS/scrtips/build_chain.sh`脚本，根据脚本提示设置参数，快速搭建区块链网络。

1. 快速构建4节点区块链

```bash
# 假设当前在用户目录下，执行下面命令下载构建脚本
curl -LO https://github.com/FISCO-BCOS/FISCO-BCOS/releases/download/v1.5.0-pre-release/build_chain.sh
# build_chain.sh
# Usage:
#	-l <IP list>                [Required] "ip1:nodeNum1,ip2:nodeNum2" e.g:"192.168.0.1:2,192.168.0.2:3"
#	-f <IP list file>           split by line, "ip:nodeNum"
# 	-e <FISCO-BCOS binary path> Default download from github
# 	-a <CA Key>                 Default Generate a new CA
# 	-o <Output Dir>             Default ./nodes/
# 	-p <Start Port>             Default 30300
# 	-c <ClientCert Passwd>      Default 123456
# 	-k <Keystore Passwd>        Default 123456
# 	-s <StateDB type>           Default leveldb. if set -s, use amop
# 	-t <Cert config file>       Default auto generate
# 	-z <Generate tar packet>    Default no
# 	-h Help

# 生成节点配置文件，下面命令表示为127.0.0.1这个IP生成4个节点
# 默认使用leveldb存储，如需使用AMDB，添加 -s 选项
bash build_chain.sh -l "127.0.0.1:4"
```
> 脚本必须有-l或-f选项来指定ip。生成节点后，拷贝IP对应的节点文件夹到部署服务器启动节点。
> -f指定文件，按行分割，每行格式为IP:NUM，表示一个部署服务器IP和该服务器部署节点数，不同节点实例端口会自动分配。`-l`选项和`-f`类似，以`,`分割，例如`"IP1:NUM1,IP2:NUM2"`
> 脚本执行报错参考[build_chain.sh执行报错](#762-build_chainsh执行报错)

2. 启动节点

```bash
cd nodes
bash start_all.sh
```

3. 验证节点启动

```bash
# 检查进程启动
ps -ef |grep fisco-bcos
#app 19390     1  1 17:52 ?   00:00:05 fisco-bcos --config config.conf --genesis genesis.json 
```

```bash
#  查看日志输出
cd nodes/node_127.0.0.1_0
tail -f log/info* |grep ++++  #查看日志输出
# 可看到不断刷出打包信息，表示节点已经正确启动！
#INFO|2018-08-12 17:52:16:877|+++++++++++++++++++++++++++ Generating seal ondcae019af78cf04e17ad908ec142ca4e25d8da14791bda50a0eeea782ebf3731#1tx:0,maxtx:1000,tq.num=0time:1513072336877
#INFO|2018-08-12 17:52:17:887|+++++++++++++++++++++++++++ Generating seal on3fef9b23b0733ac47fe5385072f80fc036b7517abae0a3e7762739cc66bc7dca#1tx:0,maxtx:1000,tq.num=0time:1513072337887
```

到这里已经成功部署一条由4个节点组成的区块链，**接下来请参考[第三章 使用控制台](#第三章-使用控制台)继续探索FISCO-BCOS**，想了解脚本操作细节请参考第二章内容。

## 第二章 手工部署单节点区块链网络

### 2.1 准备`fisco-bcos`软件

#### 2.1.1 下载官方二进制包[推荐]

```bash
# FISCO-BCOS/ 目录下操作
curl -LO https://github.com/FISCO-BCOS/FISCO-BCOS/releases/download/v1.5.0-pre-release/fisco-bcos
# 添加可执行权限
chmod a+x fisco-bcos
```

#### 2.1.2 源码编译

1. 获取源码

> 假定在用户目录下获取源码：

```bash
# clone源码
git clone https://github.com/FISCO-BCOS/FISCO-BCOS.git
# 切换到源码根目录
cd FISCO-BCOS 
git checkout prerelease-1.5
```

源码目录说明请参考[附录：源码目录结构说明](#71-源码目录结构说明)

2. 安装编译依赖

> 项目根目录下执行（若执行出错，请参考[常见问题1](#76-常见问题)）：

```shell
bash scripts/install_deps.sh
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

### 2.2 生成链证书

FISCO-BCOS网络采用面向CA的准入机制，保障信息保密性、认证性、完整性、不可抵赖性。**FISCO-BCOS支持多级证书体系，可以是[链、机构和节点的三级体系](#76-三级证书配置)，也可以链和节点两级体系**。以链和节点两级证书体系为例，一条链拥有一个链证书及对应的链私钥，链私钥由链管理员拥有。并对每个参与该链的节点签发节点证书。节点证书是节点身份的凭证，并使用该证书与其他节点间建立SSL连接进行加密通讯。本章介绍手动搭建一个单节点的区块链网络，包括链证书、节点私钥、节点证书、配置文件的生成和配置。生成方法如下：

```bash
# 切换到FISCO-BCOS/scripts目录下，创建nodes目录
mkdir nodes
# 拷贝相关脚本到nodes目录
cp chain.sh node.sh sdk.sh nodes/
# 进入nodes目录，并运行chain.sh脚本
cd nodes
bash chain.sh  #会提示输入相关证书信息，默认可以直接回车
```
> FISCO-BCOS/scripts/nodes目录下将生成链证书相关文件，包括链证书ca.crt, 链私钥ca.key, 密钥参数server.param
> **注意：ca.key 链私钥文件请妥善保存**

### 2.3 生成节点密钥和证书

```bash
# 切换到FISCO-BCOS/scripts/nodes目录下
# node.sh需要同级目录下存在ca.key、ca.crt
# 如需要生成多个节点，则重复执行 node.sh 节点名称 即可，假设节点名为 node-0，运行
# Usage:node.sh nodeName [CLIENTCERT_PWD] [KEYSTORE_PWD]
bash node.sh node-0 123456 123456
```

> **注意：node.key 节点私钥文件请妥善保存**。`FISCO-BCOS/scripts/nodes/`目录下将生成节点目录`node-0`。`node-0/data`目录下将有链证书`ca.crt`、节点私钥`node.key`、节点证书`node.crt`相关文件。
> FISCO-BCOS/scripts/nodes/node-0目录下将自动生成sdk目录，请将sdk目录下所有文件拷贝到SDK端的证书目录下。

### 2.4 准备节点环境

> 假定节点目录为FISCO-BCOS/scripts/nodes/node-0，按如下步骤操作：

```shell
# 切换到项目根目录FISCO-BCOS/下 拷贝相关文件到node-0目录下
cp genesis.json config.conf log.conf scripts/start.sh scripts/stop.sh scripts/nodes/node-0
# 拷贝fisco-bcos，如果是下载的fisco-bcos请执行cp fisco-bcos scripts/nodes/node-0
cp build/eth/fisco-bcos scripts/nodes/node-0
```

#### 2.4.1 获取NodeID

NodeID唯一标识了区块链中的某个节点，在节点启动前必须进行配置。

> nodes/node-0/data/node.nodeid文件中已计算好NodeID，也可以使用下面命令获取

```bash
# nodes/node-0/data/node.key为节点私钥
openssl ec -in nodes/node-0/data/node.key -text 2> /dev/null | perl -ne '$. > 6 and $. < 12 and ~s/[\n:\s]//g and print' | perl -ne 'print substr($_, 2)."\n"'
# 得到类似下面的NodeID
# d23a6bad030a395b4ca3f2c4fa9a31ad58411fe8b6313472881d88d1fa3feaeab81b0ff37156ab3b1a69350115fd68cc2e4f2490ce01b1d7b4d8e22de00aea71
```

#### 2.4.2 参数配置文件

配置使用INI管理，配置文件模板如下，本例中使用单个节点，需要将`nodes/node-0/config.conf`中的miner.0修改为上一步获取的NodeID。

```bash
# 假设当前目录为FISCO-BCOS/scripts
# 获取NodeID并复制
cat nodes/node-0/data/node.nodeid
# 修改 nodes/node-0/config.conf中miner.0=${NodeID}
vim nodes/node-0/config.conf
```

```bash
;rpc配置 web3sdk连接用
[rpc]
    ;强烈不建议暴露到外网，外网访问请修改为外网IP
    listen_ip=127.0.0.1
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
	; 配置链的CA证书
	ca_cert=${DATAPATH}/ca.crt

; 状态数据库配置
[statedb]
	; statedb的类型 可选leveldb/amop
	type=leveldb
	path=${DATAPATH}/statedb
	; amopdb关注的topic 默认为DB
	topic=DB
```

#### 2.4.3 配置log.conf（日志配置文件）

log.conf中配置节点日志生成的格式和路径，使用默认即可。
log.conf其它字段说明请参看[附录：log.conf说明](#75-logconf说明)

#### 2.4.3 AMDB数据代理配置[可选]

**当`config.conf`中配置项`statedb.type=amop`时**，[参考这里](../AMDB使用说明文档.md)配置并启动AMDB数据代理。

### 2.5 启动节点

节点的启动依赖下列文件，在启动前，请确认文件已经正确的配置：

- 创世块文件：genesis.json
- 节点配置文件：config.conf
- 日志配置文件：log.conf
- 证书文件（nodes/node-0/data/）：ca.crt、node.key、node.crt

> genesis.json其它字段说明请参看[附录：genesis.json说明](#73-genesisjson说明genesisjson)

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
# kill 13432 #13432是查看到的进程号
```

### 2.6 验证节点启动

1. 验证进程

```bash
ps -ef |grep fisco-bcos
```

```bash
# 看到进程启动
app 19390     1  1 17:52 ?        00:00:05 fisco-bcos --config config.conf --genesis genesis.json 
```

2. 查看日志输出

查看指定节点的共识信息（如nodes/node-0）

```bash
#  当前操作目录FISCO-BCOS/scripts/
cd nodes/node-0
tail -f log/info* |grep ++++  #查看日志输出
# 可看到不断刷出打包信息，表示节点已经正确启动！
#INFO|2018-08-12 17:52:16:877|+++++++++++++++++++++++++++ Generating seal ondcae019af78cf04e17ad908ec142ca4e25d8da14791bda50a0eeea782ebf3731#1tx:0,maxtx:1000,tq.num=0time:1513072336877
#INFO|2018-08-12 17:52:17:887|+++++++++++++++++++++++++++ Generating seal on3fef9b23b0733ac47fe5385072f80fc036b7517abae0a3e7762739cc66bc7dca#1tx:0,maxtx:1000,tq.num=0time:1513072337887
```

## 第三章 使用控制台

### 3.1 连接控制台

查看节点的配置文件config.conf中的ip和console port：
```ini
[rpc]
   listen_ip=127.0.0.1
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

========================================================================
Welcome to the FISCO BCOS console!
Version: 1.5.0 by FISCO, (c)2016-2018.

Type 'help' for command list. Type 'quit' to quit the console.
========================================================================
```

### 3.2 控制台命令使用

#### 3.2.1 help命令

首先输入help，查看可用的命令：

```bash
help
========================================================================
status          Show the blockchain status.
p2p.list        Show the peers information.
p2p.update      Update p2p nodes.
pbft.list       Show pbft consensus node list.
pbft.add        Add pbft consensus node.
pbft.remove     Remove pbft consensus node.
amdb.select     Query the table data.
quit            Quit the blockchain console.
help            Provide help information for blockchain console.
------------------------------------------------------------------------
```

#### 3.2.2 status命令

运行命令，查看节点的状态，块高和view:

```bash
status
========================================================================
Status: sealing
Block number:5 at view:100
------------------------------------------------------------------------
```

#### 3.2.3 p2p.list命令

运行命令，查看与本节点链接的其他节点:

```bash
p2p.list
========================================================================
Peers number: 3
------------------------------------
Nodeid: 223fc0cd...
Ip: 127.0.0.1
Port:64227
Online: true
------------------------------------
Nodeid: b62c9ff9...
Ip: 127.0.0.1
Port:64231
Online: true
------------------------------------
Nodeid: 7570a7c0...
Ip: 127.0.0.1
Port:64235
Online: true
------------------------------------------------------------------------
```

#### 3.2.4 p2p.update命令

运行命令，动态加载配置文件中的P2P信息，与新加入的节点建立连接:

```bash
p2p.update
========================================================================
Add p2p nodes:
------------------------------------
node.0 : 127.0.0.1:30300
node.1 : 127.0.0.1:30304
node.2 : 127.0.0.1:30308
node.3 : 127.0.0.1:30312
Update p2p nodes successfully！
------------------------------------------------------------------------
```

#### 3.2.5 pbft.list命令

运行命令，查看网络中的共识节点:

```bash
pbft.list
========================================================================
Consensus nodes number: 4
------------------------------------
NodeId: 7570a7c0...
IP: 127.0.0.1
Port:64227
Online: true
------------------------------------
NodeId: b62c9ff9...
IP: 127.0.0.1
Port:64231
Online: true
------------------------------------
NodeId: 223fc0cd...
IP: 127.0.0.1
Port:64235
Online: true
------------------------------------
NodeId(local): 58903c7f...
IP: 0.0.0.0
Port:30300
------------------------------------------------------------------------
```

#### 3.2.6 pbft.add命令

运行命令添加共识节点，该指令需要NodeID作为参数，新添加的共识节点将在下个块开始生效。操作完该指令后，请启动新添加的共识节点，并在新添加的共识节点配置文件`P2P.node.x`中配置已有节点的IP和P2P端口。
```bash
# 例如添加一个新的共识节点
pbft.add 44d80249498aaa4384b5d1393b93b586415a9333e1d85a8dd18ab1618e391c86e7acb9b5082e12cdfd352e49bd724f83d4b8267f5de552aa420b43a8b834e48e
========================================================================
Tx(Add consensus node) send successfully!
------------------------------------------------------------------------
```

#### 3.2.7 pbft.remove命令

运行命令移除共识节点，该指令需要NodeID作为参数，被移除的节点将转为观察节点。
```bash
pbft.remove 44d80249498aaa4384b5d1393b93b586415a9333e1d85a8dd18ab1618e391c86e7acb9b5082e12cdfd352e49bd724f83d4b8267f5de552aa420b43a8b834e48e
========================================================================
Tx(Remove consensus node) send successfully!
------------------------------------------------------------------------
```

#### 3.2.8 amdb.select命令

运行amdb.select命令，需要两个参数，即表名和主键字段值。现在查询t_test表，主键字段值为fruit的记录，查询结果显示如下

```bash
amdb.select t_test fruit
========================================================================
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


#### 3.2.9 quit命令

运行quit命令，退出控制台:
```bash
quit
Connection closed by foreign host.
```

## 第四章 节点管理

目前FISCO-BCOS网络中的节点有共识节点和观察节点两种身份，本章将分别介绍如何新添加共识节点、新添加观察节点以及节点类型转换。

### 4.1 新加观察节点

假设已有节点`node-0`，下面以添加`node-1`操作为例：

1. 准备`node-1`节点环境

```bash
# 假设当前在FISCO-BCOS/scripts目录下，拷贝scripts/node.sh脚本到scripts/nodes目录下
cp node.sh nodes/
cd nodes
# 生成node-1证书相关文件，nodes目录下需要有ca.key和ca.crt
bash node.sh node-1
```

2. 拷贝`node-0/config.conf,node-0/genesis.json,node-0/fisco-bcos`文件到`node-1/`
```bash
# 拷贝node-0的配置文件
cp node-0/* node-1/
```

3. 修改`node-1/config.conf`文件中监听的IP和端口，添加[p2p].node.1，填入新节点P2P监听IP和端口。

```bash
[rpc]
    listen_ip=127.0.0.1
    listen_port=30305
    http_listen_port=30306
    console_port=30307
[p2p]
    listen_ip=0.0.0.0
    listen_port=30304
    node.0=127.0.0.1:30300
    node.1=127.0.0.1:30304
```

4. 启动新节点
```bash
cd node-1
bash start.sh
```

5. 验证节点是否已加入网络，参考[第三章](#第三章-使用控制台)中`status`和`p2p.list`。
```bash
# 连接控制台
telnet 127.0.0.1 30307
# 查看连接的节点
p2p.list
# 可以看到已经连接了节点node-0
p2p.list
========================================================================
Peers number: 1
------------------------------------
Nodeid: d23a6bad...
Ip: 0.0.0.0
Port:30300
------------------------------------------------------------------------
```

### 4.2 新加共识节点

在已经运行的网络中添加共识节点，参考以下步骤操作：
1. 参考上一节[新加观察节点](#41-新加观察节点)，首先将新节点添加为观察节点
1. 使用[控制台](#第三章-使用控制台)中`pbft.add`指令添加新节点的NodeID到网络中，节点的`data/node.nodeid`文件中是节点的nodeID。
```bash
# 连接控制台 telnet IP console_port
telnet 127.0.0.1 30303
# 控制台中执行
pbft.add 68af18b8665aa0737f0a435bee975d9667405ee96696c7fdeee4b2164eb51c859bffb37f54dcb1ff28aa51f45d28686605c025cb46f9f8d8a2e6c3597c55d0fe
========================================================================
Tx(Add consensus node) send successfully!
------------------------------------------------------------------------
```
3. 启动新节点。
```bash
cd node-1
bash start.sh
```
4. 验证节点是否是共识节点，参考[第三章](#第三章-使用控制台)中`pbft.list`。
```bash
# 连接控制台 telnet IP console_port
telnet 127.0.0.1 30303
# 控制台中执行pbft.list查看共识节点列表
pbft.list
========================================================================
Consensus nodes number: 2
------------------------------------
Nodeid: 68af18b8...
Ip: 0.0.0.0
Port:30304
Online: true
------------------------------------
Nodeid(local): da7ffab9...
Ip: 0.0.0.0
Port:30300
------------------------------------------------------------------------
```

## 第五章 部署合约、调用合约

智能合约是部署在区块链上的应用。开发者可根据自身需求在区块链上部署各种智能合约。

智能合约通过solidity语言实现，用fisco-solc进行编译。本章以`HelloWorld.sol`智能合约为例介绍智能合约的部署和调用。

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
sudo apt-get install -y nodejs
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

```bash
cd FISCO-BCOS/tool/
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

## 第六章 FISCO BCOS 特性

FISCO BCOS的特性，请直接参看相关特性说明文档：

1. [AMOP（链上链下）](../amop使用说明文档.md)
1. [AMDB使用说明文档.md](../AMDB使用说明文档.md)
1. [Contract_Name_Service](../CNS_Contract_Name_Service_服务使用说明文档.md) （1.5.0暂不支持，下个版本加入）
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

## 第七章 附录

### 7.1 源码目录结构说明


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

### 7.2 cryptomod.json说明

FISCO BCOS区块链节点支持加密通信，在工具配置文件（cryptomod.json）中配置区块链节点间的加密通信方式。

| 配置项               | 说明                                       |
| ----------------- | ---------------------------------------- |
| cryptomod         | NodeId生成模式  默认填0                         |
| rlpcreatepath     | NodeId生成路径,文件名必须为network.rlp 如：nodedata-1/data/network.rlp |
| datakeycreatepath | datakey生成路径 填空                           |
| keycenterurl      | 远程加密服务 填空                                |
| superkey          | 本地加密服务密码 填空                              |

### 7.3 [genesis.json说明](../../genesis.json)

创世块文件genesis.json关键字段说明如下：

| 配置项            | 说明                                       |
| -------------- | ---------------------------------------- |
| timestamp      | 创世块时间戳(毫秒)                               |
| god            | 内置链管理员账号地址|
| alloc          | 内置合约数据                                   |
| initMinerNodes | 节点NodeId |

### 7.4 [config.conf说明](../../config.conf)

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

### 7.5 log.conf说明

日志配置文件[log.conf](../../log.conf)关键字段说明如下：

| 配置项                 | 说明                                       |
| ------------------- | ---------------------------------------- |
| FORMAT              | 日志格式，典型如%level                           |
| FILENAME            | 例如node-0/log/log_%datetime{%Y%M%d%H}.log |
| MAX_LOG_FILE_SIZE   | 最大日志文件大小                                 |
| LOG_FLUSH_THRESHOLD | 超过多少条日志即可落盘                              |

### 7.6 三级证书配置

如果要使用1.3版本的三级证书，请使用文本编辑器，将三个文件：按
	- node.crt
	- agency.crt
	- ca.crt
的顺序，合并成一个文件，作为node.crt使用

- 合并前，单个crt文件内容：
```bash
    -----BEGIN CERTIFICATE-----
    <crt文件内容>
    -----END CERTIFICATE-----
```

- 合并后，文件内容为：
```bash
-----BEGIN CERTIFICATE-----
<node.crt文件内容>
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
<agency.crt文件内容>
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
<ca.crt文件内容>
-----END CERTIFICATE-----
```

### 7.7 常见问题

#### 7.7.1 脚本执行时，格式错误

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

#### 7.7.2 `build_chain.sh`执行报错

```bash
# 报此错误是因为java环境问题，请使用OpenJDK-8以上版本
EC KeyFactory not available
```

#### 7.7.3 节点数据清空

- `levelDB`模式节点需要删除节点目录下`data`文件夹下的所有文件夹，注意`data`目录下的文件不能删除
- `amop`模式节点需要删除节点目录下`data`文件夹下的所有文件夹，注意`data`目录下的文件不能删除，**除此之外还需要清空所使用的数据库中的所有数据**。

#### 7.7.4 节点数据恢复

如果节点启动时出现`DatabaseAlreadyOpen`错误，请按照以下步骤检查：
1. 检查节点是否重复启动，如果该节点已经启动，请先停止已启动的节点
1. 如果确认没有重复启动的节点，尝试在启动fisco-bcos时增加--rescue参数
1. 如果失败，删除节点`data/pbftMsgBackup`目录，再次执行上一步
1. 如果仍有问题，按[7.7.3](#763-节点数据清空)的步骤，清理节点数据，再尝试重启
