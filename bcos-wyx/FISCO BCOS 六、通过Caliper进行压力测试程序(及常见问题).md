 **目录**

[1. 环境要求](#1. 环境要求)

[第一步. 配置基本环境（这里我使用的是Ubuntu20.04）](#第一步. 配置基本环境（这里我使用的是Ubuntu20.04）)

[第二步. 安装NodeJS](#第二步. 安装NodeJS)

[第三步. 部署Docker](#第三步. 部署Docker)

[第四步. 安装Docker Compose](#第四步. 安装Docker Compose)

[2. Caliper部署](#2. Caliper部署)

[第一步. 部署](#第一步. 部署)

[第二步. 绑定](# 第二步. 绑定)

[第三步. 快速体验FISCO BCOS基准测试](#第三步. 快速体验FISCO BCOS基准测试)

[3.常见问题](#3.常见问题)

[问题1：dial unix /var/run/docker.sock: connect: permission denied编辑](#问题1：编辑)

[问题二：Depolying error: TypeError: secp256k1.sign is not a function](#问题二：Depolying error: TypeError: secp256k1.sign is not a function)

[问题三：Depolying error: Error: Cannot convert string to buffer. toBuffer only supports 0x-prefixed hex strings and this string was given: 982005432c484060b0c89ec0b321ad72Failed to install smart contract helloworld, path=/home/shijianfeng/fisco/benchmarks/caliper-benchmarks/src/fisco-bcos/helloworld/HelloWorld.sol](# 问题三：)

------



# 1. 环境要求

## 第一步. 配置基本环境（这里我使用的是Ubuntu20.04）

- 部署Caliper的计算机需要有外网权限；
- 操作系统版本需要满足以下要求：**Ubuntu >= 16.04**、**CentOS >= 7**或**MacOS >= 10.14**；
- 部署Caliper的计算机需要安装有以下软件：**python 2.7**、**make**、**g++**、**gcc**及**git**。

## 第二步. 安装NodeJS

- 版本要求：

  NodeJS 8 (LTS), 9, 或 10 (LTS)，Caliper尚未在更高的NodeJS版本中进行过验证。

- 安装指南：

  建议使用**nvm**(Node Version Manager)安装，nvm的安装方式如下：

  ```
  # 安装nvm
  curl -o- https://raw.githubusercontent.com/creationix/nvm/v0.33.2/install.sh | bash
  
  # 若出现因网络问题导致长时间下载失败，可尝试以下命令
  curl -o- https://gitee.com/mirrors/nvm/raw/v0.33.2/install.sh | bash
  
  # 加载nvm配置
  source ~/.$(basename $SHELL)rc
  # 安装Node.js 8
  nvm install 8
  # 使用Node.js 8
  nvm use 8
  ```

  ![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

  ## **第三步. 部署Docker**

- 版本要求：>= 18.06.01

- 安装指南：

  CentOS：

  ```
  # 添加源
  sudo yum-config-manager --add-repo http://mirrors.aliyun.com/docker-ce/linux/centos/docker-ce.repo
  # 更新缓存
  sudo yum makecache fast
  # 安装社区版Docker
  sudo yum -y install docker-ce
  # 将当前用户加入docker用户组（重要）
  sudo groupadd docker
  sudo gpasswd -a ${USER} docker
  # 重启Docker服务
  sudo service docker restart
  newgrp - docker
  # 验证Docker是否已经启动
  sudo systemctl status docker
  ```

  ![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

  Ubuntu

  ```
  # 更新包索引
  sudo apt-get update
  # 安装基础依赖库
  sudo apt-get install \
      apt-transport-https \
      ca-certificates \
      curl \
      gnupg-agent \
      software-properties-common
  # 添加Docker官方GPG key
  curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
  # 添加docker仓库
  sudo add-apt-repository \
  "deb [arch=amd64] https://download.docker.com/linux/ubuntu \
  $(lsb_release -cs) \
  stable"
  # 更新包索引
  sudo apt-get update
  # 安装Docker
  sudo apt-get install docker-ce docker-ce-cli containerd.io
  ```

  ![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

- 加入Docker 用户组

​    CentOS

```
sudo groupadd docker
sudo gpasswd -a ${USER} docker

# 重启Docker服务
sudo service docker restart
# 验证Docker是否已经启动
sudo systemctl status docker
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

  Ubuntu  

```
sudo groupadd docker
sudo usermod -aG docker $USER
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

## 第四步. 安装Docker Compose

- 版本要求：>= 1.22.0
- 安装指南：

```
sudo curl -L "https://github.com/docker/compose/releases/download/1.24.0/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
sudo chmod +x /usr/local/bin/docker-compose
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

# 2. Caliper部署

## 第一步. 部署

Caliper提供了方便易用的命令行界面工具`caliper-cli`，推荐在本地进行局部安装：

1. 建立一个工作目录

```
mkdir benchmarks && cd benchmarks
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

1. 对NPM项目进行初始化

```
npm init
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

这一步主要是为在工作目录下创建package.json文件以方便后续依赖项的安装，如果不需要填写项目信息的话可以直接执行**`npm init -y`**。

1. 安装**`caliper-cli`**

```
npm install --only=prod @hyperledger/caliper-cli@0.2.0
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

由于Caliper所有依赖项的安装较为耗时，因此使用`-**-only=prod**`选项用于指定NPM只安装**Caliper**的核心组件，而不安装其他的依赖项（如各个区块链平台针对Caliper的适配器）。在部署完成后，可以通过**`caliper-cli`**显式绑定需要测试的区块链平台及相应的适配器

1. 验证**`caliper-cli`**安装成功

```
npx caliper --version
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

若安装成功，则会打印相应的版本信息，如：

![e33d90df18de42de8225b1486fc58ca7.png](https://img-blog.csdnimg.cn/e33d90df18de42de8225b1486fc58ca7.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

##  第二步. 绑定

由于Caliper采用了轻量级的部署方式，因此需要显式的绑定步骤指定要测试的平台及适配器版本，**`caliper-cli`**会自动进行相应依赖项的安装。使用**`npx caliper bind`**命令进行绑定，命令所需的各项参数可以通过如下命令查看：

```
user@ubuntu:~/benchmarks$ npx caliper bind --help
Usage:
  caliper bind --caliper-bind-sut fabric --caliper-bind-sdk 1.4.1 --caliper-bind-cwd ./ --caliper-bind-args="-g"

Options:
  --help               Show help  [boolean]
  -v, --version        Show version number  [boolean]
  --caliper-bind-sut   The name of the platform to bind to  [string]
  --caliper-bind-sdk   Version of the platform SDK to bind to  [string]
  --caliper-bind-cwd   The working directory for performing the SDK install  [string]
  --caliper-bind-args  Additional arguments to pass to "npm install". Use the "=" notation when setting this parameter  [string]
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

其中，

**–caliper-bind-sut** ：用于指定需要测试的区块链平台，即受测系统（***S***ystem ***u***nder ***T***est）；

**–caliper-bind-sdk**：用于指定适配器版本；

**–caliper-bind-cwd**：用于绑定**`caliper-cli`**的工作目录，**`caliper-cli`**在加载配置文件等场合时均是使用相对于工作目录的相对路径；

**–caliper-bind-args**：用于指定**`caliper-cli`**在安装依赖项时传递给**`npm`**的参数，如用于全局安装的`-g`。

对于FISCO BCOS，可以采用如下方式进行绑定：

```
npx caliper bind --caliper-bind-sut fisco-bcos --caliper-bind-sdk latest
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

由于FISCO BCOS对于caliper 0.2.0版本的适配存在部分不兼容情况，需要手动按照(https://github.com/FISCO-BCOS/FISCO-BCOS/issues/1248)中的步骤修改代码后方可正常运行。

## 第三步. 快速体验FISCO BCOS基准测试

为方便测试人员快速上手，FISCO BCOS已经为Caliper提供了一组预定义的测试样例，测试对象涵盖HelloWorld合约、Solidity版转账合约及预编译版转账合约。同时在测试样例中，Caliper测试脚本会使用docker在本地自动部署及运行4个互连的节点组成的链，因此测试人员无需手工搭链及编写测试用例便可直接运行这些测试样例。

**在工作目录下下载预定义测试用例**

```
git clone https://github.com/vita-dounai/caliper-benchmarks.git
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

**注意** 若出现网络问题导致的长时间拉取代码失败，则尝试以下方式:

```
# 拉取gitee代码
git clone https://gitee.com/vita-dounai/caliper-benchmarks.git
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

**执行HelloWorld合约测试**

```
npx caliper benchmark run --caliper-workspace caliper-benchmarks --caliper-benchconfig benchmarks/samples/fisco-bcos/helloworld/config.yaml  --caliper-networkconfig networks/fisco-bcos/4nodes1group/fisco-bcos.json
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

![1177298e2a9e43768446dba673ae8eed.png](https://img-blog.csdnimg.cn/1177298e2a9e43768446dba673ae8eed.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

**执行Solidity版转账合约测试**

```
npx caliper benchmark run --caliper-workspace caliper-benchmarks --caliper-benchconfig benchmarks/samples/fisco-bcos/transfer/solidity/config.yaml  --caliper-networkconfig networks/fisco-bcos/4nodes1group/fisco-bcos.json
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

![0246bbc9aeff42e98df16c557ea932a5.png](https://img-blog.csdnimg.cn/0246bbc9aeff42e98df16c557ea932a5.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

**执行预编译版转账合约测试**

```
npx caliper benchmark run --caliper-workspace caliper-benchmarks --caliper-benchconfig benchmarks/samples/fisco-bcos/transfer/precompiled/config.yaml  --caliper-networkconfig networks/fisco-bcos/4nodes1group/fisco-bcos.json
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

![509321b909e3439cb835ca21f7276569.png](https://img-blog.csdnimg.cn/509321b909e3439cb835ca21f7276569.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

测试完成后，会在命令行界面中展示测试结果（TPS、延迟等）及资源消耗情况，同时会在**`caliper-benchmarks`**目录下生成一份包含上述内容的可视化HTML报告。

**`caliper benchmark run`**所需的各项参数可以通过如下命令查看：

```
user@ubuntu:~/benchmarks$ npx caliper benchmark run --help
caliper benchmark run --caliper-workspace ~/myCaliperProject --caliper-benchconfig my-app-test-config.yaml --caliper-networkconfig my-sut-config.yaml

Options:
  --help                   Show help  [boolean]
  -v, --version            Show version number  [boolean]
  --caliper-benchconfig    Path to the benchmark workload file that describes the test client(s), test rounds and monitor.  [string]
  --caliper-networkconfig  Path to the blockchain configuration file that contains information required to interact with the SUT  [string]
  --caliper-workspace      Workspace directory that contains all configuration information  [string]
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

其中，

**–caliper-workspace**：用于指定`caliper-cli`的工作目录，如果没有绑定工作目录，可以通过该选项动态指定工作目录；

**–caliper-benchconfig**：用于指定测试配置文件，测试配置文件中包含测试的具体参数，如交易的发送方式、发送速率控制器类型、性能监视器类型等；

**–caliper-networkconfig**：用于指定网络配置文件，网络配置文件中会指定Caliper与受测系统的连接方式及要部署测试的合约等。

# 3.常见问题

## 问题1：dial unix /var/run/docker.sock: connect: permission denied  ![d5bf67ab97db40918ba37af77c280d1d.png](https://img-blog.csdnimg.cn/d5bf67ab97db40918ba37af77c280d1d.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

###  解决办法

```
##添加docker用户组
sudo groupadd docker
##把当前用户加入docker用户组
sudo usermod -aG docker $USER
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

## 问题二：Depolying error: TypeError: secp256k1.sign is not a function

![30747111946c473b948c85b64b3c5c70.png](https://img-blog.csdnimg.cn/30747111946c473b948c85b64b3c5c70.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

###  解决办法

这是一个愚蠢的bug，指定**secp256k1**依赖包时版本限制没写对，导致在绑定时时自动安装了4.0版本的**secp256k1**包，但是最新的4.0的包API全部变了，导致执行出错。

有一个临时的解决方案，进入**node_modules/@hyperledger/caliper-fisco-bcos**目录，编辑该目录下的**package.json**文件，在"**dependencies**"中添加一项**"secp256k1": "^3.8.0"**，随后在该目录下执行**npm i**，更新完成后测试程序就能启动了。

![bd8ed0e82be44a97966c4ce323ae087b.png](https://img-blog.csdnimg.cn/bd8ed0e82be44a97966c4ce323ae087b.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

##  问题三：Depolying error: Error: Cannot convert string to buffer. toBuffer only supports 0x-prefixed hex strings and this string was given: 982005432c484060b0c89ec0b321ad72Failed to install smart contract helloworld, path=/home/shijianfeng/fisco/benchmarks/caliper-benchmarks/src/fisco-bcos/helloworld/HelloWorld.sol

![f10a969ab1c74e35acd3b88c6e0cc7f2.png](https://img-blog.csdnimg.cn/f10a969ab1c74e35acd3b88c6e0cc7f2.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

 这是官方的一个issue，需要修改几个地方

![dbaa67c0f37e49218455876b972506c9.png](https://img-blog.csdnimg.cn/dbaa67c0f37e49218455876b972506c9.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

![6e17259ab24e489fa5964e7db4cd19fa.png](https://img-blog.csdnimg.cn/6e17259ab24e489fa5964e7db4cd19fa.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

![123bde680bf4434ea8966d64bd574e23.png](https://img-blog.csdnimg.cn/123bde680bf4434ea8966d64bd574e23.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

![5cbbb1d762a5481cbef211a0e51cb8dc.png](https://img-blog.csdnimg.cn/5cbbb1d762a5481cbef211a0e51cb8dc.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

看好文件！！！

 只需要将上述图片中需要修改的文件修改即可解决。