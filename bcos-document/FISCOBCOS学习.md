> ### 



### FISCO BCOS学习（一）基本环境部署





>  从零进行环境搭建，拜托环境报错烦恼，为您极大的节省学习成本。

前提:使用命令解决电脑内复制虚拟机不能粘贴问题

```

//安装工具open-vm-tools

sudo apt-get install open-vm-tools
//安装工具open-vm-tools-desktop

sudo apt-get install open-vm-tools-desktop

//重启：reboot



```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)



### 1.安装ubuntu依赖（以后操作都是基于Ubuntu操作）

```
sudo apt install -y openssl curl
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

###   2.创建操作目录，下载安装脚本

```
cd ~ && mkdir -p fisco && cd fisco
 
 curl -#LO https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/FISCO-BCOS/releases/v2.9.1/build_chain.sh && chmod u+x build_chain.sh
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

### 3.准备依赖安装java （推荐使用java 14）

```
sudo apt install -y default-jdk

配置环境
sudo vim /etc/profile
 
export JAVA_HOME=/usr/lib/jvm/java-11-openjdk-amd64
export PATH=$PATH:$JAVA_HOME/bin
 
source /etc/profile  ###更新一下配置文件
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

###  4.获取控制台

控制台相关命令[CSDN](https://mp.csdn.net/mp_blog/creation/editor/134451641)

```
cd ~/fisco && curl -#LO https://gitee.com/FISCO-BCOS/console/raw/master-2.0/tools/download_console.sh && bash download_console.sh
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

