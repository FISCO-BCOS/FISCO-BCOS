# FISCO BCOS Docker节点部署

本文档能够让初学者快速体验FISCO BCOS平台。初学者无需部署FISCO BCOS平台，仅需在安装了Docker的机器上运行本文档中的命令，即可启动两个FISCO BCOS节点。

推荐使用Docker 17.03以上版本，安装方法参照[官方文档][Docker-Install]或本文档附录。

## 启动节点

> 直接执行命令，第一次执行时需下载镜像，请耐心等待

```shell
sudo -s #务必切换到root下
chmod +x start_fisco_docker.sh
./start_fisco_docker.sh
```

> 执行后可看到提示

```log
--------------已尝试启动区块链docker节点--------------
节点信息：
  节点名 	IP		rpcport		p2pport		channelPort	日志目录
  node0		0.0.0.0		35500		53300		54300		/log-fisco-bcos/node0
  node1		0.0.0.0		35501		53301		54301		/log-fisco-bcos/node1
验证区块链节点是否启动：
	# ps -ef |grep fisco-bcos
验证一个区块链节点是否连接了另一个：
	# cat /log-fisco-bcos/node0/* | grep peers
验证区块链节点是否能够进行共识： 
	# tail -f /log-fisco-bcos/node0/* | grep ++++
```

##验证节点正常运行

### 1. 验证进程

```sh
ps -ef |grep fisco-bcos
```

> 可看到2个节点正在运行

```
root      9509  9507 10 15:26 ?        00:00:02 fisco-bcos --genesis /bcos-data/node0/genesis.json --config /bcos-data/node0/config.json
root      9510  9508 10 15:26 ?        00:00:02 fisco-bcos --genesis /bcos-data/node1/genesis.json --config /bcos-data/node1/config.json
```

### 2. 验证已连接

> 执行命令

```sh
cat /log-fisco-bcos/node0/* | grep peers
```

> 可以看到如下日志，表示日志对应的节点已经与另一个节点连接（Connected to 1 peers），连接正常：

```
INFO|2017-11-29 07:26:48|Connected to 1 peers
```

###3. 验证可共识

> 执行命令

```sh
tail -f /log-fisco-bcos/node0/* | grep ++++
```

> 可看到周期性的出现如下日志，表示节点间在周期性的进行共识，节点运行正确

```
INFO|2017-11-29 07:27:39|+++++++++++++++++++++++++++ Generating seal on6eafa4cd3ecb1e80a57588d3ea4ce95b3b4e807bb3da07c35a3931d0f25cda14#1tx:0,maxtx:1000,tq.num=0time:1511940459437
INFO|2017-11-29 07:27:41|+++++++++++++++++++++++++++ Generating seal on972ead626ee8df0f4f6750076733507430de7d020e3844704f600f2fb1d75a9c#1tx:0,maxtx:1000,tq.num=0time:1511940461448
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

