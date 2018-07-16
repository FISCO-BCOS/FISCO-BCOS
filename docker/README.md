# FISCO BCOS Docker节点部署

[FISCO BCOS Docker Installation Manual](README_EN.md)

本文档能够让初学者快速体验FISCO BCOS平台。初学者无需部署FISCO BCOS平台，仅需在安装了Docker的机器上clone了FISCO-BCOS后运行本文档中的命令，即可启动两个FISCO BCOS节点。

推荐使用Docker 17.03以上版本，安装方法参照[官方文档][https://docs.docker.com/]或本文档附录。

## 启动节点

> 直接执行命令，第一次执行时需下载镜像，请耐心等待

下文中假设目录为/data/fisco-bcos/docker

```shell
sudo -s #务必切换到root下
chmod +x start_fisco_docker.sh
./start_fisco_docker.sh
```

> 执行后可看到提示

```log
--------------FISCO-BCOS Docker Node Info--------------
Local Ip:x.x.x.x
Local Dir:/data/fisco-bcos/docker
Info：
  Name  IP              rpcport         p2pport         channelPort     LogDir
  node0         x.x.x.x          8301            30901           40001           /data/fisco-bcos/docker/node0/log/
  node1         x.x.x.x          8302            30902           40002           /data/fisco-bcos/docker/node1/log/
try to start docker node0...
try to start docker node1...
Check Node Is Running：
        # ps -ef |grep fisco-bcos
Check Node Has Connect Other Node：
        # tail -f /data/fisco-bcos/docker/node1/log/* | grep topics
Check Node Is Working： 
        # tail -f /data/fisco-bcos/docker/node0/log/* | grep ++++
```

## 验证节点正常运行

### 1. 验证进程

```sh
ps -ef |grep fisco-bcos
```

> 可看到2个节点正在运行

```
root      9509  9507 10 15:26 ?        00:00:02 fisco-bcos --genesis /fisco-bcos/node/genesis.json --config /fisco-bcos/node/config.json
root      9510  9508 10 15:26 ?        00:00:02 fisco-bcos --genesis /fisco-bcos/node/genesis.json --config /fisco-bcos/node/config.json
```

### 2. 验证已连接

> 执行命令

```sh
tail -f /data/fisco-bcos/docker/node1/log/* | grep topics
```

> 可以看到如下日志，表示日志对应的节点已经与另一个节点连接，连接正常：

```
topics Send to:1 nodes
```

### 3. 验证可共识

> 执行命令

```sh
tail -f /data/fisco-bcos/docker/node0/log/* | grep ++++
```

> 可看到周期性的出现如下日志，表示节点间在周期性的进行共识，节点运行正确

```
INFO|2018-05-15 07:51:23:576|+++++++++++++++++++++++++++ Generating seal oncf2794b4e8efff7b54057eda340b50b9ca49f558d683246ca4246ca61b39f177#29tx:0,maxtx:1000,tq.num=0time:1526370683576
```

## 附录

### 1. docker部署

以CentOS/Fedora为例

> 安装

```shell
sudo yum install docker
```

> 启动服务

```shell
sudo -s
service docker start
```
