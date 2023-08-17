# Task60：fisco-bcos节点配置之主配置文件config.ini

前提条件：在讲解开始之前，需要对fisco-bcos的相关内容有最基础的了解。

FISCO BCOS支持多账本，每条链包括多个独立账本，账本间数据相互隔离，群组间交易处理相互隔离，每个节点包括一个主配置config.ini和多个账本配置group.group_id.genesis、group.group_id.ini。下面主要讲述的是主配置文件config.ini。

config.ini是fisco-bcos的主配置文件，采用ini格式，主要包括 p2p、rpc、cert、chain、security、consensus、executor、storage、txpool和log 配置项。

## 配置RPC

·    channel_listen_ip: Channel监听IP，为方便节点和SDK跨机器部署，默认设置为0.0.0.0；

·    jsonrpc_listen_ip：RPC监听IP，安全考虑，默认设置为127.0.0.1，若有外网访问需求，请监听节点外网IP或0.0.0.0；

·    channel_listen_port: Channel端口，对应到[Java SDK](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/sdk/java_sdk.html#id2)配置中的channel_listen_port；

·    jsonrpc_listen_port: JSON-RPC端口。

特别的是：

出于安全性和易用性考虑，v2.3.0版本最新配置将listen_ip拆分成jsonrpc_listen_ip和channel_listen_ip，但仍保留对listen_ip的解析功能；

配置中仅包含listen_ip：RPC和Channel的监听IP均为配置的listen_ip；

配置中同时包含listen_ip、channel_listen_ip或jsonrpc_listen_ip：优先解析channel_listen_ip和jsonrpc_listen_ip，没有配置的配置项用listen_ip的值替代；v2.6.0 版本开始，RPC 支持 ipv4 和 ipv6。

示例如下：

\#IPv4

​                        ![image-20230817152426188](../AppData/Roaming/Typora/typora-user-images/image-20230817152426188.png)   

\#IPv6

 ![image-20230817152442413](../AppData/Roaming/Typora/typora-user-images/image-20230817152442413.png)

## 配置P2P

当前版本FISCO BCOS必须在config.ini配置中配置连接节点的IP和Port，P2P相关配置包括：

· listen_ip：P2P监听IP，默认设置为0.0.0.0；

· listen_port：节点P2P监听端；

· node.*: 节点需连接的所有节点IP：Port或DomainName:Port。该选项支持域名，但建议需要使用的用户手动编译源码；

· enable_compress：开启网络压缩的配置选项，配置为true，表明开启网络压缩功能，配置为false，表明关闭网络压缩功能；

· v2.6.0 版本开始，同样的，P2P 支持 ipv4 和 ipv6。

示例如下：

ipv4:

 ![image-20230817152451826](../AppData/Roaming/Typora/typora-user-images/image-20230817152451826.png)

ipv6:

 ![image-20230817152500045](../AppData/Roaming/Typora/typora-user-images/image-20230817152500045.png)

## 黑名单与白名单配置

```
·  ``certificate_blacklist``：黑名单配置
·  ``certificate_whitelist``：白名单配置
示例如下：
```

`  ```![image-20230817152511546](../AppData/Roaming/Typora/typora-user-images/image-20230817152511546.png)

## 配置群组内的配置

`·  ``group_data_path: ``群组数据存储路径。```

`·  ``group_config_path``: ``群组配置文件路径。```

`  ```![image-20230817152524691](../AppData/Roaming/Typora/typora-user-images/image-20230817152524691.png)

## 配置SSL连接的证书信息

`·    ``data_path``：证书和私钥文件所在目录。```

`·    ``key: ``节点私钥相对于``data_path``的路径。```

`·    ``cert: ``证书``node.crt``相对于``data_path``的路径。```

`·    ``ca_cert: ca``证书文件路径。```

`·    ``ca_path: ca``证书文件夹，多``ca``时需要。```

`·    ``check_cert_issuer``：设置``SDK``是否只能连本机构节点，默认为开启（``check_cert_issuer=true``）。```

`  ```![image-20230817152534674](../AppData/Roaming/Typora/typora-user-images/image-20230817152534674.png)

## 配置日志信息

`FISCO BCOS``支持功能强大的`[`boostlog`](https://www.boost.org/doc/libs/1_63_0/libs/log/doc/html/index.html)`，日志配置主要位于``config.ini``的``[log]``配置项中。```

`·    ``enable: ``启用``/``禁用日志，设置为``true``表示启用日志；设置为``false``表示禁用日志，默认设置为``true``，性能测试可将该选项设置为``false``，降低打印日志对测试结果的影响```

`·    ``log_path:``日志文件路径。```

`·    ``level: ``日志级别，当前主要包括``trace``、``debug``、``info``、``warning``、``error``五种日志级别，设置某种日志级别后，日志文件中会输大于等于该级别的日志，日志级别从大到小排序为：``error > warning > info > debug > trace``。```

`·    ``max_log_file_size``：每个日志文件最大容量，计量单位为``MB``，默认为``200MB``。```

`·    ``flush``：``boostlog``默认开启日志自动刷新，若需提升系统性能，建议将该值设置为``false``。```

`  ```![image-20230817152547615](../AppData/Roaming/Typora/typora-user-images/image-20230817152547615.png)

## 配置统计日志开关

`·    ``log.enable_statistic``配置成``true``，开启网络流量和``Gas``统计功能```

`·    ``log.enable_statistic``配置成``false``，关闭网络流量和``Gas``统计功能```

`  ```![image-20230817152554788](../AppData/Roaming/Typora/typora-user-images/image-20230817152554788.png)

## 配置网络统计日志输出间隔

`由于网络统计日志周期性输出，引入了``log.stat_flush_interval``来控制统计间隔和日志输出频率，单位是秒，默认为``60s``，配置示例如下：```

`  ```![image-20230817152603215](../AppData/Roaming/Typora/typora-user-images/image-20230817152603215.png)

## 配置链属性

`可通过``config.ini``中的`` chain``配置节点的链属性。此配置项建链时会自动生成，用户不需修改。```

## 配置节点兼容性

`FISCO BCOS 2.0+``所有版本向前兼容，可通过``config.ini``中的``[compatibility]``配置节点的兼容性，此配置项建链时工具会自动生成，用户不需修改。```

`重要：```

`可通过`` ./fisco-bcos --version | egrep "Version" ``命令查看``FISCO BCOS``的当前支持的最高版本。```

`build_chain.sh``生成的区块链节点配置中，``supported_version``配置为``FISCO BCOS``当前的最高版本。```

`旧节点升级为新节点时，直接将旧的``FISCO BCOS``二进制替换为最新``FISCO BCOS``二进制即可，千万不可修改``supported_version``。```

`·    ``supported_version``：表示当前节点运行的版本```

`FISCO BCOS 2.9.1``节点的``[compatibility]``配置如下```

`  ```![image-20230817152615936](../AppData/Roaming/Typora/typora-user-images/image-20230817152615936.png)

## 可选配置：落盘加密

```
为了保障节点数据机密性，``FISCO BCOS``引入`[`落盘加密`](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/design/features/storage_security.html)`保障节点数据的机密性。`` 
```

`config.ini``中的``storage_security``用于配置落盘加密，主要包括：```

`·    ``enable``：`` ``是否开启落盘加密，默认不开启；```

`·    ``key_manager_ip``：``Key Manager``服务的部署``IP``；```

`·    ``key_manager_port``：``Key Manager``服务的监听端口；```

`·    ``cipher_data_key: ``节点数据加密密钥的密文。```

`落盘加密节点配置示例如下：```

`  ```![image-20230817152627242](../AppData/Roaming/Typora/typora-user-images/image-20230817152627242.png)

## SDK请求速率限制配置

`SDK``请求速率限制位于配置项``[flow_control].limit_req``中，用于限制``SDK``每秒到节点的最大请求数目，当每秒到节点的请求超过配置项的值时，请求会被拒绝，``SDK``请求速率限制默认关闭，若要开启该功能，需要将``limit_req``配置项前面的``;``去掉``，打开``SDK``请求速率限制并设计节点每秒可接受``1000``个``SDK``请求的示例如下：```

`  ```![image-20230817152645349](../AppData/Roaming/Typora/typora-user-images/image-20230817152645349.png)

## 节点间流量限制配置

`为了防止区块同步、``AMOP``消息传输占用过多的网络流量，并影响共识模块的消息包传输，``FISCO BCOS v2.5.0``引入了节点间流量限制的功能，该配置项用于配置节点平均出带宽的上限，但不限制区块共识、交易同步的流量，当节点平均出带宽超过配置值时，会暂缓区块发送、``AMOP``消息传输。```

`[flow_control]``：节点出带宽限制，单位为``Mbit/s``，当节点出带宽超过该值时，会暂缓区块发送，也会拒绝客户端发送的`[`AMOP`](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/manual/amop_protocol.html)`请求，但不会限制区块共识和交易广播的流量。```

`该配置项默认关闭，若要打开流量限制功能，请将``outgoing_bandwidth_limit``配置项前面的去掉``。```

`打开节点出带宽流量限制，并将其设置为``2MBit/s``的配置示例如下：```

`  ```![image-20230817152653293](../AppData/Roaming/Typora/typora-user-images/image-20230817152653293.png)

`关于``fisco-bcos``中节点配置中的主配置``config.ini``的相关讲解就到此结束。```

```
 
 
```