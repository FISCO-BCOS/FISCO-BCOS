# 特性列表：
- P2P连接使用SSL
- 支持CA证书链验证
- 使用UDP协议通讯节点ID
- 更新节点握手协议
- 支持节点互换连接列表
- 支持实时重连
- 支持RPC addpeer增加新连接节点
- 移除network.rlp依赖
- 移除CAVerify选项
- 移除config.json中的rpcssl配置项
- 移除config.json中的node相关配置
- 移除config.json中的ssl配置项
- 修改channelserver的验证方式
- 增加bootstrapnodes.json自动连接与自动更新
- 升级NodeAction系统合约存储记账列表
- 升级CAAction系统合约存储注销证书列表
- 支持CAAction系统合约变更触发断连
- 增加交易队列大小为10240
- 支持连接节点域名


# 测试用例关注点：
1. CA相关脚本证书生成及正确性验证
2. 验证新配置文件启动网络
3. 验证网络增加节点、移除节点及连接稳定性、出块稳定性
4. 验证节点启动后bootstrapnodes自动连接与自动更新
5. 验证NodeAction系统合约部署、增加、移除记账节点
6. 验证CAAction系统合约部署、增加、移除注销证书列表
7. 验证CAAction系统合约增加注销证书触发断连
8. 验证RPC addpeer接口
9. 验证channelserver功能正常
10. 验证100节点网络连接稳定性、出块稳定性
11. 验证各个主功能的兼容情况
12. 上帝模式
13. 验证SDK兼容情况
14. 验证新旧版本节点共存



# 建链说明
**注意：openssl 版本建议：1.0.2k，必须支持椭圆曲线 secp256k1*

下文介绍中，假设源码目录为fisco-bcos，假设该链有agencyA,agencyB两家机构参与，agencyA拥有节点A，agencyB拥有节点B，均为记账节点。

## 准备工作
1. 生成链证书

```
fisco-bcos/cert/chain.sh
```
fisco-bcos/cert/ 目录下将生成链证书相关文件。

2. 生成机构证书
```
fisco-bcos/cert/agency.sh agencyA
fisco-bcos/cert/agency.sh agencyB
```
fisco-bcos/cert/agencyA 目录下将生成agencyA机构证书相关文件。
fisco-bcos/cert/agencyB 目录下将生成agencyB机构证书相关文件。

## 开启创世节点
假设agencyA机构的A为创世节点。
假设A节点目录为nodeA。

1. 生成节点证书

```
fisco-bcos/cert/node.sh  agencyA A
```

fisco-bcos/cert/agencyA/A 目录下将生成A节点证书相关文件。

2. 建立节点目录
```
cd nodeA
mkdir data log 
cp fisco-bcos/genesis.json  fisco-bcos/config.json fisco-bcos/log.conf fisco-bcos/start.sh fisco-bcos/stop.sh    .
cp fisco-bcos/bootstrapnodes.json   ./data/
chmod +x *.sh
cp fisco-bcos/cert/agencyA/nodeA/* ./data/   #拷贝节点证书相关文件
```
修改配置信息：

a. 修改genesis.json中的initMinerNodesw为节点证书公钥 nodeA/data/node.nodeid文件内容,另外god字段设置为链管理员地址。

b. 根据实际情况修改nodeA/config.json中的listenip、rpcport、p2pport、channelPort等字段。

3. 启用系统合约


```
vim fisco-bcos/web3lib/config.js #更新其中RPC的IP和Port的配置
./start.sh  #初始启动节点
cd fisco-bcos/systemcontract/
babel-node deploy.js #部署系统合约
babel-node tool.js NodeAction register nodeA/data/node.json
```
得到部署系统合约之后的系统代理合约地址

4. 创世开启

将上面得到的系统合约地址更新到nodeA/config.json中的systemproxyaddressz字段，并重启节点
```
cd nodeA
./stop.sh
./start.sh
```

##  加入新节点
假设新节点目录为nodeB。

1. 生成节点证书

```
fisco-bcos/cert/node.sh  agencyB B
```

fisco-bcos/cert/agencyB/nodeB 目录下将生成nodeB节点证书相关文件。

2. 建立节点目录
```
cd nodeB
mkdir data log 
cp nodeA/genesis.json nodeA/config.json nodeA/log.conf nodeA/start.sh nodeA/stop.sh    .
cp nodeA/data/bootstrapnodes.json   ./data/
chmod +x *.sh
cp fisco-bcos/cert/agencyB/nodeB/* ./data/   #拷贝节点证书相关文件
```
修改配置信息：

a. 根据实际情况修改nodeB/config.json中的listenip、rpcport、p2pport、channelPort等字段。

b. 根据实际情况修改nodeB/data/bootstrapnodes.json 中host，port(任何已存在的节点，如创世节点)

3. 启动新节点
```
cd nodeB
./start.sh
```
4. 加入记账

