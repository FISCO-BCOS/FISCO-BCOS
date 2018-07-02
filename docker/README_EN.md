# FISCO BCOS Docker Installation Manual

[中文版本：使用Docker安装部署FISCO BCOS指南](README.md)

This manual helps a  FISCO BCOS beginner get started quickly. You just need to follow the steps in this documentation to start a blockchain network of two nodes on a linux server where Docker is installed already and FISCO-BCOS git project has been cloned.

We recommend a Docker version of 17.03 and above. For Docker installation, please refer to [Official Documents](https://docs.docker.com/) or the appendix.

## Start the nodes

> Execute the following command. It may take quite a long time, since the docker images needs to be downloaded for the first time.

Suppose that the directory of the node is /data/fisco-bcos/docker

```shell
sudo -s # Switch to root
chmod +x start_fisco_docker.sh
./start_fisco_docker.sh
```

> The following execution result shows that docker nodes are started successfully.

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

## Check Correctness

### 1. Check the process

```sh
ps -ef |grep fisco-bcos
```

> The following message indicates that the two nodes are started succesfully.

```
root      9509  9507 10 15:26 ?        00:00:02 fisco-bcos --genesis /fisco-bcos/node/genesis.json --config /fisco-bcos/node/config.json
root      9510  9508 10 15:26 ?        00:00:02 fisco-bcos --genesis /fisco-bcos/node/genesis.json --config /fisco-bcos/node/config.json
```

### 2. Check the connection

> Execute the following command:

```sh
tail -f /data/fisco-bcos/docker/node1/log/* | grep topics
```

> If the relevant node has been connected to each other, the logs will print the following message:

```
topics Send to:1 nodes
```

### 3. Check the sealing information

> Execute the following command:

```sh
tail -f /data/fisco-bcos/docker/node0/log/* | grep ++++
```

> If the nodes reached consensus,  the logs will print the following message periodically:

```
INFO|2018-05-15 07:51:23:576|+++++++++++++++++++++++++++ Generating seal oncf2794b4e8efff7b54057eda340b50b9ca49f558d683246ca4246ca61b39f177#29tx:0,maxtx:1000,tq.num=0time:1526370683576
```

## Appendix

### 1. Docker Installation

An example for CentOS/Fedora

> Execute the following command to install docker:

```shell
sudo yum install docker
```

> start the docker service:

```shell
sudo -s
service docker start
```
