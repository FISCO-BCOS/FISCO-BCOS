
### v3.16.2

(2025-10)

**修复**

* 修复 CallRequest 解码错误（改进 `decodeCallRequest` 的健壮性，不抛异常地忽略非法十六进制输入）

**修改/工程**

* 版本号/默认兼容版本提升至 3.16.2；同步配置模板、协议版本常量等

**升级描述**

* 升级节点可执行程序（3.14.0+ 建议直接替换二进制）；如使用配置模板，更新兼容版本为 3.16.2

---

### v3.16.1

(2025-09-29)

**新增/修改**

* 回执新增字段；更新 `sealTxs` 接口
* `blockHeader()` const 返回类型调整为 `AnyBlockHeader`；`blockHeaderConst` 重命名为 `blockHeader`
* 废弃“按索引取区块”相关接口；移除区块对象上已废弃的 `receipt()`、`metaData()`、`transactions()` 等访问器
* 更新 `archive-reader`

**性能/稳定性**

* 优化正则性能，引入 `throwWithTrace`
* P2P 写入路径改为将异步任务投递到 TBB 任务组；拆分 `fastSendMessage`
* 拆分/补充多处单元测试；修复缺失交易检查

**升级描述**

* 直接替换可执行程序可获得上述修复/改进；如依赖废弃接口，请按新接口进行代码迁移

---

### v3.16.0

(2025-09-03)

**Web3/RPC**

* 支持 WebSocket 协议与 CORS；增强区块响应（含 receipts、logs bloom 计算）
* 兼容旧交易编码（无 chainId）；`estimateGas()` 增强；新增同步交易行为配置

**交易池（txpool）**

* 重构为 “sealed/unseal” 队列；移除未使用接口与冗余逻辑，统一/精简命名
* 移除旧 `asyncSendBroadcastMessage`；合并 `submitTransactionWithHook` 至 `submitTransaction`
* 修复 pending nonce、过滤等问题；补充交易验证器

**执行/调度与性能**

* 引入 `AnyHolder` 优化 `BlockImpl`；修复 `delegatecall` value；改进 `writeOne` 实现；多处异常捕获与容错

**工程/CI**

* 更新 vcpkg 基线与 CI；HTTP Server、tars-protocol client 实现迁移；广播序列修复；回调 Sealer 等待时间策略

**升级描述**

* 二进制可直接替换；如对 txpool/RPC 的废弃接口有依赖，需要按新版接口适配

---

### v3.15.5

(2025-08-19)

**修复**

* 修复 KMS 与交易池协同下的异常
* RPC 增加获取区块头时间戳的调用

---

### v3.15.4

(2025-07-14)

**概念与接口**

* 将 `tag_invoke`、`EVMAccount` 重构为成员自定义点；补充 Storage/Account 概念；新增 `enforceSubmitTransaction` 标志

**交易池/执行**

* 修复 web3 pending nonce、max nonce 等问题；KMS 在 CentOS + AWS SDK 下的内存分配问题

**工程**

* 增强 Web3 CI；为 GitHub macOS 添加 vcpkg 缓存
* 调整 Sealer 执行等待时间（1ms → 1s），并修复 pending 相关问题

---

### v3.15.3

(2025-05-20)

**修改**

* 引入 RocksDB cache；`merge` 方法支持多参数

**修复**

* 修复 web3 待处理交易计数

---

### v3.15.2

(2025-04-24)

**存储与一致性**

* `memstorage` 的 `readOne` 支持升级为 writer；改进 `readSome/exists`；将 `blockhash/number` 写入存储以支持 `getBlockHash()`

**调度/共识**

* `scheduler v1` 支持空块；`viewChange` 时重置 `lowWaterMark`；每块获取 `LedgerConfig`；移除 `marktx` 锁

**测试/其他**

* 新增 `single_point_consensus` 与 `force_sender` 测试开关

**修复**

* 修复预编译 EVM 状态、方法权限校验 sender、`m_result` 计算等

---

### v3.15.1

(2025-04-15)

