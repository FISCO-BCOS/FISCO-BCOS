# 频率控制与接口统计模块
<!-- TOC -->

- [频率控制与接口统计模块](#频率控制与接口统计模块)
    - [1. 功能介绍](#1-功能介绍)
    - [2. 配置文件介绍](#2-配置文件介绍)
        - [`config.json`配置项](#configjson配置项)
        - [频率控制配置文件](#频率控制配置文件)
    - [3. 模块代码说明](#3-模块代码说明)
        - [模块组织说明](#模块组织说明)
        - [涉及文件](#涉及文件)
    - [4. 测试用例](#4-测试用例)
        - [4.1 测试RPC接口统计](#41-测试rpc接口统计)
        - [4.2 测试P2P接口统计](#42-测试p2p接口统计)
        - [4.3 测试共享内存](#43-测试共享内存)
        - [4.4 测试频率控制](#44-测试频率控制)

<!-- /TOC -->

## 1. 功能介绍

- 支持`RPC`总调用次数控制
- 支持配置`RPC`单个接口频率控制
- 默认令牌桶初始化含有桶大小的3/4令牌
- 统计`RPC`调用次数与耗时
- 统计`P2P`包数量

## 2. 配置文件介绍

### `config.json`配置项

- 在`config.json`添加`limitconf`配置文件路径
- 在`config.json`添加`statsInterval`添加统计时长设置(以秒计)，按统计时长输出结果到`info`日志文件，无接口调用则不输出，无此配置项则不启动统计功能

```json
{
    "limitconf":"/home/ubuntu/nodedata/singleNode/limit.conf",
    "statsInterval":"5"    
}
```

### 频率控制配置文件

- 配置文件示例

```json
{
    "globalLimit": {
        "enable": true,
        "defaultLimit": 100
    },
    "interfaceLimit": {
        "enable": true,
        "defaultLimit": 0,
        "custom": {
            "eth_getBlockByNumber": 50,
            "eth_sendRawTransaction": 50
        }
    }
}
```

- `globalLimit`说明

总调用次数控制相关配置，其中`enable`选项控制是否启用总调用控制，取值为`bool`类型，`defaultLimit`设置每秒总计允许多少次RPC调用，取值为`uint`

- `interfaceLimit`说明

每个IP每秒访问接口次数控制，其中`enable`选项控制是否启用此功能，`defaultLimit`设置默认接口调用频率，值类型为`uint`，当取值`0`时，检查`custom`是否有调用接口配置，如果没有则默认不控制接口调用频率，否则按照`custom`对接口频率控制。

## 3. 模块代码说明

### 模块组织说明

|源文件|备注|
|:----|:-------|
|libstatistics/CMakeLists.txt|cmake文件|
|libstatistics/boost_shm.h|共享内存相关头文件|
|libstatistics/InterfaceStatistics.cpp|接口统计实现|
|libstatistics/InterfaceStatistics.h|接口统计头文件|
|libstatistics/RateLimiter.cpp|频率控制源码|
|libstatistics/RateLimiter.h|频率控制头文件|
|libstatistics/RateLimitHttpServer.cpp|频率控制调用|
|libstatistics/RateLimitHttpServer.h|修改了`HttpServer`的`callback`函数|
|libstatistics/limit.conf|频率控制配置文件示例|
|libstatistics/README.md|模块说明及测试用例|

### 涉及文件

|源文件|备注|
|:----|:-------|
|eth/main.cpp|`new InterfaceStatistics()`创建对象|
|libweb3jsonrpc/ModularServer.h|`ModularServer`添加statistics指针|
|libweb3jsonrpc/ModularServer.h|`HandleMethodCall`添加统计代码|
|libweb3jsonrpc/SafeHttpServer.h|`#include "libStatisticsLimiter/RateLimitHttpServer.h"`|
|libethcore/ChainOperationParams.h|添加配置参数`"limitconf","statsInterval"`|
|libethereum/ChainParams.cpp|配置参数处理|
|libp2p/Session.h|添加`statistics`指针|
|libp2p/Session.cpp|`readPacket`添加统计代码|
|libp2p/Host.cpp|293行`setStatistics`函数|

## 4. 测试用例

### 4.1 测试RPC接口统计

1. 前置条件

- 在`config.json`文件中配置统计时长，例如每隔5秒输出统计结果`"statsInterval":"5"`，如果不配置则不启动接口统计(`info`日志输出`RPC 's statistics is off`)

2. 用例步骤

- 在`perfoemance`文件夹下，部署合约`babel-node deploy.js Ok`

- 使用`perfoemance/performance.js`发送交易，参数为`babel-node performance.js Ok 1000 100`

- 使用`tail -f info* | grep "Statistics RPC"`查看日志文件

3. 预期结果

```bash
INFO|2017-08-10 15:24:07|15:23:57|Statistics RPC|Name:eth_sendRawTransaction   |Count:  720|Avg: 1.07 ms|Max: 43 ms
INFO|2017-08-10 15:24:07|15:23:57|Statistics RPC|Name:eth_blockNumber          |Count:    1|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-10 15:24:17|15:24:07|Statistics RPC|Name:eth_call                 |Count:    1|Avg: 3.00 ms|Max:  3 ms
INFO|2017-08-10 15:24:17|15:24:07|Statistics RPC|Name:eth_getBlockByNumber     |Count:   14|Avg: 0.29 ms|Max:  1 ms
INFO|2017-08-10 15:24:17|15:24:07|Statistics RPC|Name:eth_getTransactionReceipt|Count:    2|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-10 15:24:17|15:24:07|Statistics RPC|Name:eth_sendRawTransaction   |Count:  280|Avg: 0.41 ms|Max:  6 ms
```
### 4.2 测试P2P接口统计

1. 前置条件

- 开启两个以上的节点测试
- 在`config.json`文件中配置统计时长，例如每隔5秒输出统计结果`"statsInterval":"5"`，如果不配置则每10秒输出统计结果

2. 用例步骤

- 使用`tail -f info* | grep "Statistics P2P"`查看日志文件

3. 预期结果

```bash
INFO|2017-08-18 21:42:20|21:42:10|Statistics P2P0aae4|Name:0aae4...ce885-0-39       |Count:  20|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-18 21:42:30|21:42:20|Statistics P2P5b466|Name:5b466...b81fc-0-39       |Count:  17|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-18 21:42:30|21:42:20|Statistics P2P0aae4|Name:0aae4...ce885-0-39       |Count:  20|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-18 21:42:30|21:42:20|Statistics P2P5b466|Name:5b466...b81fc-0-36       |Count:   7|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-18 21:42:30|21:42:20|Statistics P2P0aae4|Name:0aae4...ce885-0-36       |Count:   7|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-18 21:42:40|21:42:30|Statistics P2P5b466|Name:5b466...b81fc-0-2        |Count:   1|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-18 21:42:40|21:42:30|Statistics P2P0aae4|Name:0aae4...ce885-0-2        |Count:   1|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-18 21:42:40|21:42:30|Statistics P2P5b466|Name:5b466...b81fc-0-39       |Count:  18|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-18 21:42:40|21:42:30|Statistics P2P0aae4|Name:0aae4...ce885-0-39       |Count:  20|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-18 21:42:40|21:42:30|Statistics P2P5b466|Name:5b466...b81fc-0-36       |Count:   7|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-18 21:42:40|21:42:30|Statistics P2P0aae4|Name:0aae4...ce885-0-36       |Count:   7|Avg: 0.00 ms|Max:  0 ms
```

### 4.3 测试共享内存

1. 前置条件

- 启动节点

2. 用例步骤

- 查看`eth`启动的文件夹目录下是否有内存映射文件
- 设置较长的统计时长，打断`eth`进程后重启，查看日志的统计输出

3. 预期结果

```bash
RPC_MappedFile
RPCSSL_MappedFile
IPC_MappedFile
P2P_MappedFile
```

### 4.4 测试频率控制

1. 前置条件

- 在`config.json`文件中配置频率控制文件路径，例如`"limitconf":"/home/ubuntu/nodedata/singleNode/limit.conf"`
- 在`config.json`文件中配置统计时长，例如每隔10秒输出统计结果`"statsInterval":"10"`
- `limit.conf`中配置如下

```json
{
    "globalLimit": {
        "enable": true,
        "defaultLimit": 500
    },
    "interfaceLimit": {
        "enable": true,
        "defaultLimit": 0,
        "custom": {
            "eth_getBlockByNumber": 100,
            "eth_sendRawTransaction": 10
        }
    }
}
```

2. 用例步骤

- 在`perfoemance`文件夹下，部署合约`babel-node deploy.js Ok`

- 使用`perfoemance/performance.js`发送交易，参数为`babel-node performance.js Ok 10000 100`

- 使用`tail -f info* | grep "Statistics RPC"`查看日志文件

3. 预期结果

可以看到，每10秒统计`RPC`接口调用次数，平均每秒控制在100，因为桶初始有`0.75*capacity`令牌，所以会有一定误差

```bash
INFO|2017-08-09 21:21:58|21:21:48|Statistics RPC|Name:eth_sendRawTransaction   |Count:   60|Avg: 0.15 ms|Max:  2 ms
INFO|2017-08-09 21:21:58|21:21:48|Statistics RPC|Name:eth_blockNumber          |Count:    1|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-09 21:22:08|21:21:58|Statistics RPC|Name:eth_sendRawTransaction   |Count:   99|Avg: 0.01 ms|Max:  1 ms
INFO|2017-08-09 21:22:18|21:22:08|Statistics RPC|Name:eth_sendRawTransaction   |Count:   99|Avg: 0.01 ms|Max:  1 ms
INFO|2017-08-09 21:22:28|21:22:18|Statistics RPC|Name:eth_sendRawTransaction   |Count:  100|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-09 21:22:38|21:22:28|Statistics RPC|Name:eth_sendRawTransaction   |Count:   99|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-09 21:22:48|21:22:38|Statistics RPC|Name:eth_sendRawTransaction   |Count:   99|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-09 21:22:58|21:22:48|Statistics RPC|Name:eth_sendRawTransaction   |Count:  100|Avg: 0.03 ms|Max:  2 ms
INFO|2017-08-09 21:23:08|21:22:58|Statistics RPC|Name:eth_sendRawTransaction   |Count:  100|Avg: 0.01 ms|Max:  1 ms
INFO|2017-08-09 21:23:18|21:23:08|Statistics RPC|Name:eth_sendRawTransaction   |Count:   99|Avg: 0.01 ms|Max:  1 ms
INFO|2017-08-09 21:23:28|21:23:18|Statistics RPC|Name:eth_sendRawTransaction   |Count:   99|Avg: 0.01 ms|Max:  1 ms
INFO|2017-08-09 21:23:38|21:23:28|Statistics RPC|Name:eth_sendRawTransaction   |Count:  100|Avg: 0.01 ms|Max:  1 ms
INFO|2017-08-09 21:23:48|21:23:38|Statistics RPC|Name:eth_getBlockByNumber     |Count:   23|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-09 21:23:48|21:23:38|Statistics RPC|Name:eth_getTransactionReceipt|Count:    4|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-09 21:23:48|21:23:38|Statistics RPC|Name:eth_sendRawTransaction   |Count:   47|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-09 21:23:58|21:23:48|Statistics RPC|Name:eth_getBlockByNumber     |Count:   53|Avg: 0.00 ms|Max:  0 ms
INFO|2017-08-09 21:24:08|21:23:58|Statistics RPC|Name:eth_call                 |Count:    1|Avg: 3.00 ms|Max:  3 ms
INFO|2017-08-09 21:24:08|21:23:58|Statistics RPC|Name:eth_getBlockByNumber     |Count:   37|Avg: 0.00 ms|Max:  0 ms
```

