
### v3.4.0

(2023-06-14)

**新增**

* [RPC支持带签名的Call接口](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3611)
* [P2P动态加载黑白名单](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3621)

**修改**

* [升级TBB版本到2021.8.0](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3656)
* [优化同步场景读写锁的互斥范围](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3650)

**修复**

* [修复在极端场景下偶现的同步失效的bug](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3674)
* [修复交易回滚时只回滚部分合约的bug](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3629)
* [修复viewchange时标记交易操作中返回值处理的bug](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3654)
* [修复pro架构下断连场景中偶现的proxy为空的bug](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3684)
* [修复AMOP回调析构的bug](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3673)
* [修复BucketMap极端情况下的线程安全问题](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3666)
* [修复Session中反复创建多个task_group的问题](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3662)
* [修复轻节点发送交易因为编码问题导致Response为空的bug](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3670)
* [修复KeyPage在删空表中数据后可能触发的fatal](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3686)

**升级描述**

* 升级节点可执行程序

  效果：修复bug，并带来稳定性、性能的提升

  操作：停止节点服务，升级节点可执行程序为当前版本，重启节点服务

  注意事项：推荐逐步替换可执行程序进行灰度升级

  支持升级的版本：v3.0.0+

* 升级链数据版本

  效果：可使用当前版本的最新特性

  操作：先完成升级所有节点可执行程序，再参考[文档](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/introduction/change_log/upgrade.html#id1)发送交易升级链数据版本至 v3.4.0

  注意事项：务必备份原节点的所有账本数据，若操作失误造成升级失败，可通过原数据回滚到升级前的状态
  支持升级的版本：v3.0.0+

**组件兼容性**

|            | 推荐版本  | 最低版本                 | 说明                               |
| ---------- | --------- | ------------------------ | ---------------------------------- |
| WeBASE     | 3.0.2      | 3.0.2                   |                                    |
| WeIdentity | v3.0.0-rc.1| v3.0.0-rc.1             |                                    |
| Console    | 3.4.0     | 3.0.0                    |                                    |
| Java SDK   | 3.4.0     | 3.0.0                    |                                    |
| CPP SDK    | 3.4.0     | 3.0.0                    |                                    |
| Solidity   | 0.8.11    | 最低 0.4.25，最高 0.8.11 | 需根据合约版本下载编译器（控制台） |
| WBC-Liquid | 1.0.0-rc3 | 1.0.0-rc3                |                                    |

### 3.3.0

(2023-04-14)

**新增**

* [块内分片](https://fisco-bcos-doc.readthedocs.io/zh_CN/release-3.3.0/docs/design/parallel/sharding.html)：将合约分组，不同组的交易调度到不同执行器执行，片内DAG调度，片间DMC调度

* [权限动态可配]()：可在运行时动态关闭/开启权限功能
* [SDK支持硬件加密机](https://fisco-bcos-doc.readthedocs.io/zh_CN/release-3.3.0/docs/design/hsm.html)：SDK支持通过加密机运行密码学算法
* 网关入限速：通过配置文件 (config.ini) 控制入流量大小
* [Merkle树缓存](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3430)：提升取交易证明的性能
* 网关模块支持多CA：不同的链可共用同一个网关模块转发消息

**修改**

* 优化各种细节，提升节点性能
* rpc的交易接口返回input字段：可在配置文件中控制是否需要返回

**修复**

* 修复使用`3.2.0`版本二进制将链版本号从`3.0.0`到`3.1.0`及以上的版本触发的BFS不可用、链执行不一致的问题
* 修复`P2P`消息解析异常，导致网络断连的问题
* 修复`StateStorage`读操作时提交，导致迭代器失效的问题
* 修复`Pro`版本扩容操作没有生成节点私钥文件`node.pem`，扩容失败的问题
* 修复交易回执返回时，回执hash偶现不正确的问题

**兼容性**

* 历史版本升级

  需要升级的链的“数据兼容版本号（[compatibility_version](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/change_log/3_2_0.html#id5)）”为如下版本时：

  * 3.3.x：数据完全兼容，直接替换二进制即可完成升级
  * 3.2.x、3.1.x、3.0.x：支持通过替换二进制进行灰度升级，若需使用当前版本的新特性，需升级数据兼容版本号，操作见[文档](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/change_log/3_2_0.html#id5)
  * 3.0-rc x：数据不兼容，无法升级，可考虑逐步将业务迁移至3.x正式版
  * 2.x：数据不兼容，2.x版本仍持续维护，可考虑升级为2.x的最新版本

* 组件兼容性

|            | 推荐版本  | 最低版本                 | 说明                               |
| ---------- | --------- | ------------------------ | ---------------------------------- |
| Console    | 3.3.0     | 3.0.0                    |                                    |
| Java SDK   | 3.3.0     | 3.0.0                    |                                    |
| CPP SDK    | 3.3.0     | 3.0.0                    |                                    |
| Solidity   | 0.8.11    | 最低 0.4.25，最高 0.8.11 | 需根据合约版本下载编译器（控制台） |
| WBC-Liquid | 1.0.0-rc3 | 1.0.0-rc3                |                                    |

### 3.2.1

(2023-03-17)
**修复**

* 修复在使用`3.2.0`版本二进制将链版本号从3.0.0到3.1.0及以上的版本触发的BFS不可用、链执行不一致的问题
* 修复`P2P`消息解析异常，导致网络断连的问题
* 修复`StateStorage`读操作时提交，导致迭代器失效的问题
* 修复`Pro`版本扩容操作没有生成节点私钥文件`node.pem`，扩容失败的问题

**兼容性**

* 历史版本升级

  需要升级的链的“数据兼容版本号（[compatibility_version](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/change_log/3_2_0.html#id5)）”为如下版本时：

  * 3.2.0：数据完全兼容当前版本，直接替换二进制即可完成升级
  * 3.1.x/3.0.x：支持通过替换二进制进行灰度升级，若需使用当前版本的新特性，需升级数据兼容版本号，操作见[文档](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/change_log/3_2_0.html#id5)
  * 3.0-rc x：数据不兼容，无法升级，可考虑逐步将业务迁移至3.x正式版
  * 2.x：数据不兼容，2.x版本仍持续维护，可考虑升级为2.x的最新版本

* 组件兼容性

|            | 推荐版本  | 最低版本                 | 说明                               |
| ---------- | --------- | ------------------------ | ---------------------------------- |
| Console    | 3.2.0     | 3.0.0                    |                                    |
| Java SDK   | 3.2.0     | 3.0.0                    |                                    |
| CPP SDK    | 3.2.0     | 3.0.0                    |                                    |
| Solidity   | 0.8.11    | 最低 0.4.25，最高 0.8.11 | 需根据合约版本下载编译器（控制台） |
| WBC-Liquid | 1.0.0-rc3 | 1.0.0-rc3                |                                    |


### 3.2.0

(2023-01-17)

**新增**

* CRUD 新增更多接口
* 网关内白名单
* 适配硬件加密机
* 适配麒麟操作系统
* 新增EVM的analysis缓存，降低大合约的执行开销
* 出块时间可配置上限
* 数据归档工具
* tikv 读写工具
* max支持手动部署

**更改**

* 配置文件中重要字段去除默认值，必须在配置文件中进行配置
* INFO 日志优化日志大小

**修复**

* 超过3级跳转的消息路由问题
* rpc sendTransaction接口的交易哈希校验问题

**兼容性**

* 历史版本升级

  需要升级的链的“数据兼容版本号（[compatibility_version](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/change_log/3_2_0.html#id5)）”为如下版本时：

  * 3.2.0：数据完全兼容当前版本，直接替换二进制即可完成升级
  * 3.1.x/3.0.x：支持通过替换二进制进行灰度升级，若需使用当前版本的新特性，需升级数据兼容版本号，操作见[文档](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/change_log/3_2_0.html#id5)
  * 3.0-rc x：数据不兼容，无法升级，可考虑逐步将业务迁移至3.x正式版
  * 2.x：数据不兼容，2.x版本仍持续维护，可考虑升级为2.x的最新版本

* 组件兼容性

|            | 推荐版本  | 最低版本                 | 说明                               |
| ---------- | --------- | ------------------------ | ---------------------------------- |
| Console    | 3.2.0     | 3.0.0                    |                                    |
| Java SDK   | 3.2.0     | 3.0.0                    |                                    |
| CPP SDK    | 3.2.0     | 3.0.0                    |                                    |
| Solidity   | 0.8.11    | 最低 0.4.25，最高 0.8.11 | 需根据合约版本下载编译器（控制台） |
| WBC-Liquid | 1.0.0-rc3 | 1.0.0-rc3                |                                    |

### 3.1.2

(2022-01-03)

**新增**

* 交易结构新增extraData字段，以方便业务对交易进行标识，该字段不纳入交易hash的计算

**兼容性**

* 历史版本升级

  需要升级的链的“数据兼容版本号（[compatibility_version](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/change_log/3_1_2.html#id5)）”为如下版本时：

  * 3.1.0：数据完全兼容当前版本，直接替换二进制即可完成升级
  * 3.0.x：支持通过替换二进制进行灰度升级，若需使用当前版本的新特性，需升级数据兼容版本号，操作见[文档](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/change_log/3_1_2.html#id5)
  * 3.0-rc x：数据不兼容，无法升级，可考虑逐步将业务迁移至3.x正式版
  * 2.x：数据不兼容，2.x版本仍持续维护，可考虑升级为2.x的最新版本

* 组件兼容性

|            | 推荐版本  | 最低版本                 | 说明                               |
| ---------- | --------- | ------------------------ | ---------------------------------- |
| Console    | 3.1.2     | 3.0.0                    |                                    |
| Java SDK   | 3.1.2     | 3.0.0                    |                                    |
| CPP SDK    | 3.1.0     | 3.0.0                    |                                    |
| Solidity   | 0.8.11    | 最低 0.4.25，最高 0.8.11 | 需根据合约版本下载编译器（控制台） |
| WBC-Liquid | 1.0.0-rc3 | 1.0.0-rc3                |                                    |

### 3.1.1

(2022-12-07)

**新增**

* 支持在MacOS上通过搭链脚本（[`build_chain.sh`](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/tutorial/air/build_chain.html)）直接下载二进制搭链，无需手动编译节点二进制（[#3179](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3179)）

**修复**

* 共识模块快速视图切换的问题（[#3168](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3168)）
* 测试合约初始化逻辑修复（[#3182](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3182)）
* gateway回包问题修复（[#3197](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3197)）
* DMC执行时消息包类型错误修复（[#3198](https://github.com/FISCO-BCOS/FISCO-BCOS/pull/3198)）

**兼容性**

* 历史版本升级

  需要升级的链的“数据兼容版本号（[compatibility_version](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/change_log/3_1_1.html#id5)）”为如下版本时：

  * 3.1.0：数据完全兼容当前版本，直接替换二进制即可完成升级
  * 3.0.x：支持通过替换二进制进行灰度升级，若需使用当前版本的新特性，需升级数据兼容版本号，操作见[文档](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/change_log/3_1_1.html#id5)
  * 3.0-rc x：数据不兼容，无法升级，可考虑逐步将业务迁移至3.x正式版
  * 2.x：数据不兼容，2.x版本仍持续维护，可考虑升级为2.x的最新版本

* 组件兼容性

|            | 推荐版本  | 最低版本                 | 说明                               |
| ---------- | --------- | ------------------------ | ---------------------------------- |
| Console    | 3.1.0     | 3.0.0                    |                                    |
| Java SDK   | 3.1.1     | 3.0.0                    |                                    |
| CPP SDK    | 3.0.0     | 3.0.0                    |                                    |
| Solidity   | 0.8.11    | 最低 0.4.25，最高 0.8.11 | 需根据合约版本下载编译器（控制台） |
| WBC-Liquid | 1.0.0-rc3 | 1.0.0-rc3                |                                    |

### 3.1.0

(2022-11-22)

**新增**

* 账户冻结、解冻、废止功能
* 网关分布式限流功能
* 网络压缩功能
* 共识对时功能
* 合约二进制与ABI存储优化
* 适配EVM的delegatecall，extcodeHash，blockHash等接口
* BFS新增查询分页功能

**更改**

* DBHash 计算逻辑更新，提升校验稳定性
* chain配置项从config.ini中挪出，修改为在config.genesis创世块中配置
* BFS 目录表结构性能优化

**修复**

* 虚拟机接口功能问题： [#2598](https://github.com/FISCO-BCOS/FISCO-BCOS/issues/2598), [#3118](https://github.com/FISCO-BCOS/FISCO-BCOS/issues/3118)
* tikv client 问题：[#2600](https://github.com/FISCO-BCOS/FISCO-BCOS/issues/2598)
* 依赖库使用：[#2625](https://github.com/FISCO-BCOS/FISCO-BCOS/issues/2625)
* CRUD接口问题：[#2910](https://github.com/FISCO-BCOS/FISCO-BCOS/issues/2910)
* docker 镜像：[#3051](https://github.com/FISCO-BCOS/FISCO-BCOS/issues/3051)

**兼容性**

* 历史链数据

  当前链已有数据为如下版本时，是否可替换节点二进制完成升级

  * 3.0.x：支持通过替换二进制进行灰度升级，若需使用当前版本的新特性，需在所有节点二进制替换完成后用[控制台将链版本升级为当前版本](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/develop/console/console_commands.html#setsystemconfigbykey)
  * 3.0-rc x：数据不兼容，无法升级，可考虑逐步将业务迁移至3.x正式版
  * 2.x：数据不兼容，2.x版本仍持续维护，可考虑升级为2.x的最新版本

* 组件兼容性

|            | 推荐版本  | 最低版本                 | 说明                               |
| ---------- | --------- | ------------------------ | ---------------------------------- |
| Console    | 3.1.0     | 3.0.0                    |                                    |
| Java SDK   | 3.1.0     | 3.0.0                    |                                    |
| CPP SDK    | 3.0.0     | 3.0.0                    |                                    |
| Solidity   | 0.8.11    | 最低 0.4.25，最高 0.8.11 | 需根据合约版本下载编译器（控制台） |
| WBC-Liquid | 1.0.0-rc3 | 1.0.0-rc3                |                                    |

### 3.0.1

(2022-09-23)

**修复**

* 修复RPC 模块的内存增长问题
* 修复优雅退出问题
* 修复max版本稳定性文档

#### 兼容性

3.0.1 版本与3.0.0 版本数据完全兼容，Solidity合约源码兼容，但与2.0及3.0 rc版本不兼容。如果要从2.0版本升级到3.0版本，需要做数据迁移。

|            | 推荐版本  | 最低版本                 | 说明                               |
| ---------- | --------- | ------------------------ | ---------------------------------- |
| Console    | 3.0.1     | 3.0.0                    |                                    |
| Java SDK   | 3.0.1     | 3.0.0                    |                                    |
| CPP SDK    | 3.0.0     | 3.0.0                    |                                    |
| Solidity   | 0.8.11    | 最低 0.4.25，最高 0.8.11 | 需根据合约版本下载编译器（控制台） |
| WBC-Liquid | 1.0.0-rc3 | 1.0.0-rc3                |                                    |

### 3.0.0

（2022-08-24）

#### 新架构

**Air / Pro / Max** ：满足不同的部署场景

- **Air**：传统的区块链架构，所有功能在一个区块链节点中（all-in-one），满足开发者和简单场景的部署需求
- **Pro**：网关 + RPC + 区块链节点，满足机构内外部环境隔离部署需求
- **Max**：网关 + RPC + 区块链节点（主备） + 多个交易执行器，满足追求高可用和极致的性能的需求

#### 新机制

**流程：流水线共识**

以流水线的方式生成区块，提升性能

- 将区块生成过程拆分为四个阶段：打包，共识，执行，落盘
- 连续的区块在执行时以流水线的方式走过四个阶段（103在打包，102在共识，101在执行，100在落盘)
- 连续出块时，性能趋近于流水线中执行时间最长阶段的性能

**执行：确定性多合约并行**

实现合约间交易的并行执行与调度的机制

- 高效：不同合约的交易可并行执行，提高交易处理效率
- 易用：对开发者透明，自动进行交易并行执行与冲突处理
- 通用：支持EVM、WASM、Precompiled 或其它合约

**存储：KeyPage**

参考内存页的缓存机制实现高效的区块链存储

* 将key-value组织成页的方式存储
* 提升访存局部性，降低存储空间占用

**继承与升级**

* DAG并行执行：不再依赖基于并行编程框架，可根据solidity代码自动生成冲突参数，实现合约内交易的并行执行
* PBFT共识算法：立即一致的共识算法，实现交易秒级确认
* 更多请参考在线文档

#### 新功能

**区块链文件系统**

用命令行管理区块链资源，如合约，表等

- 命令：pwd, cd, ls, tree, mkdir, ln
- 功能：将合约地址与路径绑定，即可用路径调用合约

**权限治理**

开启后，对区块链的设置需进行多方投票允许

* 角色：治理者、管理员、用户
* 被控制的操作：部署合约、合约接口调用、系统参数设置等

**WBC-Liquid：WeBankBlockchain-Liquid(简称WBC-Liquid)**

不仅支持Soldity写合约，也支持用Rust写合约

- Liquid是基于Rust语言的智能合约编程语言
- 集成WASM运行环境，支持WBC-Liquid智能合约。
- WBC-Liquid智能合约支持智能分析冲突字段，自动开启DAG。

**继承与升级**

* Solidity：目前已支持至0.8.11版本
* CRUD：采用表结构存储数据，对业务开发更友好，3.0中封装了更易用的接口
* AMOP：链上信使协议，借助区块链的P2P网络实现信息传输，实现接入区块链的应用间的数据通信
* 落盘加密：区块链节点的私钥和数据加密存储于物理硬盘中，物理硬件丢失也无法解密
* 密码算法：内置群环签名等密码算法，可实现各种安全多方计算场景
* 更多请参考在线文档

#### 兼容性

3.0版本与以往各版本数据和协议不兼容，Solidity合约源码兼容。如果要从2.0版本升级到3.0版本，需要做数据迁移。

|            | 推荐版本  | 最低版本                 | 说明                               |
| ---------- | --------- | ------------------------ | ---------------------------------- |
| 控制台     | 3.0.0     | 3.0.0                    |                                    |
| Java SDK   | 3.0.0     | 3.0.0                    |                                    |
| CPP SDK    | 3.0.0     | 3.0.0                    |                                    |
| Console    | 3.0.0     | 3.0.0                    |                                    |
| Solidity   | 0.8.11    | 最低 0.4.25，最高 0.8.11 | 需根据合约版本下载编译器（控制台） |
| WBC-Liquid | 1.0.0-rc3 | 1.0.0-rc3                |                                    |



### 3.0.0-rc4

(2022-06-30)

**新增**

- 实现`Max`版本FISCO-BCOS, 存储采用分布式存储TiKV，执行模块独立成服务，存储和执行均可横向扩展，且支持自动化主备恢复，可支撑海量交易上链场景
- 从数据到协议层全面设计并实现兼容性框架，可保证协议和数据的安全升级
- 支持CRUD合约接口，简化区块链应用开发门槛
- 支持群环签名合约接口，丰富链上隐私计算能力
- 支持合约生命周期管理功能，包括合约冻结、解冻
- 支持数据落盘加密
- 基于`mtail` + `prometheus` + `grafana` + `ansiable`实现区块链系统监控


**更改**

- 引入KeyPage，优化读存储性能
- 基于Rip协议原理，实现网络转发功能，提升网络鲁棒性
- 支持linux aarch64平台
- 更新权限治理合约，将节点角色管理、系统配置修改、合约生命周期管理等功能纳入到治理框架
- 重构权限治理合约，计算逻辑可升级
- 优化DMC执行框架的性能
- 优化RPC和P2P的网络性能
- 优化`Pro`版FISCO-BCOS建链脚本，支持以机构维度配置RPC、Gateway、BcosNodeService等服务

**修复**

- 修复[#issue 2448](https://github.com/FISCO-BCOS/FISCO-BCOS/issues/2448)


**兼容性**

3.0.0-rc4版本与3.0.0-rc3版本数据和协议不兼容，Solidity/WBC-Liquid合约源码兼容。如果要从3.0.0-rc3版本升级到3.0.0-rc4版本，需要做数据迁移。

|            | 推荐版本                | 最低版本  | 说明                   |
| ---------- | ----------------------- | --------- | ---------------------- |
| 控制台     | 3.0.0-rc4                  | 3.0.0-rc4    |                        |
| Java SDK        | 3.0.0-rc4           | 3.0.0-rc4     |     |
| CPP SDK        | 3.0.0-rc4           | 3.0.0-rc4     |     |
| WeBASE     | 暂时不支持(预计lab-rc4版本支持)         | 暂时不支持(预计lab-rc4版本支持) | |
| Solidity   | 最高支持 solidity 0.8.11.0 | 0.6.10    |                        |
| Liquid     | 1.0.0-rc3               | 1.0.0-rc2  |                      |


### 3.0.0-rc3
(2022-03-31)

**新增**

- 支持Solidity合约并行冲突字段分析
- 将密码学、交易编解码等相关逻辑整合到bcos-cpp-sdk中，并封装成通用的C接口
- WASM虚拟机支持合约调用合约
- 新增bcos-wasm替代Hera
- `BFS`支持软链接功能
- 支持通过`setSystemConfig`系统合约的`tx_gas_limit`关键字动态修改交易执行的gas限制
- 部署合约存储合约ABI


**更改**

- 升级EVM虚拟机到最新，支持Solidity 0.8
- 机构层面优化网络广播，减少机构间外网带宽占用
- 支持国密加速库，国密签名和验签性能提升5-10倍
- EVM节点支持`BFS`，使用`BFS`替代`CNS`
- DAG框架统一支持Solidity和WBC-Liquid

**修复**

- 修复[#issue 2312](https://github.com/FISCO-BCOS/FISCO-BCOS/issues/2312)
- 修复[#issue 2307](https://github.com/FISCO-BCOS/FISCO-BCOS/issues/2307)
- 修复[#issue 2254](https://github.com/FISCO-BCOS/FISCO-BCOS/issues/2254)
- 修复[#issue 2211](https://github.com/FISCO-BCOS/FISCO-BCOS/issues/2211)
- 修复[#issue 2195](https://github.com/FISCO-BCOS/FISCO-BCOS/issues/2195)

**兼容性**

3.0.0-rc3版本与3.0.0-rc2版本数据和协议不兼容，Solidity/WBC-Liquid合约源码兼容。如果要从3.0.0-rc2版本升级到3.0.0-rc3版本，需要做数据迁移。

|            | 推荐版本                | 最低版本  | 说明                   |
| ---------- | ----------------------- | --------- | ---------------------- |
| 控制台     | 3.0.0-rc3                  | 3.0.0-rc3    |                        |
| Java SDK        | 3.0.0-rc3           | 3.0.0-rc3     |     |
| CPP SDK        | 3.0.0-rc3           | 3.0.0-rc3     |     |
| WeBASE     | 暂时不支持(预计lab-rc3版本支持)         | 暂时不支持(预计lab-rc2版本支持) | |
| Solidity   | 最高支持 solidity 0.8.11.0 | 0.6.10    |                        |
| Liquid     | 1.0.0-rc3               | 1.0.0-rc2  |                      |


### 3.0.0-rc2
(2022-02-23)

**更改**

- 优化代码仓库管理复杂度，将多个子仓库集中到FISCO BCOS统一管理
- 交易由`Base64`编码修改为十六进制编码
- 升级`bcos-boostssl`和`bcos-utilities`依赖到最新版本
- 修改`bytesN`类型数据的Scale编解码
- 优化交易处理流程，避免交易多次重复验签导致的性能损耗
- 优化事件推送模块的块高获取方法


**修复**

- 修复scheduler调度交易过程中导致的内存泄露
- 修复DMC+DAG调度过程中执行不一致的问题
- 修复[Issue 2132](https://github.com/FISCO-BCOS/FISCO-BCOS/issues/2132)
- 修复[Issue 2124](https://github.com/FISCO-BCOS/FISCO-BCOS/issues/2124)
- 修复部分场景新节点入网没有触发快速视图切换，导致节点数满足`(2*f+1)`却共识异常的问题
- 修复部分变量访问线程不安全导致节点崩溃的问题
- 修复AMOP订阅多个topics失败的问题

**兼容性**

3.0.0-rc2版本与3.0.0-rc1版本数据和协议不兼容，Solidity/WBC-Liquid合约源码兼容。如果要从3.0.0-rc1版本升级到3.0.0-rc2版本，需要做数据迁移。

|            | 推荐版本                | 最低版本  | 说明                   |
| ---------- | ----------------------- | --------- | ---------------------- |
| 控制台     | 3.0.0-rc2                  | 3.0.0-rc2     |                        |
| Java SDK        | 3.0.0-rc2           | 3.0.0-rc2     |     |
| CPP SDK        | 3.0.0-rc2           | 3.0.0-rc2     |     |
| WeBASE     | 暂时不支持(预计lab-rc2版本支持)         | 暂时不支持(预计lab-rc2版本支持) | |
| Solidity   | 最高支持 solidity 0.6.10 | 0.6.10    |                        |
| Liquid     | 1.0.0-rc2               | 1.0.0-rc2  |                      |

### 3.0.0-rc1
(2021-12-09)

**新增**

## 微服务架构
- 提供通用的区块链接入规范。
- 提供管理平台，用户可以一键部署、扩容、获得接口粒度的监控信息。

## 确定性多合约并行
- 易用：区块链底层自动并行，无需使用者预先提供冲突字段。
- 高效：区块内的交易不重复执行，没有预执行或预分析的流程。
- 通用：无论 EVM、WASM、Precompiled 或其它合约，都能使用此方案。

## 区块链文件系统
- 引入文件系统概念来组织链上资源，用户可以像浏览文件一样浏览链上资源。
- 基于区块链文件系统实现管理功能，如分区、权限等，更直观。

## 流水线PBFT共识
- 交易排序与交易执行相互独立，实现流水线架构，提升资源利用率。
- 支持批量共识，对区间内区块并行共识处理，提升性能。
- 支持单个共识Leader连续出块，提升性能。

## WBC-Liquid
- 集成WASM运行环境，支持Liquid智能合约。
- Liquid智能合约支持智能分析冲突字段，自动开启DAG。

**修复**

**兼容性**

3.0版本与2.0版本数据和协议不兼容，Solidity合约源码兼容。如果要从2.0版本升级到3.0版本，需要做数据迁移。

|            | 推荐版本                | 最低版本  | 说明                   |
| ---------- | ----------------------- | --------- | ---------------------- |
| 控制台     | 3.0.0-rc1                  | 3.0.0-rc1     |                        |
| Java SDK        | 3.0.0-rc1           | 3.0.0-rc1     |     |
| CPP SDK        | 3.0.0-rc1           | 3.0.0-rc1     |     |
| WeBASE     | lab-rc1                   | lab-rc1 |                        |
| Solidity   | 最高支持 solidity 0.6.10 | 0.6.10    |                        |
| Liquid     | 1.0.0-rc2               | 1.0.0-rc2  |                      |