**执行与回滚**

* `executeTransaction` 新增 `call` 标志；消费余额移动到执行阶段；不返回空块
* 调整更新 nonce 顺序与保存点；`LedgerNonceChecker::batchInsert` 解锁；`BucketMap::clear()` 去除回调
* 支持在多层存储查询不存在项

**稳定性**

* 规避潜在内存错误；为通知任务增加隔离；修复扩容模板（`build_chain`）问题

---

### v3.15.0 

(2025-03-26)

**执行器版本治理**

* 新增 `executor_version` 配置（含创世）；修复版本同步；重命名 `transaction-executor/scheduler` 为 v1；v1 引入新的 nonce 逻辑；初始化 `evmc_tx_context`

**Web3/RPC 与其他**

* 修复 `to` 为空时交易编码；静态调用场景下更新 nonce；`StateKey` 与 `StateKeyView` 比较函数一致性修复；余额问题修复

**工程**

* 移除 `checkRequirements`；简化 baseline

---

### v3.14.0

(2025-02-28)

**Web3/RPC/WS 与 SDK**

* 修复 WS 消息解码；限制 SM 模式下 TLS 低版本；优化日志关键字；修复多项 SDK 崩溃

**执行/调度**

* 修复回滚场景 nonce 不增长、合约初始化 nonce；将更新 EOA nonce 的逻辑移动至 scheduler；修复 `notifySealer` 引发的死锁

**P2P/网络**

* 修复向已关闭 socket 写入导致的崩溃；定时器异常修复；当 `p2p.enable_ssl=false` 时不校验证书；修复链 ID 一致性问题

**存储与权限**

* `memory storage` 的 `range()` 加锁；并发容器替换为互斥；新增 `transfer` 权限项

---

### v3.13.0

(2025-02-25)

**性能/重构**

* 内存/结构优化：`MemoryStorage` 支持异构查找与 `Equal`，多处容器/结构（如使用 `array` 替代 `unordered_map`、`std::string_view` 用作 nonce）与隐藏友元（hidden friend）风格重构
* 任务/执行器：引入 `pmr` 分配器以降低分配开销；执行器拆分为三段协程以提升并发；为基线执行器实现基础余额逻辑与 `BalancePrecompiled`
* 网络/网关：减少 handle 拷贝与无用 `shared_ptr`；新增协程化的 `broadcastMessage` 接口并在 `PBFT`/网络中落地；去除阻塞的 `notifyUnSealTxs`

**功能/接口**

* 交易新增 `conflictKeys` 字段；新增交易签名检查开关；区块对象接口完善；定时器支持借用 `io_service`

**修复/工程**

* 多处基准/单测与工具链改进（sanitize CI、BucketMap 遍历、前置消息类型/路由图更新等）；执行器/事务若干缺陷修复；性能基准用例完善

---

### v3.12.4

(2025-01-21)

**修复/工程**

* 修复 build_chain.py 的 CDN 问题
* 修复 RPC 过滤器在 ranges 下的边界问题
* 版本提升与发布流程脚本更新

---

### v3.12.3

(2024-12-27)

**修复**

* 执行器新地址生成问题
* P2P 端对交易签名长度检查与 sender 字段处理
* 修复 Web3 RPC 交易返回结构

---

### v3.12.2

(2024-12-20)

**修复/工程**

* CI：补充 Ubuntu 上传并尝试修复 Windows cppsdk 构建
* RPC：区块响应编码修复；执行器 nonce 兼容性修复
* 同步与版本提升

---

### v3.12.1

(2024-11-25)

**网关/安全**

* 新增网关 SSL server/client 验证模式配置；当使用 `verify_none` 时生成随机 p2p 节点 ID，并修复握手异常

**修复/工程**

* 修复 p2p 消息长度校验导致的 coredump；在 Web3 JSON-RPC 区块响应中增加更多信息
* 修复 Docker CI 构建失败与发布 actions

---

### v3.12.0

(2024-10-25)

**新增**

