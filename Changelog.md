### V1.3.1

* Update
1. 节点建立连接时过滤自身的ip端口
2. 修复使用web3sdk会偶尔断开无法重连的bug
3. 修复节点间连接会偶尔断开无法重连的bug
4. nodejs支持发送国密算法交易

* Add

1. nodejs客户端通过开关支持国密算法发送交易

* Fix

1. 缓存bugfix
2. 共识bugfix

### V1.3.0

* New Feature
1. CA证书链SSL连接协议
2. 支持UTXO模型的转账功能
3. 支持国密功能

**注意: 如果老版本fisco-bcos需要使用1.3版本的新连接协议特性，需要进行相应的升级操作，具体请参考[升级操作说明](systemcontract/README.md)**

* Update 
1. 更新节点握手协议
2. 升级NodeAction系统合约存储记账列表
3. 升级CAAction系统合约存储注销证书列表
4. 简化config.json配置文件
5. 更新一键安装脚本及相关说明
6. 更新docker镜像及相关说明
7. 更新FISCO BCOS 用户手册说明
8. 物料包工具更新:（1）适配新的连接管理 （2） god帐号地址不再固定, 改为在构建工具包时进行创建。

* Add:
1. 增加P2P新版本SSL连接协议功能。
2. 增加节点本地指定连接列表bootstrapnodes.json功能。
3. 增加节点连接支持域名配置功能。
4. 支持RPC addpeer增加新连接节点功能。
5. web3sdk增加系统合约部署和调用工具。
6. web3sdk增加命令行调用取块高，视图等rpc接口。
7. 增加web3sdk使用SM3交易HASH运算功能
8. 增加web3sdk使用SM2发送国密交易功能。
9. 增加国密SM4数据落盘加密功能。
10. 增加区块链共识使用SM2签名验签功能。
11. 增加节点通信使用国密SSL功能。
12. 增加生成国密证书及验证国密证书功能。
13. 增加fiscl-solc使用国密SM3 Hash算法编译合约。
14. 国密用户手册说明。

* Fix:
1. 交易队列大小为10240

* Delete
  1、不再维护DFS客户端代码:移除tool目录中的DfsSample，DfsSDK，third-jars目录。节点中DFS功能相关代码暂时保留，但将不再维护，请社区慎重使用。如果有基于DFS做了一些功能的，可以继续使用V1.2.0的DFS。如果疑问请联系我们，讨论后续的维护方法。

### V1.2.0

(2018-05-14)  

- Update

1. 规范日志打印行为，统一以英文方式输出。

- Add

1. 增加[可监管的零知识证明验证功能](doc/可监管的零知识证明说明.md)。
2. 提供[弹性联盟链共识框架的主体功能](doc/弹性联盟链共识框架说明文档.md)，在系统合约部分提供的和这个功能相关的合约，以及ConsensusControl.sol 这个规则示例。
3. 添加了[群签名和环签名验证功能](doc/启用_关闭群签名环签名ethcall.md)。

- Fix

1. 区块链浏览器miner字段兼容问题。

(2018-03-29)

- Fix

1. 修正一键安装脚本编译完成之后, 启动nodejs模块缺失的问题

(2018-03-28)  

- Update

1. 为查询block的RPC接口增加更多的返回字段。  
2. FISCO BCOS用户手册更新, a. 在web3lib中需要增加cnpm install的操作  b. config.js文件放入weblib3目录中。  

- Add:

1. 添加打印监控日志的功能，适配于区块链浏览器的report agent。  
2. scripts/install_deps.sh 依赖脚本添加Linux Oracle Server的支持。  

### V1.1.0

(2018-03-27)  
* Update  
1. js文件整理: 删除accounttool目录。
2. 将tool、systemcontractv2目录中的公共js代码放入web3lib中。 

* Add:
1. CNS添加对java客户端的支持。
2. 添加支持CNS方式调用的rpc接口, CNS方式调用添加的新的接口如下：
  CNS方式调用接口                 原接口
  eth_getCodeCNS             =>    eth_getCode
  eth_getBalanceCNS           =>    eth_getBalance
  eth_getStorageAtCNS         =>    eth_getStorageAt
  eth_getTransactionCountCNS   =>    eth_getTransactionCount

* Fix:
1. CNS调用合约支持合约重载的接口。  