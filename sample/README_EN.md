# FISCO BCOS One-Click Installation Manual

[中文版本：一键快速安装部署](README.md)

This manual helps FISCO BCOS beginners get started quickly.  FISCO BCOS provides automation scripts to help you install and deploy FISCO BCOS easily.

## Notes:
1. The installation environment shall meet the prerequisties of FISCO BCOS.[（FISCO BCOS Manual 1.1）](../doc/manual/README.md);
2. One-Click installation script (build.sh) is applicable to CentOS and Ubuntu;
3. Make sure *yum* & *git* are installed on your Linux;
4. If you fail to download the dependencies when running *build.sh*, it is most likely due to the network condition. Please delete all the git-cloned contents of FISCO BCOS, and git clone again. After that, please find and download the relevant dependencies in https://github.com/bcosorg/lib and copy them to the corresponding directories. Then run *build.sh* again;  
5. In some steps, root password is required.

## Installation and Deployment

**1. Download source code**

   ```shell
   git clone https://github.com/FISCO-BCOS/FISCO-BCOS.git
   ```

**2. Run build.sh***

   ```shell
   cd FISCO-BCOS
   chmod +x build.sh 
   ./build.sh
   ```
   
> The following message indicates FISCO BCOS has been installed successfully.

   ```log
   fisco-bcos build succ! path: /usr/local/bin/fisco-bcos
   ```

 > Tips: the path of the executable files: /usr/local/bin/fisco-bcos


## Deployment of Two Nodes on One Single Server

This part will help deploy a two-node blockchain network. The two nodes are in the same server and connected with each other to constitute a blockchain.

**1. Run**

   ```shell
   cd sample
   chmod +x run.sh
   ./run.sh
   ```

**2. Check the Nodes**

   **(1) Check the Process**

   > Execute the following command:

   ```shell
   ps -ef |grep fisco-bcos
   ```

   > The following message inidicates that the two nodes are running

   ```
   root  6226  6225  3 17:22 pts/2    00:00:02 fisco-bcos --genesis /bcos-data/node1/genesis.json --config /bcos-data/node1/config.json
   root  6227  6224  3 17:22 pts/2    00:00:02 fisco-bcos --genesis /bcos-data/node0/genesis.json --config /bcos-data/node0/config.json
   ```

   **(2) Check the Network Connection**

   > Execute the following command:

   ```shell
    cat /bcos-data/node0/log/* | grep "topics Send to"
   ```

   > The following message indicates the two nodes are connected with each other successfully. 

   ```shell
    DEBUG|2018-05-25 21:14:39|topics Send to6a9b9d071fa1e52a12c215ec0f469668f177af4817823e71277f36cbe3e020ff8cbe953c967fbc4d7467cd0eadd7443212d87c99ad38976b2150eccbc1aaa739@127.0.0.1:30304
   ```

   **(3) Check the Consensus Mechanism**

   > Execute the following command:

   ```shell
   tail -f /bcos-data/node0/log/* | grep ++++
   ```

   > If the nodes are installed successfully, their logs will print the following message periodically. This means that the nodes are reaching consensus periodically.

   ```log
   INFO|2017-11-23 15:04:12|+++++++++++++++++++++++++++ Generating seal onc04e60aa22d6348f323de53031744120206f317d3abcb8b3a90be060284b8a5b#1tx:0time:1511420652136
   INFO|2017-11-23 15:04:14|+++++++++++++++++++++++++++ Generating seal on08679a397f9a2d100e0c63bfd33a7c7311401e282406b87fd6c607cfb2dde2c6#1tx:0time:1511420654148
   ```

   

## Deployment of Four Nodes on Two Servers

This section introduces how to deploy a four-node FISCO BCOS. Among which two nodes are installed on one server and the other two nodes are installed on the other one. Four nodes are connected with each other, constitute a four-node blockchain.

Suppose two servers are provided, the IP of one server with FISCO BCOS installed is 192.168.1.101, the onther one's IP is 192.168.1.102.

**1. Deploy  software dependencies**

   > Install the required software with the following command: 

   ```shell
   yum -y -q install epel-release
   yum install -y leveldb-devel libmicrohttpd-devel
   ```

**2. Deploy the nodes **

   > Execute the following commands on the server which has already deployed FISCO BCOS (192.168.1.101)

   ```sh
   cd sample
   ./init_four.sh  192.168.1.101 192.168.1.102 #Local IP first
   
   #two nodes will be deployed on "192.168.1.101" and a node-installation package for another server 192.168.1.102 will be genearted, name of generated package is 192.168.1.102_install.tar.gz
   
   scp 192.168.1.102_install.tar.gz app@192.168.1.102:/home/app/ #Copy the installation package to any directory of the other server(192.168.1.102).
   ```

**3. Start the nodes**

   **(1) For the server with FISCO BCOS**

   ```sh
   cd sample
   ./start_two.sh
   ```

   **(2) For the other server**

   ```sh
   cd /home/app/
   tar -zxvf 192.168.1.102_install.tar.gz
   cd 192.168.1.102_install
   chmod +x start_two.sh
   ./start_two.sh
   ```

**4. Check the nodes**

   **(1) Check the process**

   > Execute the following command on the two servers:

   ```sh
   ps -ef |grep fisco-bcos
   ```

   > The following message indicates that the server has started two nodes successfully.

   ```
   root 30038 30037  1 17:16 pts/0    00:00:07 fisco-bcos --genesis /bcos-data/node4_3/genesis.json --config /bcos-data/node4_3/config.json
   root 30039 30036  1 17:16 pts/0    00:00:07 fisco-bcos --genesis /bcos-data/node4_2/genesis.json --config /bcos-data/node4_2/config.json
   ```

   **(2) Check the Network Connection**

   > Execute the following comand on the latest-started server (For example, we start 192.168.1.101 firstly and then 192.168.1.102. We should execute the command on 192.168.1.102 to check the network connection).

   ```sh
   cat /bcos-data/node4_*/log/* | grep "topics Send to"
   ```

   > The following logs show that every node has been connected to each other successfully.

   ```
    DEBUG|2018-05-08 01:43:14|topics Send to:3 nodes
   ```

   **(3) Check the Consensus Mechanism**

   > Execute the following command on one of the servers:

   ```sh
   tail -f /bcos-data/node4_*/log/* | grep ++++
   ```

   > The following logs show that the nodes are reaching  consensus periodically.

   ```
   INFO|2017-11-23 15:39:45|+++++++++++++++++++++++++++ Generating seal on8fc40418b375cef45ba4841dcc4ef7adf7fc536d5a0f00d31f086b44ade64482#1tx:0time:1511422785361
   INFO|2017-11-23 15:39:48|+++++++++++++++++++++++++++ Generating seal on1c11cbd4a6e2b5bdd22e8830978fe3960755b6ec866f54aabf70d0d47a60be1b#1tx:0time:1511422788391
   ```

 > If there are still some problems with installation and deployment, please refer to: [FISCO BCOS FAQ](https://github.com/FISCO-BCOS/Wiki/blob/master/FISCO%20BCOS%E5%B8%B8%E8%A7%81%E9%97%AE%E9%A2%98/README.md)