* Web3 兼容增强：配置项 `feature_evm_address`（Web3 合约地址计算逻辑）、对 EOA 的 nonce 递增检查
* 时间戳单位适配：配置项 `feature_evm_timestamp`（Solidity 交易内合约时间戳改为秒）
* rPBFT：新增“轮换选举权重”，可按权重选出共识节点
* 配置：新增 `rpc.enable_ssl`（覆盖旧的 `rpc.disable_ssl`）

**修复**

* rPBFT 在流水线共识场景的轮换失败：VRF 输入从 prev block hash 改为 block number（`bugfix_rpbft_vrf_blocknumber_input`）
* 修复游离节点可将交易广播到共识节点、观察节点切为 leader 后已落盘交易被拒的问题
* 修复部分 Web3 JSON-RPC 接口错误 HEX 输入导致的 coredump；修复落盘加密不可用

**兼容性**

* 3.4.x ~ 3.11.x 数据完全兼容；3.0.x ~ 3.3.x 可灰度替换二进制，如需新特性需升级 `compatibility_version`（参考文档）

---

### v3.11.0

(2024-08-27)

**新增**

* 自动清理过期 Nonce；新增 `[log].rotate_time_point`（日志滚动时间）
* 快照生成/导入；通过 p2p 同步归档区块的交易与收据
* 支持“区块与状态”分离存储

**工程/性能**

* 多模块统一/合并编译（unity build）、代码结构与接口清理，快照/归档工具链完善

**兼容性**

* 3.4.x ~ 3.10.x 数据完全兼容；更旧版本参考升级说明

---

### v3.10.3

(2024-09-20)

**修复**

* 修复 Web3 RPC `eth_estimateGas` 返回问题；版本小幅提升

---

### v3.10.2

(2024-09-13)

**修复/改进**

* 依赖版本修正；PBFT 提案重推缓存缺陷修复
* 预编译余额转账开关；回滚日志修正；构建/头文件与方法补充

---

### v3.10.0

(2024-08-06)

**新增**

* EVM 升级至 CANCUN：新增 TLOAD/TSTORE/MCOPY/BLOBHASH/BLOBBASEFEE 等操作码；支持 Solidity 0.8.26
* 新增开关 `feature_evm_cancun` 控制升级；搭链脚本支持开启 debug 日志

**修复/性能**

* baseline 模式下“合约不存在/回滚”行为与内存泄漏修复；限制缓存执行实例数量，降低内存
* 修复部分 Web3 JSON-RPC 接口缺失 Block Tag 导致的退出

**兼容性**

* 3.4.x ~ 3.9.x 数据完全兼容；更旧版本参考升级说明

---

### v3.9.0

(2024-07-03)

**新增**

* Web3 JSON-RPC 接口：支持 Hardhat、OpenZeppelin 等 Web3 工具联调
* 交易类型：支持 RLP 编码交易（EIP-2930/EIP-1559/EIP-4844）的解析与执行
* 事件与过滤器：新增事件拉取与过滤接口（newFilter/newBlockFilter/getFilterChanges/getFilterLogs 等）
* 账本查询：新增 `eth_getStorageAt`；区块响应增强（含 logs bloom）
* 配置项：新增 `web3_chain_id`（系统配置）与 `[web3_rpc]` 段（默认端口 8545）

**修复**

* 修复 Solidity 静态调用 staticcall 异常返回、receive 函数处理、EOA code 返回、view 场景取区块高/时间戳错误等问题
* 修复多项 Web3 JSON-RPC 兼容性问题（syncing/transaction 响应、estimateGas 等）
* 交易池：修复同一交易在池内导致的提案 nonce 校验失败；修复新交易同步、EIP1559 签名校验等

**兼容性/升级**

* 3.4.x ~ 3.8.x 数据完全兼容，直接替换二进制即可；3.0.x ~ 3.3.x 可灰度替换，如需新特性需升级 `compatibility_version` 至 3.9.0（参考文档）

---

### v3.8.0

(2024-05-15)

**新增**

