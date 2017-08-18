# BCOS-Docker使用
<!-- TOC -->

- [BCOS-Docker使用](#bcos-docker使用)
    - [1. 单节点网络](#1-单节点网络)
        - [1.1 生成配置文件与启动容器](#11-生成配置文件与启动容器)
        - [1.2 查看工作状态](#12-查看工作状态)
    - [2. 多节点网络](#2-多节点网络)
        - [2.1 配置系统合约](#21-配置系统合约)
        - [2.2 生成新节点配置文件](#22-生成新节点配置文件)
        - [2.3 新节点入网](#23-新节点入网)
        - [2.4 查看网络状态](#24-查看网络状态)

<!-- /TOC -->

## 1. 单节点网络

我们提供Dockerfile文件和`Docker Hub`上预构建的镜像([地址][bcos-docker])，下面示例中当`bcosorg/bcos`镜像不存在时，Docker会自动去仓库拉取，考虑到网络问题，建议配置国内的Docker镜像加速服务。Docker安装参照[官方文档][Docker-Install]。

*注意：如果Docker Hub镜像无法启动，请使用本目录下的Dockerfile手动构建镜像运行，完成后需要修改scripts下脚本内的dockerImage参数为手动构建镜像的名称，继续按照本说明执行即可*

### 1.1 生成配置文件与启动容器

```bash
$ cd bcos/docker 
# 产生配置文件位于文件夹node-0
$ ./scripts/genConfig.sh
# 启动容器
$ ./scripts/start_bcos_docker.sh $PWD/node-0
```

### 1.2 查看工作状态

```bash
# 连接到容器
$ docker exec -it $(docker ps -a | grep bcos-node-0 | awk 'NR==1{print $1}') sh
# 查看日志，blk不断增长说明单节点网络已正常工作

$ tail -f /nodedata/logs/info* | grep "Report"
INFO |2017-07-05 07:37:15|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ Report: blk=314,hash=881cff6fe6f7f8863ee3f9ba6cffa69614337d15523f6a4332503b4b6879b6fe,idx=0, Next: blk=315
INFO |2017-07-05 07:37:16|^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ Report: blk=315,hash=2aede959eabe75405ef2a7b718111e9bf32aa047c3b1d5b6d173a25c228fea96,idx=0, Next: blk=316
```

*********************************************************

## 2. 多节点网络

按照[步骤1](#1-单节点网络)**启动单节点网络**，然后按下述步骤操作

### 2.1 配置系统合约

```bash
$ cd bcos/systemcontractv2
$ cnpm install
# 修改config.js中proxy端口为35500
$ sed -i 's/127.0.0.1:8545/127.0.0.1:35500/' config.js
$ babel-node deploy.js
# 将输出中，SystemProxy合约地址记下来
SystemProxy合约地址 0xff27dc5cc5144c626b9fdc26b2f292d9df062470

# 修改docker/node-0/config.json中systemproxyaddress为上述输出
$ sed -i 's/"systemproxyaddress":"0x0"/"systemproxyaddress":"0xff27dc5cc5144c626b9fdc26b2f292d9df062470"/' ../docker/node-0/config.json
# 重启node-0
$ docker restart $(docker ps -a | grep bcos-node-0 | awk 'NR==1{print$1}')

# 创世节点信息写入合约
$ babel-node tool.js NodeAction registerNode ../docker/node-0/node.json 
```

### 2.2 生成新节点配置文件

执行下面命令生成`node-1`节点配置文件。

```bash
$ cd bcos/docker
# genConfig.sh的参数分别是除创世节点外的组网节点数和创世节点配置文件路径
$ ./scripts/genConfig.sh 1 node-0
```

### 2.3 新节点入网

```bash
$ cd bcos/systemcontractv2
# 新加节点node-1信息写入合约
$ babel-node tool.js NodeAction registerNode ../docker/node-1/node.json 

# 启动新加入节点node-1，参数为新节点配置文件完整路径
$ ../docker/scripts/start_bcos_docker.sh $PWD/../docker/node-1 
# 要加入更多节点只需要重复步骤2.2-2.3，启动新节点时参数改为新节点配置文件路径即可
```

### 2.4 查看网络状态

```bash
# 查看工作状态
# 块高增长说明网络工作正常
$ cd bcos/systemcontractv2
$ babel-node monitor.js
RPC=http://127.0.0.1:35500
Ouputpath=./output/
已连接节点数：1
...........Node 0.........
NodeId:ae8f25ee89f00db93283f7f05be1441780581716e7890b60de87ae49d4bf5e4b4436f496780c10dbed8f85d819a3e2333ef7dcd06bc114ea98ef827cf074d8f3
Host:172.17.0.1:53301

当前块高377
--------------------------------------------------------------
已连接节点数：1
...........Node 0.........
NodeId:ae8f25ee89f00db93283f7f05be1441780581716e7890b60de87ae49d4bf5e4b4436f496780c10dbed8f85d819a3e2333ef7dcd06bc114ea98ef827cf074d8f3
Host:172.17.0.1:53301

当前块高378
```

*********************************************************
[Docker-Install]:https://docs.docker.com/engine/installation/
[official mirror]:https://docs.docker.com/registry/recipes/mirror/#configure-the-docker-daemon
[docker-accelerate]:https://yq.aliyun.com/articles/29941?spm=5176.100239.blogcont7695.18.jyYdbj
[bcos-docker]:https://hub.docker.com/r/bcosorg/bcos/