![img](https://s2.loli.net/2024/05/23/8X7tVqpGmNiKsPD.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

### 5.webase-front下载安装包（建议在fisco文件下面）

```
wget https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/WeBASE/releases/download/v1.5.5/webase-front.zip
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

![img](https://s2.loli.net/2024/05/23/viwoj9uOWVd67Eg.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

### 6.一键部署环境配置

```
mysql配置

sudo apt-get install mysql-server -y #mysql 服务端

sudo apt install mysql-client -y #mysql 客户端

python3配置

// 添加仓库，回车继续
sudo add-apt-repository ppa:deadsnakes/ppa
// 安装python 3.6
sudo apt-get install -y python3.6
sudo apt-get install -y python3-pip
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

 查询出以下结果说明安装成功。

![img](https://s2.loli.net/2024/05/23/MQSHDV7EmWTY2FU.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

 安装webase-deploy

```
wget https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/WeBASE/releases/download/v1.5.5/webase-deploy.zip
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

> 一键部署启动时还需要对mysql进行初始化操作，具体参考[FISCO BCOS入门（十三）Webase一键部署及其使用-CSDN博客](https://blog.csdn.net/2302_77339802/article/details/134366096)

### 7.获取黑白名单时用到的脚本。

获取证书生成脚本（在nodes/127.0.0.1下进行）



```
curl -#LO https://gitee.com/FISCO-BCOS/FISCO-BCOS/raw/master-2.0/tools/gen_node_cert.sh
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

 一定要等到它出现100%

![img](https://s2.loli.net/2024/05/23/OsKj7Tcf5SZelt6.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

### 8.安装VIM编辑器  

```
sudo apt install npm

sudo apt install vim
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

![img](https://s2.loli.net/2024/05/23/HsJL7IO6Vo5U4uj.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

> 结束~希望本篇博客能帮到您，更多服务私信。



> ### FISCO BCOS学习（二）利用脚本进行区块链系统监控
>
>  要利用脚本进行区块链系统监控，你可以使用各种编程语言编写脚本，如Python、Shell等
>
> 利用脚本进行区块链系统监控可以提高系统的稳定性、可靠性，并帮助及时发现和解决潜在问题，从而确保区块链网络的正常运行。本文可以利用脚本来解决两个问题
>
> 1.编写脚本 1，每隔 1 秒检查一次 fisco 进程数量，若为 4 则打印正常信 息，否则打印错误信息
>
> \2. 编写脚本 2，每隔 3 秒检查一次 fisco 进程占用的端口数量，若为 12 则打印正常信息，否则打印错误信息

 我首先搭建默认的单机四节点链并启动

bash build_chain.sh -l 127.0.0.1:4 -p 30300,20200,8545

![img](https://s2.loli.net/2024/05/23/AnuQMlVstqJbXKg.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

bash nodes/127.0.0.1/start_all.sh 
 

![img](https://s2.loli.net/2024/05/23/Ag7buKmrvNpfe3s.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

 建立脚本jb1.sh和jb2.sh文件并编写相对应的脚本

![img](https://s2.loli.net/2024/05/23/ZUoIz8Ed5My9gBJ.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

![img](https://s2.loli.net/2024/05/23/4jDlaZkYNGAfBbc.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

测试每隔 1 秒检查一次 fisco 进程数量，若为 4 则打印正常信 息，否则打印错误信息

![img](https://s2.loli.net/2024/05/23/Cjv7AIMYe8EOXqN.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

测试脚本 2，每隔 3 秒检查一次 fisco 进程占用的端口数量，若为 12 则打印正常信息，否则打印错误信息

![img](https://s2.loli.net/2024/05/23/8eOvBsqKVZQdST1.png)



### FISCO BCOS学习（三）使用Caliper进行压力测试



>  Caliper是一个用于区块链性能测试和基准测试的工具，使用Caliper进行基准测试可以提供关于区块链系统性能的重要指标，本文章Caliper 测试工具通过调用 Helloorld 合约来进行区块链系统进行压力测试并设置交易数量txNumber=10，交易速率tps=1。

##  前提：

#### 配置基本环境，安装nodejs,安装Docker,安装Docker Compose

参考：[使用Caliper进行压力测试环境部署（ubuntu）_发呆...的博客-CSDN博客](https://blog.csdn.net/2302_77339802/article/details/133793761)

## 加入Docker用户组

```
sudo groupadd docker

//USER为主机名
sudo gpasswd -a ${USER} docker
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

```

# 重启Docker服务
sudo service docker restart
# 验证Docker是否已经启动
sudo systemctl status docker
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)







## 

## Caliper部署

1.新建一个工作目录以及对NPM项目初始化

```

mkdir benchmarks && cd benchmarks
npm init
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

```

```

 这一步主要是为在工作目录下创建package.json文件以方便后续依赖项的安装

2.安装caliper-cli

```
npm install --only=prod @hyperledger/caliper-cli@0.2.0
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

![img](https://s2.loli.net/2024/05/23/jKwJ5WO8T2HaegY.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

验证

```
npx caliper --version
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

![img](https://s2.loli.net/2024/05/23/PgVt4AdoGUkFuCW.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

## 绑定

```
npx caliper bind --caliper-bind-sut fisco-bcos --caliper-bind-sdk latest
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

> **–caliper-bind-sut** ：用于指定需要测试的区块链平台
>
> **–caliper-bind-sdk**：用于指定适配器版本；

 ![img](https://s2.loli.net/2024/05/23/H4NBSFbUrstfV1J.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑





## **执行HelloWorld合约测试**

```
npx caliper benchmark run --caliper-workspace caliper-benchmarks --caliper-benchconfig benchmarks/samples/fisco-bcos/helloworld/config.yaml  --caliper-networkconfig networks/fisco-bcos/4nodes1group/fisco-bcos.json
```

![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

> npx caliper benchmark run: 使用Caliper的benchmark插件执行基准测试。
>
> --caliper-workspace caliper-benchmarks: 指定Caliper项目的工作目录，也就是基准测试配置文件和网络配置文件所在的路径。
>
> --caliper-benchconfig benchmarks/samples/fisco-bcos/helloworld/config.yaml: 指定基准测试配置文件的路径和文件名。该配置文件定义了要运行的基准测试场景、合约、参与者等相关信息。
>
> --caliper-networkconfig networks/fisco-bcos/4nodes1group/fisco-bcos.json: 指定网络配置文件的路径和文件名。该配置文件定义了FISCO-BCOS区块链网络的连接信息，如节点地址、密钥等。

####  现要求：并设置交易数量txNumber=10，交易速率tps=1

更改helloworld目录下的config.yaml文件

![点击并拖拽以移动](https://s2.loli.net/2024/05/23/EwkYoLiANftHsZn.png)编辑

![image-20240523103727307](https://s2.loli.net/2024/05/23/fUQ234kHMLcbJPy.png)编辑



再次执行**HelloWorld合约测试（在benchmarks目录下）**

改之前

![img](https://s2.loli.net/2024/05/23/BUktKTnmlRrzhFA.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

 改之后

![img](https://s2.loli.net/2024/05/23/3sTDZ8LWUrhJe2P.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑



### 压力测试核心代码

1.fisco-bcos.json文件下

![img](https://s2.loli.net/2024/05/23/sobYdq6GTZgXfN9.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

在config.yaml文件

![img](https://s2.loli.net/2024/05/23/THNEjIS6bl7zrZR.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

在get.js文件

![img](https://s2.loli.net/2024/05/23/xQZmTnPsd3BI1AM.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

在set.js

![img](https://img-blog.csdnimg.cn/ae5ef726c77c476b9722497dbdec8407.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

#### 报错1 绑定时遇到报错Error: Failed to execute "npm" with return code 1.    at ChildProcess.proc.on (/home/song/fisco/benchmarks/node_modules/@hyperledger/caliper-cli/lib/utils/cmdutils.js:56:35)    at emitTwo (events.js:126:13)    at ChildProcess.emit (events.js:214:7)

> npm --registry https://registry.npm.taobao.org install express
>  

 ![img](https://s2.loli.net/2024/05/23/vDVjMWUq4pPtsLi.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)编辑