* 流水线执行（实验特性）：提供并发流水线模式，显著提升吞吐
* Storage2：为 RocksDB/MemoryStorage 提供异步 `range()`/遍历能力
* 基线工具：支持 getCode/listABI 查询；新增预编译错误输出编码
* 执行器：为不存在地址的 STATIC_CALL 返回 success（与以太坊对齐）；添加特性开关检查

**修复/改进**

* 修复 EOA 账户 `getCode` 返回、DMC 部署 gas、EVM 异常 gas 统计、并行回滚缺失 entry、`create2`/`delegatecall` 语义、冻结账户等问题
* 为 KeyPage/StateStorage 写入未变更时计算 DBHash；多处日志、编译与 CI 稳定性修复

**升级描述**

* 替换全部节点二进制可获得修复；如需使用新特性，完成二进制替换后将 `compatibility_version` 升级为 3.8.0

---

### v3.7.3

(2024-06-21)

**修复/改进**

* 判断 EOA/合约 codeEntry 的兼容修复；修复 TARS 推送交易回调异常
* 修复 staticcall 无地址返回、DMC 部署 gasUsed、set_row 脏标等问题
* 交易池/同步：修复提案 nonce 校验、同步日志级别与 TLS 版本限制（国密模式）

---

### v3.7.2

(2024-05-21)

**修改**

* 回退日志级别变更以避免兼容性问题（仅影响日志，不影响业务）

---

### v3.7.1

(2024-04-12)

**新增/改进**

* 新增“只读模式”与 `allow_free_node` 配置；更新版本与 License
* 网络/运维：忽略未知 AMOP 报文；修复 Boost.Asio 定时器 bug；新增上传产物 CI

---

### v3.7.0

(2024-03-25)

**新增**

* 交易结构新增 `memo` 字段（业务标识，不影响 txHash）

**修复**

* 依赖与平台：更新 tikv_client/bcos-wasm/wedprcrypto/rust 工具链；更新 TASSL 修复 macOS 编译
* 功能修复：空 ABI 的 getABI、开启部署权限后资产转移受限、搭链脚本安装 python 失败等

**升级/兼容**

* 二进制直接替换；如需新特性，完成替换后升级链数据版本至 3.7.0；组件兼容矩阵参考官方文档

---

### v3.6.1

(2024-03-04)

**修复/改进**

* 修复 DagTransfer 兼容性；开启网络层限流；修复 keyPage 哈希不一致；`internalCreate` 复用部署逻辑

**升级/兼容**

* 建议直接替换二进制；如需新特性与行为变更，请按 3.6.0 的升级步骤处理 `compatibility_version`

---

### v3.6.0

(2024-02-08)

**新增**

* SDK：新增交易流程编解码接口
* 搭链：`build_chain.sh` 支持直接搭建 pro/max 区块链
* 日志：新增归档与压缩配置项

**修复**

* 与以太坊对齐 opcode 行为；修复 DMC 仅回滚部分状态；支持配置网络 hostname
* 修复交易池 MarkTxs 失败；修复 feature 升级时 bugfix 启用问题；修复轻节点创世校验导致无法同步

**升级/兼容**

* 二进制替换即可获得稳定性提升；如需新特性，完成替换后将 `compatibility_version` 升级为 3.6.0

---

### v3.5.0

(2023-11-20)

**新增**

* rPBFT 共识算法；交易/区块树状广播；按时间顺序打包交易
* 同态加密 Paillier 预编译；支持使用 TARS RPC 发送交易

**修改**

* 日志打印与关键路径优化

**修复**

* 共识 Proposal 验证/CheckPoint 计时与交易回退处理；DAG 执行自锁；EVM 缓存失效；网关损坏包；归档重导入字段处理等

**升级/特性开关**

* 稳定版可灰度替换；最新版需全量同时升级
* 新增多项实验特性开关（如 `feature_sharding`、`feature_paillier`、`feature_rpbft`、`bugfix_*`），开启后不可回退；建议确认所有节点版本一致后再开启

---

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
