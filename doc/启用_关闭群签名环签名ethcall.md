# 启用/关闭群签名&&环签名ethcall

## 目录
<!-- TOC -->

- [1 基本介绍](#1-基本介绍)
    - [1.1 场景](#11-场景)
    - [1.2 相关代码](#12-相关代码)
- [2 部署](#2-部署)
    - [2.1 开启群签名&&环签名ethcall](#21-开启群签名环签名ethcall)
    - [2.2 关闭群签名&&环签名ethcall](#22-关闭群签名环签名ethcall)
- [3 使用](#3-使用)
- [4 注意事项](#4-注意事项)

<!-- TOC -->

<br>
<br>

## 1 基本介绍

### 1.1 场景


FISCO-BCOS实现了群签名和环签名链上验证功能，主要包括如下使用场景：

**（1）群签名场景**


- **场景1**： 机构内成员（C端用户）或机构内下属机构通过机构将群签名信息上链，其他人在链上验证签名时，仅可获知签名所属的群组，却无法获取签名者身份，保证成员的匿名性和签名的不可篡改性；（如拍卖、匿名存证、征信等场景）
- **场景2**：B端用户将生成的群签名通过AMOP发送给上链结构（如fisco），上链机构将收集到的群签名信息统一上链（如竞标、对账等场景），其他人验证签名时，无法获取签名者身份，保证成员的匿名性，监管可通过可信第三方追踪签名者信息，保证签名的可追踪性。

<br>

**（2）环签名场景**


- **场景1（匿名投票）**：机构内成员（C端用户）对投票信息进行环签名，并通过可信机构（如fisco）将签名信息和投票结果写到链上，其他人可在链上验证签名时，仅可获取发布投票到链上的机构，却无法获取投票者身份信息

- **其他场景（如匿名存证、征信）**：场景与群签名匿名存证、征信场景类似，唯一的区别是任何人都无法追踪签名者身份

- **匿名交易**：在UTXO模型下，可将环签名算法应用于匿名交易，任何人都无法追踪转账交易双方；

<br>

[返回目录](#目录)

<br>


### 1.2  相关代码

FISCO BCOS提供了群签名&&环签名链上验证功能，下表详细介绍了相关代码模块：

| <div align = left>模块</div>             | <div align = left>代码</div>                                       | <div align = left>说明</div>                                       |
| -------------- | ---------------------------------------- | ---------------------------------------- |
| 依赖软件安装模块       | scripts/install_pbc.sh<br>deploy_pbc.sh  | 群签名库的实现依赖于pbc和pbc-sig库，可调用deploy_pbc.sh安装pbc和pbc-sig |
| 群签名&&环签名库源码    | deps/src/group_sig_lib.tgz               | 群签名&&环签名库源码压缩包                           |
| 编译模块           | cmake/FindPBC.cmake<br>cmake/ProjectGroupSig.cmake | 在FISCO BCOS中编译群签名&&环签名库源码相关的cmake文件      |
| 群签名&&环签名验证实现模块 | libevm/ethcall/EthcallGroupSig.h<br>libevm/ethcall/EthcallRingSig.h | 使用ethcall，调用群签名&&环签名库，实现群签名&&环签名链上验证功能   |

为了给用户提供最大的使用灵活性，FISCO BCOS支持群签名&&环签名ethcall开关功能（默认关闭群签名&&环签名ethcall）：

- **关闭群签名&&环签名ethcall验证功能**：群签名&&环签名库以及群签名&&环签名验证实现模块均没被编译到FISCO BCOS中，编译速度较快，但FISCO BCOS不支持群签名&&环签名链上验证功能；

- **开启群签名&&环签名ethcall验证功能**：群签名&&环签名库以及相关代码被编译到FISCO BCOS中，编译速度稍慢，但支持群签名&&环签名链上验证功能。

  下节详细描述了开启和关闭群签名&&环签名ethcall的部署方法。

<br>

[返回目录](#目录)

<br>
<br>

  
## 2 部署


开启和关闭群签名&&环签名ethcall之前，必须保证您的机器可正确部署FISCO BCOS，FISCO BCOS详细部署方法可参考[FISCO BCOS区块链操作手册](https://github.com/FISCO-BCOS/FISCO-BCOS/tree/master-1.3/doc/manual).

### 2.1 开启群签名&&环签名ethcall


**(1) 安装依赖软件**


**① 安装基础依赖软件**


部署FISCO BCOS之前需要安装git, dos2unix和lsof依赖软件：

- git: 用于拉取最新代码
- dos2unix && lsof: 用于处理windows文件上传到linux服务器时，可执行文件无法被linux正确解析的问题；

针对centos和ubuntu两种不同的操作系统，可分别用以下命令安装这些依赖软件：

(若依赖软件安装失败，请检查yum源或者ubuntu源是否配置正确)

```bash
[centos]
sudo yum -y install git
sudo yum -y install dos2unix
sudo yum -y install lsof

[ubuntu]
sudo apt install git
sudo apt install lsof
sudo apt install tofrodos
ln -s /usr/bin/todos /usr/bin/unxi2dos
ln -s /usr/bin/fromdos /usr/bin/dos2unix
```

**② 安装群签名依赖软件pbc和pbc-sig**


群签名依赖pbc和pbc-sig，因此打开群签名和环签名ethcall开关前，必须先安装pbc和pbc-sig，为了方便用户操作，FISCO BCOS提供了pbc和pbc-sig的部署脚本（支持在centos和ubuntu系统安装pbc和pbc-sig）:

```bash
#用dos2unix格式化可执行脚本，防止windows文件上传到unix，无法被正确解析
bash format.sh
#用deploy_pbc.sh脚本安装pbc和pbc-sig
sudo bash deploy_pbc.sh
```
<br>

**(2) 打开群签名&&环签名ethcall编译开关**


FISCO BCOS群签名和环签名相关的编译开关是在使用cmake生成Makefile文件时通过-DGROUPSIG=ON或者-DGROUPSIG=OFF设置，默认是关闭该开关的：

```bash
#新建build文件用于存储编译文件
cd FISCO-BCOS && mkdir -p build && cd build
#cmake生成Makefile时打开编译开关
#**centos系统：
cmake3 -DGROUPSIG=ON -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF ..
#**ubuntu系统:
cmake -DGROUPSIG=ON -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF ..
```

<br>

**(3) 编译并运行fisco bcos**

```bash
#编译fisco-bcos
make -j4 #注: 这里j4表明用4线程并发编译，可根据机器CPU配置调整并发线程数
#运行fisco-bcos: 替换节点启动的可执行文件，重启节点:
bash start.sh
```

<br>

[返回目录](#目录)

<br>


### 2.2 关闭群签名&&环签名ethcall

**(1) 关闭群签名&&环签名ethcall编译开关**


编译fisco bcos时，将cmake的-DGROUPSIG编译选项设置为OFF可关闭群签名&&环签名ethcall功能：

```bash
#新建build文件用于存储编译文件
cd FISCO-BCOS && mkdir -p build && rm -rf build/* && cd build
#关闭群签名&&环签名ethcall开关
#centos系统：
cmake3 -DGROUPSIG=OFF -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF ..
#ubuntu系统：
cmake -DGROUPSIG=OFF -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF ..
```
<br>

**(2) 编译并运行fisco bcos**

```bash
#编译fisco-bcos
make -j4 #注: 这里j4表明用4线程并发编译，可根据机器CPU配置调整并发线程数
#运行fisco-bcos: 替换节点启动的可执行文件，重启节点:
bash start.sh
```

<br>

[返回目录](#目录)

<br>



## 3 使用

在区块链上使用群签名&&环签名功能，还需要部署以下服务：

**(1) 群签名&&环签名客户端： [sig-service-client](https://github.com/FISCO-BCOS/sig-service-client)**

sig-service-client客户端提供了以下功能：

- 访问[群签名&&环签名服务](https://github.com/FISCO-BCOS/sig-service)rpc接口；
- 将群签名&&环签名信息写入链上（发交易）

具体使用和部署方法请参考[群签名&&环签名客户端操作手册](https://github.com/FISCO-BCOS/FISCO-BCOS/tree/master-1.3/doc/manual).

<br>

**(2) 群签名&&环签名服务：[sig-service](https://github.com/FISCO-BCOS/sig-service)**


sig-service签名服务部署在机构内，为群签名&&环签名客户端([sig-service-client](https://github.com/FISCO-BCOS/sig-service-client))提供了如下功能：

- 群生成、群成员加入、生成群签名、群签名验证、追踪签名者身份等rpc接口；
- 环生成、生成环签名、环签名验证等rpc接口；

在FISCO BCOS中，sig-service-client客户端一般先请求该签名服务生成群签名或环签名，然后将获取的签名信息写入到区块链节点；区块链节点调用群签名&&环签名ethcall验证签名的有效性。

sig-service的使用和部署方法请参考[群签名&&环签名RPC服务操作手册](https://github.com/FISCO-BCOS/sig-service).

<br>

[返回目录](#目录)

<br>



## 4 注意事项

**(1) 群签名&& 环签名ethcall兼容老版本FISCO BCOS** 

<br>

(2) 启用群签名&&环签名ethcall，并调用相关功能后，**不能关闭该ethcall接口，否则验证区块时，群签名和环签名相关的数据无法找到验证接口，从而导致链异常退出**


若操作者在开启并使用群签名&&环签名特性后，不小心关闭了该ethcall功能，可通过回滚fisco bcos到开启群签名&&环签名ethcall时的版本来使链恢复正常；

<br>

**(3) 同一条链的fisco bcos版本必须相同**


即：若某个节点开启了群签名&&环签名验证功能，其他链必须也开启该功能，否则在同步链时，无群签名&&环签名ethcall实现的节点会异常退出；

<br>

(4) 使用群签名&&环签名链上验证功能前，**必须先部署[群签名&&环签名服务](https://github.com/FISCO-BCOS/sig-service)和[群签名&&环签名客户端](https://github.com/FISCO-BCOS/sig-service-client)**

[客户端sig-service-client](https://github.com/FISCO-BCOS/sig-service-client)向链上发签名信息，[群签名&&环签名服务](https://github.com/FISCO-BCOS/sig-service)为客户端提供签名生成服务

<br>

[返回目录](#目录)

<br>

 