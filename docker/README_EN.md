# FISCO BCOS Docker Installation Manual

[中文版本：使用Docker安装部署FISCO BCOS指南](README.md)

This manual helps a  FISCO BCOS beginner get started quickly. You just need to follow the steps in this documentation to start a blockchain network of two nodes on a linux server where Docker is installed already.

We recommend a Docker version of 17.03 and above. For Docker installation, please refer to [Official Documents](https://docs.docker.com/) or the appendix.

## Start the nodes

> Execute the following command. It may take quite a long time, since the docker images needs to be downloaded for the first time.

```shell
sudo -s # Switch to root
chmod +x start_fisco_docker.sh
./start_fisco_docker.sh
```

> The following execution result shows that docker nodes are started successfully.

```log
--------------Nodes info in docker--------------
Nodes info:
  node name 	IP		rpcport		p2pport		channelPort	log dir
  node0		0.0.0.0		35500		53300		54300		/log-fisco-bcos/node0
  node1		0.0.0.0		35501		53301		54301		/log-fisco-bcos/node1
To check whether the nodes are started:
	# ps -ef |grep fisco-bcos
To check whether the nodes are connected each other:
	# cat /log-fisco-bcos/node0/* | grep peers
To check whether the nodes can seal: 
	# tail -f /log-fisco-bcos/node0/* | grep ++++
```

## Check Correctness

### 1. Check the process

```sh
ps -ef |grep fisco-bcos
```

> The following message indicates that the two nodes are started succesfully.

```
root      9509  9507 10 15:26 ?        00:00:02 fisco-bcos --genesis /bcos-data/node0/genesis.json --config /bcos-data/node0/config.json
root      9510  9508 10 15:26 ?        00:00:02 fisco-bcos --genesis /bcos-data/node1/genesis.json --config /bcos-data/node1/config.json
```

### 2. Check the connection

> Execute the following command:

```sh
cat /log-fisco-bcos/node0/* | grep peers
```

> If the relevant node has been connected to each other, the logs will print the following message:

```
INFO|2017-11-29 07:26:48|Connected to 1 peers
```

### 3. Check the sealing information

> Execute the following command:

```sh
tail -f /log-fisco-bcos/node0/* | grep ++++
```

> If the nodes reached consensus,  the logs will print the following message periodically:

```
INFO|2017-11-29 07:27:39|+++++++++++++++++++++++++++ Generating seal on6eafa4cd3ecb1e80a57588d3ea4ce95b3b4e807bb3da07c35a3931d0f25cda14#1tx:0,maxtx:1000,tq.num=0time:1511940459437
INFO|2017-11-29 07:27:41|+++++++++++++++++++++++++++ Generating seal on972ead626ee8df0f4f6750076733507430de7d020e3844704f600f2fb1d75a9c#1tx:0,maxtx:1000,tq.num=0time:1511940461448
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
