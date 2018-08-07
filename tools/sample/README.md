# 一键安装FISCO BCOS脚本使用说明

[FISCO BCOS One-Click Installation Manual](README_EN.md)

本文档能够让初学者快速体验FISCO BCOS平台。提供了FISCO BCOS的快速安装和FISCO BCOS节点的快速部署步骤。

## 注意事项：
1. 安装环境需满足FISCO BCOS运行的配置要求[（操作手册1.1节）](../doc/manual/README.md)；
2. build.sh在centos和Ubuntu版本测试成功；
3. 所有Linux发行版本请确保yum和git已安装，并能正常使用；
4. 如遇到执行build.sh时中途依赖库下载失败，一般和网络状况有关。请删除掉所有git clone下载的内容，重新git clone。之后到 https://github.com/bcosorg/lib 找到相应的库，手动下载，并放到相应目录后，再执行build.sh脚本；  
5. 某些步骤会提示输入root密码。

## FISCO BCOS的部署

1. **下载代码**

   ```shell
   git clone https://github.com/FISCO-BCOS/FISCO-BCOS.git
   ```

2. **执行build**

```
   $ cd fisco-bcos
   $ chmod +x build.sh 
   $ ./build.sh
```

   如看到如下的提示说明：FISCO BCOS安装完毕
   ```
      如看到如下的提示说明：FISCO BCOS安装完毕
   ```log
   fisco-bcos build succ! path: /usr/local/bin/fisco-bcos
   ```

   说明：可执行文件安装路径：/usr/local/bin/fisco-bcos


## 部署单机两节点

本步骤指导完成两个区块链节点的部署，两个节点都在一台机器上。两个节点相互连接，形成一条由两个节点组成的区块链。

1. **运行**

   ```shell
   cd sample
   chmod +x run.sh
   ./run.sh
   ```

2. **验证节点是否正常运行**

   **（1）验证进程**

   > 执行命令

   ```shell
   ps -ef |grep fisco-bcos
   ```

   > 可看到2个节点正在运行：

   ```
   root  6226  6225  3 17:22 pts/2    00:00:02 fisco-bcos --genesis /bcos-data/node1/genesis.json --config /bcos-data/node1/config.json
   root  6227  6224  3 17:22 pts/2    00:00:02 fisco-bcos --genesis /bcos-data/node0/genesis.json --config /bcos-data/node0/config.json
   ```

   **（2）验证已连接**

   > 执行命令

   ```shell
   cat /bcos-data/node0/log/* | grep "topics Send to"
   ```

   > 可以看到如下日志，表示日志对应的节点已经与另一个节点连接（Connected to 1 peers），连接正常：

   ```shell
   DEBUG|2018-05-25 21:14:39|topics Send to6a9b9d071fa1e52a12c215ec0f469668f177af4817823e71277f36cbe3e020ff8cbe953c967fbc4d7467cd0eadd7443212d87c99ad38976b2150eccbc1aaa739@127.0.0.1:30304
   ```

   **（3）验证可共识**

   > 执行命令

   ```shell
   tail -f /bcos-data/node0/log/* | grep ++++
   ```

   > 可看到周期性的出现如下日志，表示节点间在周期性的进行共识，节点运行正确

   ```log
   INFO|2017-11-23 15:04:12|+++++++++++++++++++++++++++ Generating seal onc04e60aa22d6348f323de53031744120206f317d3abcb8b3a90be060284b8a5b#1tx:0time:1511420652136
   INFO|2017-11-23 15:04:14|+++++++++++++++++++++++++++ Generating seal on08679a397f9a2d100e0c63bfd33a7c7311401e282406b87fd6c607cfb2dde2c6#1tx:0time:1511420654148
   ```

   ​

## 部署双机四节点

本步骤将会指导完成4个区块链节点的部署。其中两个在FISCO BCOS所安装机器本地，两个在另一台机器上。4个节点相互连接，形成一条由4个节点组成的区块链。

假定安装了FISCO BCOS的机器IP为192.168.1.101，另一台机器IP为192.168.1.102

1. **部署环境**

   > 在另一台未安装FISCO BCOS的机器上，安装依赖环境

   ```shell
   sudo yum -y -q install epel-release
   sudo yum install -y leveldb-devel libmicrohttpd-devel
   ```

2. **生成节点**

   > 在安装了FISCO BCOS的机器上执行

   ```sh
   cd sample
   chmod +x init_four.sh
   ./init_four.sh  192.168.1.101 192.168.1.102 #本机IP在前，另一台机器IP在后
   #此时会生成本地的两个节点，以及另一台机器的节点安装包：192.168.1.102_install.tar.gz 
   scp 192.168.1.102_install.tar.gz app@192.168.1.102:/home/app/ #将安装包拷贝到另一台机器的任意目录
   ```

  *若执行过程出现 “没有那个文件或目录”类似提示信息可忽略。*

3. **启动节点**

   **（1）安装了BCOS的机器**

   ```sh
   cd sample
   ./start_two.sh
   ```

  *若出现 “文本文件忙” 类似提示信息可忽略。*

   **（2）另一台机器**

   ```sh
   cd /home/app/
   tar -zxvf 192.168.1.102_install.tar.gz
   cd 192.168.1.102_install
   chmod +x start_two.sh
   ./start_two.sh
   ```

  *若出现 “文本文件忙” 类似提示信息可忽略。*
  
4. **验证节点正常运行**

   **（1）验证进程**

   > 在两台机器的任意一台上，执行

   ```sh
   ps -ef |grep fisco-bcos
   ```

   > 可看到其中一台机器的2个节点正在运行

   ```
   root 30038 30037  1 17:16 pts/0    00:00:07 fisco-bcos --genesis /bcos-data/node4_3/genesis.json --config /bcos-data/node4_3/config.json
   root 30039 30036  1 17:16 pts/0    00:00:07 fisco-bcos --genesis /bcos-data/node4_2/genesis.json --config /bcos-data/node4_2/config.json
   ```

   **（2）验证已连接**

   > 在后启动节点的机器上，执行命令。（如本例先启动了：192.168.1.101，再启动了192.168.1.102，则在192.168.1.102上执行命令）

   ```sh
   cat /bcos-data/node4_*/log/* | grep "topics Send to"
   
   ```

   > 可看到以下信息，则表示每个区块链节点都连接了除自己以外的3个节点，连接正常。
   ```
   DEBUG|2018-05-08 01:43:14|topics Send to:3 nodes
   ```
 

   **（3）验证可共识**

   > 在两台机器的任意一台上，执行

   ```sh
   tail -f /bcos-data/node4_*/log/* | grep ++++
   ```

   > 可看到周期性的出现如下日志，表示节点间在周期性的进行共识，节点运行正确

   ```
   INFO|2017-11-23 15:39:45|+++++++++++++++++++++++++++ Generating seal on8fc40418b375cef45ba4841dcc4ef7adf7fc536d5a0f00d31f086b44ade64482#1tx:0time:1511422785361
   INFO|2017-11-23 15:39:48|+++++++++++++++++++++++++++ Generating seal on1c11cbd4a6e2b5bdd22e8830978fe3960755b6ec866f54aabf70d0d47a60be1b#1tx:0time:1511422788391
   ```

   如部署过程遇到问题，可参考：[FISCO BCOS常见问题](https://github.com/FISCO-BCOS/Wiki/blob/master/FISCO%20BCOS%E5%B8%B8%E8%A7%81%E9%97%AE%E9%A2%98/README.md)

