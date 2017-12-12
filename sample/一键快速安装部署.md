# 一键安装FISCO BCOS脚本使用说明

本文档能够让初学者快速体验FISCO BCOS平台。提供了FISCO BCOS的快速安装和FISCO BCOS节点的快速部署步骤。
## 注意事项：
1. build.sh在centos和Ubuntu版本测试成功；
2. 所有Linux发行版本请确保yum和git已安装，并能正常使用；
3. 如遇到执行build.sh时中途依赖库下载失败，一般和网络状况有关。请删除掉所有git clone下载的内容，重新git clone。之后到 https://github.com/bcosorg/lib 找到相应的库，手动下载，并放到相应目录后，再执行build.sh脚本；  
4. 某些步骤会提示输入root密码。

## FISCO BCOS的部署

1. **下载代码**

   ```shell
   git clone https://github.com/bcosorg/fisco-bcos
   ```

2. **执行build**

   ```shell
   chmod +x build.sh 
   ./build.sh
   ```

   > 至此，bcoseth安装完毕，安装路径：/usr/local/bin/bcoseth

   注意：在执行build.sh时，若boost库无法下载，一般是网络状况的问题。请先清除所有git clone下载的内容，重新clone代码。clone后进入到deps目录下，新建src文件夹，再将[boost\_1\_63_0.tar.gz](https://github.com/bcosorg/lib/blob/master/dependence/boost_1_63_0.tar.gz)下载到src文件夹内。最后再执行build.sh脚本。
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
   cat /bcos-data/node0/log/* | grep peers
   ```

   > 可以看到如下日志，表示日志对应的节点已经与另一个节点连接（Connected to 1 peers），连接正常：

   ```shell
   INFO|2017-11-22 17:22:06|Connected to 1 peers
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

本步骤将会指导完成4个区块链节点的部署。其中两个在BCOS所安装机器本地，两个在另一台机器上。4个节点相互连接，形成一条由4个节点组成的区块链。

假定安装了BCOS的机器IP为192.168.1.101，另一台机器IP为192.168.1.102

1. **生成节点**

   > 在安装了BCOS的机器上执行

   ```sh
   cd sample
   ./init_four.sh  192.168.1.101 192.168.1.102 #本机IP在前，另一台机器IP在后
   #此时会生成本地的两个节点，以及另一台机器的节点安装包：192.168.1.102_install.tar.gz 
   scp 192.168.1.102_install.tar.gz app@192.168.1.102:/home/app/ #将安装包拷贝到另一台机器的任意目录
   ```

2. **启动节点**

   **（1）安装了BCOS的机器**

   ```sh
   cd sample
   ./start_two.sh
   ```

   **（2）另一台机器**

   ```sh
   cd /home/app/
   tar -zxvf 192.168.1.102_install.tar.gz
   cd 192.168.1.102_install
   chmod +x start_two.sh
   ./start_two.sh
   ```

3. **验证节点正常运行**

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

   > 在两台机器的任意一台上，执行

   ```sh
   cat /bcos-data/node4_*/log/* | grep peers
   ```

   > 可看到如下日志，则表示每个区块链节点都连接了除自己以外的3个节点，连接正常：

   ```
   INFO|2017-11-22 17:16:32|Connected to 3 peers
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

   ​