```
cd fisco-bcos/systemcontract/
babel-node tool.js NodeAction register nodeB/data/node.json
```

# 兼容性说明

|连接通讯协议|V0（以太坊原生握手）| V1（旧SSL） | V2（新SSL） 
- | :- | :-: | -: 
旧版本| 支持 |支持|不支持
新版本 | 支持 | 支持| 支持



|| 新版本兼容情况  
- | :- | :-: | -: 
业务合约| 兼容
链数据| 兼容
SDK| 兼容
业务兼容| 兼容
配置文件| 兼容
系统合约| 兼容

如果已使用旧版本搭建好网络，现在fisco-bcos升级新版本之后，想使用新版本的V2连接协议功能，需要**升级节点身份配置、升级系统合约、升级网络**。


**注意**

**1. 网络中不能出现使用不同连接通讯协议版本的节点**

**2. 新版本的配置中默认启用V2版本连接通讯协议功能**

**3. 如果旧配置的网络需要使用新版本，需要对网络进行升级，步骤请参看下文的”网络升级“**


# 网络升级
升级过程，请保持网络正常运行状态，不停止节点。
## 升级节点身份配置
1. 参考前文中的步骤，生成链证书、机构证书、及为每个节点生成节点证书相关文件，并将其拷贝到每个节点的data目录下。
2. 为每个节点新建 bootstrapnode.json文件，相应字段填上其中一个节点的IP和p2p端口,示例如下：
```
{"nodes":[{"host":"127.0.0.1","p2pport":"30303"}]}
```

## 升级系统合约
将systemcontract/upgradeV3目录下的文件拷贝到旧网络下的systemcontractv2目录，并进入systemcontractv2目录，执行
```
babel-node upgradeV3.js
```
**注意:升级系统合约之后，systemcontractv2目录下的tool.js工具脚本已失效，请换用toolV3.js**

接着，对每个记账节点都执行以下操作，将其身份注册到系统合约中。
```
babel-node toolV3.js NodeAction register 节点目录/data/node.json
```

## 升级网络
修改每个节点的config.json中的**ssl配置项为2**

将所有节点，全部重启。**注意：为避免出现未预期结果，节点重启期间必须停止发送交易，待全部重启之后升级成本再发送**

至此，网络升级成功。


# 已完成冒烟测试用例
|| 场景 | 结果 
- | :-| :- 
旧配置| 单节点-非系统合约-出块| 通过
旧配置| 四节点-非系统合约-出块| 通过
旧配置| 四节点-系统合约-出块| 通过
旧配置| 四节点-系统合约-动态增加节点、删除节点| 通过
旧配置| 四节点-系统合约-SSL功能|通过
旧配置| 四节点-系统合约-CA启用、禁用功能| 通过
旧配置| 四节点-节点断连重连功能| 通过
旧配置| 四节点-全局配置功能| 通过
旧配置| 四节点-PBFT共识| 通过
旧配置| 四节点-RAFT共识| 通过
旧配置| 四节点-Channel功能| 通过
旧配置| 四节点-本地落盘加密功能| 
新配置| CA相关工具脚本| 通过
新配置| 单节点-非系统合约-出块| 通过
新配置| 四节点-非系统合约-出块| 通过
新配置| 四节点-系统合约-出块| 通过
新配置| 四节点-系统合约-动态增加节点、删除节点| 通过
新配置| 四节点-CAAction系统合约部署、增加、移除注销证书功能| 通过
新配置| 四节点-节点断连重连功能| 通过
新配置| 四节点-节点域名功能| 通过
新配置| 四节点-RPC addpeer接口| 通过
新配置| 四节点-Channel功能| 通过
新配置| 四节点-本地落盘加密功能|
新配置| 四节点-PBFT共识| 通过
新配置| 四节点-RAFT共识| 通过
旧配置| 四节点-系统合约升级| 通过
旧配置| 四节点-网络升级| 通过
旧配置| 四节点-混用新版本旧版本同时使用V0协议|通过
新配置| 四节点-SDK兼容|通过

# SDK升级说明
节点升级之后，与该节点相连的SDK客户端，均需要升级证书。

假设agencyA机构下的SDK客户端需要升级，则需要执行：
```
    ./sdk.sh agencyA sdk
```

并将所生成的sdk目录下所有文件拷贝到SDK端的证书目录下。



# 影响组件
以下组件需要同步更新升级
- 一键建链工具
- Docker相关
- 相关说明文档
- 相关Wiki文章


# 常用工具命令
```
查看已连接节点
curl -s -X POST --data '{"jsonrpc":"2.0","method":"admin_peers","params":[],"id":74}' 127.0.0.1:3001 
```
```
手动添加连接节点
curl -s -X POST --data '{"jsonrpc":"2.0","method":"admin_addPeer","params":["127.0.0.1:4001"],"id":74}' 127.0.0.1:3003
```