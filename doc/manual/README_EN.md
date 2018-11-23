# FISCO-BCOS Manual  

[中文版本：FISCO BCOS使用说明书1.0](README.md)

## Preface  

This documentation is a part of the Financial Blockchain Shenzhen Consortium Blockchain Open Source platform (FISCO-BCOS).     

The functional upgrades and remodeling are developed by Financial Blockchain Shenzhen Consortium (hereinafter referred to FISCO)  Open Source Working Group based on the original BCOS. The objective is to create a secure and controllable blockchain platform that meets the requirements of the financial industry. Members in the working group include: WeBank, Shenzhen Securities Communications, Tencent, Huawei, Forms Syntron, Digital China, Beyondsoft, Yuexiu Financial Holdings (Fintech) and more.  

Focusing on distributed business requirements of the financial industry, FISCO-BCOS is built based on the current BCOS open source project. It provides a blockchain solution for the financial industry from all aspects including business value, performance, security, government policy, technical feasibility, operation and maintenance, cost and more.

In order to give you a better understanding of how to use FISCO-BCOS, this documentation introduce the construction, installation, and launch of FISCO-BCOS step by step, as well as the basic deployment and invocation of smart contract. It also includes a higher level introduction of multi-node network and system contract.

This documentation does not include the vision and concept of FISCO-BCOS. For more details, please refer to the FISCO-BCOS white paper.

The followings instructions work well on Centos 7.2/Ubuntu 16.04 and above.


## Chapter 1  Setting Up FISCO-BCOS  

This chapter mainly introduce how to install all the prerequisites and compile the code.

### 1.1 Hardware  

| Requirements     | Minimum | Recommended                             |
| ---------------- | ------- | --------------------------------------- |
| CPU              | 1.5GHz  | 2.4GHz                                  |
| Memory           | 1GB     | 4GB                                     |
| Core Number      | 2       | 4                                       |
| Bandwidth        | 1Mb     | 5Mb                                     |
| Operation System |         | CentOS (7.2  64x) or Ubuntu(16.04  64x) |
| JAVA             |         | Java(TM) 1.8 && JDK 1.8                 |
### 1.2 Install Environments

#### 1.2.1 Install dependencies

**For Centos**

```shell
#install dependencies
sudo yum install -y git openssl openssl-devel deltarpm cmake3
#install nodejs
sudo yum install -y nodejs
sudo npm install -g cnpm --registry=https://registry.npm.taobao.org
sudo cnpm install -g babel-cli babel-preset-es2017 ethereum-console
echo '{ "presets": ["es2017"] }' > ~/.babelrc
```
**For Ubuntu**

```shell
#install dependencies
sudo apt-get -y install git openssl libssl-dev libkrb5-dev cmake
#install nodejs(Note: Required version of nodejs is 6 or above. Update nodejs using apt-get on Ubuntu)
sudo apt-get -y install nodejs-legacy
sudo apt-get -y install npm
sudo npm install -g secp256k1
sudo npm install -g cnpm --registry=https://registry.npm.taobao.org
sudo cnpm install -g babel-cli babel-preset-es2017 ethereum-console
echo '{ "presets": ["es2017"] }' > ~/.babelrc
```

#### 1.2.2 Install FISCO-BCOS smart contract compiler

> The compiler name is *fisco-solc*. Download to /usr/bin.

```shell
wget https://github.com/FISCO-BCOS/FISCO-BCOS/raw/master/fisco-solc
sudo cp fisco-solc  /usr/bin/fisco-solc
sudo chmod +x /usr/bin/fisco-solc
```

### 1.3 Build and Install

#### 1.3.1 Get the code

> Clone the code to the directory, eg */mydata*:

```shell
#create mydata dir
sudo mkdir -p /mydata
sudo chmod 777 /mydata
cd /mydata

#git clone
git clone https://github.com/FISCO-BCOS/FISCO-BCOS.git

#change to FISCO-BCOS root dir
cd FISCO-BCOS 
```

For more, please refer to <u>Appendix:11.1 Source Code Structure</u>

#### 1.3.2 Install compile dependency

> In *FISCO-BCOS* root directory:

```shell
chmod +x scripts/install_deps.sh
./scripts/install_deps.sh
```

#### 1.3.3 Compile

```shell
mkdir -p build
cd build/

[Centos] 
cmake3 -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF ..  #Note the .. in the end
[Ubuntu] 
cmake  -DEVMJIT=OFF -DTESTS=OFF -DMINIUPNPC=OFF ..  #Note the .. in the end

make
```

> If success, fisco-bcos is generated in *build/eth/*.

#### 1.3.4 Installation

```shell
sudo make install
```

## Chapter2  Certificates Generation

To guarantee confidentiality, confirmability, integrality and non-tamperability of message transfered among nodes of fisco-bcos blockchain, FISCO-BCOS implements a secure network that grant network-access authority for nodes by verifying certificates.

Each blockchain poses a chain-certificate and corresponding private key, the private key of the chain-certificate is managed by administrator of the blockchain. Administrator of the blockchain can issue certificate for agencies with the chain-certificate and private key, the private keys of the agency-certificates are managed by administrators of agencies. Administrator of agency can issue node-certificates for nodes owned by the agency, the node-certificate is used to establish SSL-connections with other nodes, SSL-connections ensure the confidentiality, confirmability, integrality of FISCO-BCOS.

According to the above description, you shall generate chain-certificate, agency-certificates and node-certificates before deploy FISCO-BCOS nodes.

### 2.1 Generate Certificates for the Blockchains

This section mainly descripts how to generate chain-certificates.

```shell
cd /mydata/FISCO-BCOS/cert/
chmod +x *.sh
./chain.sh  # will output some certificate related information, input enter by default
```
Chain-certificate related files will be generated in /mydata/FISCO-BCOS/cert/ directory after executing above commands.

**Notes：please keep private key of chain-certificate, namely ca.key cautiously**


### 2.2 Generate Certificates for Agencies

This section mainly introduces how to generate agency-certificates.

Suppose that the agency is named of **WB**, you can generate agency-certificate for agency **WB** by executing the following commands:

```shell
cd /mydata/FISCO-BCOS/cert/
./agency.sh WB # will output some certificate related information, input enter by default
# you can generate multiple certificates for multiple agencies by executing the command **./agency.sh agency_name** repeatedly
```
Agency-certificate related files will be generated in directory /mydata/FISCO-BCOS/cert/.

**Notes: please keep the private key of the agency-certificate, namely agency.key cautiously**

### 2.3 Generate Certificates for Nodes

This section mainly introduces how to generate node-certificates. 

Suppose that generating node-certificate for **nodedata-1** of agency **WB**, please execute the following commands:

```shell
cd /mydata/FISCO-BCOS/cert/
./node.sh WB nodedata-1 # will output some certificate related information, input enter by default
# you can generate multiple certificates for multiple nodes by executing the command **./node.sh agency_name node_name** repeatedly
```

**Note: please keep private keys of nodes, namely node.key cautiously**

### 2.4 Generate Certificates for Web3sdk

Web3sdk needs generates certificate to communicate with nodes of FISCO-BCOS, too. This section mainly introduces how to generate sdk-certificates.

```
shell
cd /mydata/FISCO-BCOS/cert/
./sdk.sh WB sdk
```

sdk subdirectory will be generated in /mydata/FISCO-BCOS/cert/WB/ directory after executing above commands, please copy all files in /mydata/FISCO-BCOS/cert/WB/sdk directory to the certificate-directory of web3sdk after sdk-certificate generated.

**Note: please keep the private key of web3sdk, namely sdk.key cautiously**

### 2.5 Extra Illustration

Files located in /mydata/FISCO-BCOS/cert/WB/nodedata-1 directory are demanded for deploying nodedata-1 of FISCO-BCOS. nodedata-1 mainly requires files below:

ca.crt: chain-certificate

agency.crt: agency-certificate of agency **WB**

node.crt: node-certificate of **nodedata-1**

node.key: the private key of **nodedata-1**

node.nodeid: nodeid of **nodedata-1**

node.serial: serial number of certificate of nodedata-1

node.json: configuration file of nodedata-1, node.json is used to register nodedata-1 into miners or remove nodedata-1 from the miners by using system contracts tools.

node.ca: certificate information of nodedata-1, node.ca is used to add certificate of nodedata-1 into **revocation list of certificates** or remove certificate of nodedata-1 from **revocation list of certificates** by using system contract tools.





## Chapter3  Deploy Genesis Node

The genesis node is the first node of a FISCO-BCOS blockchain. So let's get started from creating a genesis node.

### 3.1 Setup Node Environment

> Eg: Genesis node's directory is */mydata/nodedata-1/* :

```shell
#Create directory structure
mkdir -p /mydata/nodedata-1/
mkdir -p /mydata/nodedata-1/data/ 
mkdir -p /mydata/nodedata-1/log/ 
mkdir -p /mydata/nodedata-1/keystore/ 

#copy files
cd /mydata/FISCO-BCOS/ 
cp genesis.json config.json log.conf start.sh stop.sh /mydata/nodedata-1/
```

### 3.2 Generate God Account

God account owns the highest priority of the blockchain. Create it before running our chain. 

#### 3.2.1 Generate a god account

```shell
cd /mydata/FISCO-BCOS/tool 
cnpm install #install nodejs. Only required for the first time.
node accountManager.js > godInfo.txt
cat godInfo.txt |grep address
```

> And we get the address of the god account

```log
address : 0x27214e01c118576dd5f481648f83bb909619a324
```

#### 3.2.2 Configure god account

> Configure god address in *genesis.json*

```shell
vim /mydata/nodedata-1/genesis.json
```

> Like

```
"god":"0x27214e01c118576dd5f481648f83bb909619a324",
```

### 3.3  Generate Genesis Node ID

Each node has a  node ID, generate it before starting up a node.

#### 3.3.1 Generate node ID file

(1) Query NodeId

You can query NodeId of specified node by executing the following command:

```shell
cat /mydata/nodedata-1/data/node.nodeid
```
> We will get a node ID like:

```log
2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878
```

(2) Modify genesis block filie *genesis.json*

> Configure node ID into the field "*initMinerNodes*" in *genesis.json*, which demonstrate this node is the genesis node of the blockchain.

```shell
vim /mydata/nodedata-1/genesis.json
```

> Like

```log
"initMinerNodes":["2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878"]
```

## 3.4 Configure **bootstrapnodes.json**

Node of FISCO BCOS need configure connection information to connect with other nodes at boot time, bootstrapnodes.json is used to configure connection information. Since there are no other nodes at boot time of the node, you can configure the bootstrap-nodes to the node itself by copying the given bootstrapnodes.json from `/mydata/FISCO-BCOS/bootstrapnodes.json`. Also, we suggest that you configure **host** located in bootstrapnodes.json with the external network ip.

```shell
cp /mydata/FISCO-BCOS/bootstrapnodes.json /mydata/nodedata-1/data
```

> Below is the sample of bootstrapnodes.json：

```log
{"nodes":[{"host":"127.0.0.1","p2pport":"30303"}]}

```

`host` should be configurated to ip or domain name, p2pport should be configurated to port used to establish p2p connections.

### 3.5 Configure Node Configuration Files

To start a node, we need 3 configuration files:

- Genesis block file: *genesis.json*
- Node configuration file: *config.json*
- Log configuration file: *log.conf*

#### 3.5.1 Configure *genesis.json*

*genesis.json* describes genesis block information. 

```shell
vim /mydata/nodedata-1/genesis.json
```

> Generally, we only need to configure "god" and "initMinerNodes", like

```log
{
     "nonce": "0x0",
     "difficulty": "0x0",
     "mixhash": "0x0",
     "coinbase": "0x0",
     "timestamp": "0x0",
     "parentHash": "0x0",
     "extraData": "0x0",
     "gasLimit": "0x13880000000000",
     "god":"0x27214e01c118576dd5f481648f83bb909619a324",
     "alloc": {},
     "initMinerNodes":["2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878"]
}
```

Introduction of the fields in *genesis.json* see: <u>Appendix:11.3 *genesis.json* Instructions</u>

#### 3.5.2 Configure *config.json* 

*config.json* describes a node starting up information, includes  IP address, port, data directory, node ID etc. 

```shell
vim /mydata/nodedata-1/config.json
```

> Generally, we need to configure these fields:
>
> - Network setting: listenip, rpcport, p2pport, channelPort
> - Drectory setting: wallet, keystoredir, datadir, logconf
> - Node ID setting: sub fields of NodeextraInfo: Nodeid, Nodedesc, Agencyinfo, Peerip, Identitytype, Port, Idx (should be the same as network setting)
>
> Like:

```log
{
        "sealEngine": "PBFT",
        "systemproxyaddress":"0x0",
        "listenip":"127.0.0.1",
        "cryptomod":"0",
        "rpcport": "8545",
        "p2pport": "30303",
        "channelPort": "30304",
        "rpcsslport":"-1",
        "wallet":"/mydata/nodedata-1/keys.info",
        "keystoredir":"/mydata/nodedata-1/keystore/",
        "datadir":"/mydata/nodedata-1/data/",
        "vm":"interpreter",
        "networkid":"12345",
        "logverbosity":"4",
        "coverlog":"OFF",
        "eventlog":"ON",
        "statlog":"OFF",
        "logconf":"/mydata/nodedata-1/log.conf",
}
```

#### 3.5.3 Configure *log.conf* 

Format and path of logs are configured in *log.conf*.

```shell
vim /mydata/nodedata-1/log.conf 
```

> Generally, we only need to configure the path. Like

```log
* GLOBAL:
    ENABLED                 =   true
    TO_FILE                 =   true
    TO_STANDARD_OUTPUT      =   false
    FORMAT                  =   "%level|%datetime{%Y-%M-%d %H:%m:%s:%g}|%msg"
    FILENAME                =   "/mydata/nodedata-1/log/log_%datetime{%Y%M%d%H}.log"
    MILLISECONDS_WIDTH      =   3
    PERFORMANCE_TRACKING    =   false
    MAX_LOG_FILE_SIZE       =   209715200 ## 200MB - Comment starts with two hashes (##)
    LOG_FLUSH_THRESHOLD     =   100  ## Flush after every 100 logs

* TRACE:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-1/log/trace_log_%datetime{%Y%M%d%H}.log"

* DEBUG:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-1/log/debug_log_%datetime{%Y%M%d%H}.log"

* FATAL:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-1/log/fatal_log_%datetime{%Y%M%d%H}.log"

* ERROR:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-1/log/error_log_%datetime{%Y%M%d%H}.log"

* WARNING:
     ENABLED                 =   true
     FILENAME                =   "/mydata/nodedata-1/log/warn_log_%datetime{%Y%M%d%H}.log"

* INFO:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-1/log/info_log_%datetime{%Y%M%d%H}.log"

* VERBOSE:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-1/log/verbose_log_%datetime{%Y%M%d%H}.log"
```

Introduction of other fields in *log.conf* see: <u>Appendix:11.5 *log.conf* Instructions</u>

### 3.6 Start Up Genesis Node

Check again that all these files below are correct: 

- Certificate files(/mydata/nodedata-1/data): *ca.crt, server.crt, server.key*
- Node ID files (/mydata/nodedata-1/data): *network.rlp, network.rlp.pub*
- Configuration files(/mydata/nodedata-1/): *genesis.json, config.json, log.conf*

> To start up node, run

```shell
cd /mydata/nodedata-1/
chmod +x *.sh
./start.sh
#To stop the node
#./stop.sh
```

> or start up manually

```shell
cd /mydata/nodedata-1/
fisco-bcos --genesis ./genesis.json --config ./config.json & #start the node
tail -f log/info* |grep ++++  #view the logs
#To stop the node
#ps -ef |grep fisco-bcos #view the process id
#kill 13432 #kill the process by process id 13432 
```

> Wait for a few seconds, get sealing information constantly.

```log
INFO|2017-12-12 17:52:16:877|+++++++++++++++++++++++++++ Generating seal ondcae019af78cf04e17ad908ec142ca4e25d8da14791bda50a0eeea782ebf3731#1tx:0,maxtx:1000,tq.num=0time:1513072336877
INFO|2017-12-12 17:52:17:887|+++++++++++++++++++++++++++ Generating seal on3fef9b23b0733ac47fe5385072f80fc036b7517abae0a3e7762739cc66bc7dca#1tx:0,maxtx:1000,tq.num=0time:1513072337887
INFO|2017-12-12 17:52:18:897|+++++++++++++++++++++++++++ Generating seal onb5b38c7a380b13b2e46fecbdca0fac5473f4cbc054190e90b8bd4831faac4521#1tx:0,maxtx:1000,tq.num=0time:1513072338897
INFO|2017-12-12 17:52:19:907|+++++++++++++++++++++++++++ Generating seal on3530ff04adddd30508a4cb7421c8f3ad6421ca6ac3bb5f81fb4880fd72c57a8c#1tx:0,maxtx:1000,tq.num=0time:1513072339907
```

### 3.7 Check Node Started

#### 3.7.1 Check the process

```shell
ps -ef |grep fisco-bcos
```

> Like

```log
app 19390     1  1 17:52 ?        00:00:05 fisco-bcos --genesis /mydata/nodedata-1/genesis.json --config /mydata/nodedata-1/config.json
```

#### 3.7.2 Check logs

> Check sealing information

```shell
tail -f /mydata/nodedata-1/log/info* |grep ++++  #view the logs
```

> Like

```log
INFO|2017-12-12 17:52:16:877|+++++++++++++++++++++++++++ Generating seal ondcae019af78cf04e17ad908ec142ca4e25d8da14791bda50a0eeea782ebf3731#1tx:0,maxtx:1000,tq.num=0time:1513072336877
INFO|2017-12-12 17:52:17:887|+++++++++++++++++++++++++++ Generating seal on3fef9b23b0733ac47fe5385072f80fc036b7517abae0a3e7762739cc66bc7dca#1tx:0,maxtx:1000,tq.num=0time:1513072337887
INFO|2017-12-12 17:52:18:897|+++++++++++++++++++++++++++ Generating seal onb5b38c7a380b13b2e46fecbdca0fac5473f4cbc054190e90b8bd4831faac4521#1tx:0,maxtx:1000,tq.num=0time:1513072338897
INFO|2017-12-12 17:52:19:907|+++++++++++++++++++++++++++ Generating seal on3530ff04adddd30508a4cb7421c8f3ad6421ca6ac3bb5f81fb4880fd72c57a8c#1tx:0,maxtx:1000,tq.num=0time:1513072339907
```

If everything listed above is right. Congratulations, you have create a blockchain!

## Chapter 4 Deploy and Use Smart Contract

Smart contract is an application deployed on the blockchain. Developers can develop many smart contracts according to their own needs and deploy them on the blockchain.

Smart contract is written by *Solidity* language, and compiled by *fisco-solc* . This chapter will show how to deploy and call a contract function with an example of *HelloWorld.sol*.

### 4.1 Setup

> Change to tool directory

```shell
cd /mydata/FISCO-BCOS/tool
```

> Install required environment.

```shell
cnpm install
```

> Configure RPC port of the node where contract operations sent to

```shell
vim config.js
```

> The RPC port must point to the RPC port of genesis node on chain. (refer to:<u>2.5.2 *config.json* Configuration</u> ). Like

```javascript
var proxy="http://127.0.0.1:8545";
```

### 4.2 Deploy Smart Contract

#### 4.2.1 Write smart contract

```shell
cd /mydata/FISCO-BCOS/tool
vim HelloWorld.sol
```

> *HelloWorld.sol* like

```solidity
pragma solidity ^0.4.2;
contract HelloWorld{
    string name;
    function HelloWorld(){
       name="Hi,Welcome!";
    }
    function get()constant returns(string){
        return name;
    }
    function set(string n){
        name=n;
    }
}
```

#### 4.2.2 Compile and deploy

Make sure that *config.js* has pointed to a stared node's RPC port.

> Use deploy.js to compile and deploy simultaneously

```shell
babel-node deploy.js HelloWorld #Note: there is no ".sol" after HelloWorld.
```

> If success, output smart contract address

```log
deploy.js  ........................Start........................
Soc File :HelloWorld
HelloWorldcomplie success！
send transaction success: 0xa8c1aeed8e85cc0308341081925d3dab80da394f6b22c76dc0e855c8735da481
HelloWorldcontract address 0xa807685dd3cf6374ee56963d3d95065f6f056372
HelloWorld deploy success!
```

### 4.3 Call Smart Contract Function

#### 4.3.1 Program to call smart contract

> Consult with demoHelloWrold.js

```shell
vim demoHelloWorld.js
```

#### 4.3.2 Call smart contract function 

> Run demoHelloWorld.js

```shell
babel-node demoHelloWorld.js
```

> If success, get 

```log
{ HttpProvider: 'http://127.0.0.1:8545',
  Ouputpath: './output/',
  privKey: 'bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd',
  account: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3' }
HelloWorldcontract address:0xa807685dd3cf6374ee56963d3d95065f6f056372
HelloWorld contract get function call first :Hi,Welcome!
send transaction success: 0x6463e0ea9db6c4aff1e3fc14d9bdb86b29306def73e6d951913a522347526435
HelloWorld contract set function call , (transaction hash ：0x6463e0ea9db6c4aff1e3fc14d9bdb86b29306def73e6d951913a522347526435)
HelloWorld contract get function call again :HelloWorld!
```



## Chapter 5 Deploy System Contracts

System contract is a majority design of FISCO-BCOS. A FISCO-BCOS block only needs one system contract. Using system contract,  members of a FISCO-BCOS blockchain can control the features of the chain. For example, members can use system contract to determine whether a new member can join the chain or not. For more information about system contract, please refer to <u>Appendix:11.7 System Contract Instructions</u>.

### 5.1 Setup

> Change directory to systemcontractv2 where we deploy our system contract

```shell
cd /mydata/FISCO-BCOS/systemcontractv2
```

> Install dependency

```shell
cnpm install
```

> Configure which node's RPC port to access

```shell
vim config.js
```

> Like (for more: <u>2.5.2 *config.json* Configuration</u> ).

```javascript
var proxy="http://127.0.0.1:8545";
```

### 5.2 Deploy System Contract

> Run *deploy.js* to deploy the system contract. (Note that *deploy.js*  is not the one in the *tool* directory)

```shell
babel-node deploy.js 
```

> If success, get

```log
#.....ignore many compiling warning messages.....
SystemProxycomplie success！
send transaction success: 0x56c9e34cf559b3a9aead8694a7bda7e6b5ea4af855d5ec6ef08fadf494accf08
SystemProxycontract address 0x210a7d467c3c43307f11eda35f387be456334fed
AuthorityFiltercomplie success！
send transaction success: 0x112b6ac9a61197920b6cbe1a71d8f8d4a6c0c11cd0ae3c1107d1626691bf1c35
AuthorityFiltercontract address 0x297e397a7534464a4e7448c224aae52f9614af77
Groupcomplie success！
send transaction success: 0x1be1fb1393e3a3f37f197188ea99de0a5dd1828cc9fc24638f678528f0e30c23
Groupcontract address 0xed0a1b82649bd22d947e5c3ca0b779aac8ee5edc
TransactionFilterChaincomplie success！
send transaction success: 0x704a614b10c5682c44a11e48305bad40a0809d1fc9e178ddec1218c52e7bc9d0
TransactionFilterChaincontract address 0x60d34569bc861b40a7552f89a198a89d8c99075e
CAActioncomplie success！
send transaction success: 0x75f890777b586060c3f94dc3396f5ad86c3e10f2eb9b8350bbc838beecf50ece
CAActioncontract address 0x6fbf3bef2f757c01e0c43fc1364207c1e8a19d08
NodeActioncomplie success！
send transaction success: 0x9d26304258608de5bf1c47ecb9b2ac79f5323e6b74cef7eddef1fb9893d5e98e
NodeActioncontract address 0xb5e0d2c6f1b9f40ea21fa8698a28d1662e8afa3e
send transaction success: 0x03c86b3dcd3d564a00a709b7cd6f1902cd4111cc30c71c62728deadc6e8d7511
ConfigActioncomplie success！
send transaction success: 0xa49205ff3ad697fda75019cb2bbf541a120c146b973f8c5d50b761fd5024b795
ConfigActioncontract address 0xb1e6d5f95c9cb39a9e4e3071b3765e08c30ea281
ConsensusControlMgrcomplie success！
send transaction success: 0xcfe0a0fc77910c127d31470e38707dfe70a7fb699abce3e9261ef55a4e50997c
ConsensusControlMgrcontract address 0x3414ef5c15848a07538a2fac57c09a549036b5e3
ContractAbiMgrcomplie success！
send transaction success: 0xc1e2c4e837edda0e215ca06aaa02eecb3a954acfafd498a049b7cf6cee410f5c
ContractAbiMgrcontract address 0xac919b98301804575bd2dc676330aa8f2637f7d5
#......ignore some output messages...........
send transaction success: 0x2a0f5f9eeb069fe61289e8c95cb4b6cf026859cd20e38e8e47c0788609d8aad1
send transaction success: 0xcd51375a90056e92a52869c63ec153f05722ab8ee56b5ae242b9114c4838e32b
send transaction success: 0x250c0fc5f34bfb73a6bc2a858b64287aa859f12651a3798d46d7269e7305bf6f
send transaction success: 0xff3aeddb55c9ac6868df0cde04466431c7286d93baa80e3826522a2a8ad9681a
send transaction success: 0x71d484aa4a90068e409a11800e9ae5df6143dd59e0cc21a06c1a0bbba4617307
send transaction success: 0x8bd093d44c99817685d21053ed468afa8f216bc12a1c3f5fe33e5cd2bfd045c0
send transaction success: 0x5b9acaab5252bf43b111d24c7ff3adac0121c58e59636f26dbe2ca71dd4af47d
send transaction success: 0x34fb9226604143bec959d65edb0fc4a4c5b1fe5ef6eead679648c7295099ac8b
send transaction success: 0xe9cac7507d94e8759fcc945e6225c873a422a78674a79b24712ad76757472018
register TransactionFilterChain.....
send transaction success: 0x814ef74b4123f416a0b602f25cc4d49e038826cf50a6c4fbc6a402b9a363a8d9
register ConfigAction.....
send transaction success: 0xdb4aaa57b01ca7b1547324bcbeeaaeaa591bf9962ea921d5ce8845b566460776
register NodeAction.....
send transaction success: 0x2c7f43c84e52e99178a92e6a63fb69b5dedf4993f7cbb62608c74b6256241b39
register CAAction.....
send transaction success: 0xcb792f508ca890296055a53c360d68c3c46f8bf801ce73818557e406cbd0618c
register ContractAbiMgr.....
send transaction success: 0xfdc0dd551eada0648919a4c9c5ffa182d042099d73fa802cf803bebf5068aec1
register ConsensusControlMgr.....
send transaction success: 0xbbbf66ab4acd7b5484dce365d927293b43b3904cd14063a7f60839941a0479a0
SystemProxy address :0x9fe9648f723bff29f940b8c18fedcc9c7ed2b91f
-----------------SystemProxy route ----------------------
get 0xb33485375d208a23e897144b6244e20d9c1e83d9
0 )TransactionFilterChain=>0x60d34569bc861b40a7552f89a198a89d8c99075e,false,250
1 )ConfigAction=>0xb1e6d5f95c9cb39a9e4e3071b3765e08c30ea281,false,251
2 )NodeAction=>0xb5e0d2c6f1b9f40ea21fa8698a28d1662e8afa3e,false,252
3 )CAAction=>0x6fbf3bef2f757c01e0c43fc1364207c1e8a19d08,false,253
4 )ContractAbiMgr=>0xac919b98301804575bd2dc676330aa8f2637f7d5,false,254
5 )ConsensusControlMgr=>0x3414ef5c15848a07538a2fac57c09a549036b5e3,false,255
-----------------SystemProxy route ----------------------
```

> The most important output above is the system proxy contract address, like:

```log
SystemProxycontract address 0x210a7d467c3c43307f11eda35f387be456334fed
```

### 5.3 Configure SystemProxy Address to Nodes

System contract include some sub-contracts, system proxy contract is the router to these sub-contracts. Nodes of a chain must configure the same SystemProxy address.

> Modify *config.json* of every node. Configure the field *systemproxyaddress* to the address we get above.

```shell
vim /mydata/nodedata-1/config.json
```

> Like

```log
"systemproxyaddress":"0x919868496524eedc26dbb81915fa1547a20f8998",
```

> Restart configured node

```shell
cd /mydata/nodedata-1/
chmod +x *.sh
./stop.sh
./start.sh #If success, get sealing information constantly.
```

Now the system contract has been deployed. And we are ready for deploying more FISCO-BCOS nodes. For more informations of system contract, please refer to <u>Appendix:11.7 System Contract Instructions</u>



## Chapter 6 Generate More Nodes

Nodes of a blockchain share a the same genesis block file(*genesis.json*) and root certificate key file(*ca.crt*).

The steps to deploy more nodes is the same as deploy a genesis node. And we don't need to modify  *genesis.json* and *ca.crt* .

### 6.1 Setup Node Environment

> Eg: New node's directory is */mydata/nodedata-2/* :

```shell
#Create directory structure
mkdir -p /mydata/nodedata-2/
mkdir -p /mydata/nodedata-2/data/ #store node files
mkdir -p /mydata/nodedata-2/log/ #store logs
mkdir -p /mydata/nodedata-2/keystore/ #store keys

#copy files from the genesis node
cd /mydata/nodedata-1/ 
cp genesis.json config.json log.conf start.sh stop.sh /mydata/nodedata-2/
```

### 6.2  **Generate Node ID**

 Each node has a  node ID, create it before starting up a node.

 You can refer to <u>2.3 Generate Certificates for Nodes</u> to generate certificate for specified node, and copy the generated files to data directory of the node.

```shell
cp /mydata/FISCO-BCOS/cert/WB/nodedata-2/*  /mydata/nodedata-2/data/
```

### 6.3 Configure **bootstrapnodes.json**

There are two steps to configure bootstrapnodes.json:

(1) Copy bootstrapnodes.json from data directory of the genesis node to data directory of current node.

```shell
cp /mydata//nodedata-1/data/bootstrapnodes.json /mydata/nodedata-2/data/
```
(2) Edit bootstrapnodes.json, modify `host` and `p2pport` of bootstrapnodes.json

```shell
vim /mydata//nodedata-2/data/bootstrapnodes.json
```
> Below is a sample of bootstrapnodes.json:
```log
{"nodes":[{"host":"IP of genesis node, eg. 127.0.0.1","p2pport":"30303"}]}
```

### 6.4 Configure Node Configuration File *config.json*

*config.json* describes a node starting up information, includes  IP address, port, data directory, node ID etc. 

```shell
vim /mydata/nodedata-2/config.json
```

> Generally, we need to configure these fields:
>
> - Network setting: listenip, rpcport, p2pport, channelPort
> - Drectory setting: wallet, keystoredir, datadir, logconf
> - Node ID setting: sub fields of NodeextraInfo: Nodeid, Nodedesc, Agencyinfo, Peerip, Identitytype, Port, Idx (should be the same as network setting)
>
> Like:

```log
{
        "sealEngine": "PBFT",
        "systemproxyaddress":"0x210a7d467c3c43307f11eda35f387be456334fed",
        "listenip":"127.0.0.1",
        "cryptomod":"0",
        "rpcport": "8546",
        "p2pport": "30403",
        "channelPort": "30404",
        "wallet":"/mydata/nodedata-2/keys.info",
        "keystoredir":"/mydata/nodedata-2/keystore/",
        "datadir":"/mydata/nodedata-2/data/",
        "vm":"interpreter",
        "networkid":"12345",
        "logverbosity":"4",
        "coverlog":"OFF",
        "eventlog":"ON",
        "statlog":"OFF",
        "logconf":"/mydata/nodedata-2/log.conf",
}
```

Introduction of other fields in *config.json* see: <u>Appendix:12.4 *config.json* Instructions</u>

### 6.5 log.conf configuration

Format and path of logs are configured in *log.conf*.

```shell
vim /mydata/nodedata-2/log.conf 
```

> Generally, we only need to configure the path. Like

```log
* GLOBAL:
    ENABLED                 =   true
    TO_FILE                 =   true
    TO_STANDARD_OUTPUT      =   false
    FORMAT                  =   "%level|%datetime{%Y-%M-%d %H:%m:%s:%g}|%msg"
    FILENAME                =   "/mydata/nodedata-2/log/log_%datetime{%Y%M%d%H}.log"
    MILLISECONDS_WIDTH      =   3
    PERFORMANCE_TRACKING    =   false
    MAX_LOG_FILE_SIZE       =   209715200 ## 200MB - Comment starts with two hashes (##)
    LOG_FLUSH_THRESHOLD     =   100  ## Flush after every 100 logs

* TRACE:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-2/log/trace_log_%datetime{%Y%M%d%H}.log"

* DEBUG:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-2/log/debug_log_%datetime{%Y%M%d%H}.log"

* FATAL:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-2/log/fatal_log_%datetime{%Y%M%d%H}.log"

* ERROR:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-2/log/error_log_%datetime{%Y%M%d%H}.log"

* WARNING:
     ENABLED                 =   true
     FILENAME                =   "/mydata/nodedata-2/log/warn_log_%datetime{%Y%M%d%H}.log"

* INFO:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-2/log/info_log_%datetime{%Y%M%d%H}.log"

* VERBOSE:
    ENABLED                 =   true
    FILENAME                =   "/mydata/nodedata-2/log/verbose_log_%datetime{%Y%M%d%H}.log"
```

Introduction of other fields in *log.conf* see: <u>Appendix:11.5 *log.conf* Instructions</u>

### 6.6 Start Up the Node

Check again that all these files below are correct: 

- Certificate files(/mydata/nodedata-2/data): *ca.crt, server.crt, server.key*
- Node ID files (/mydata/nodedata-2/data): *network.rlp, network.rlp.pub*
- Configuration files(/mydata/nodedata-2/): *genesis.json, config.json, log.conf*

> Note that, after starting up the node, you can only grep the process, but cannot get the sealing information. Because the node has not been registered to blockchain yet. To make the node run correctly, please go to <u>Chapter 6 Register Nodes to FISCO-BCOS Blockchain</u> .

```shell
cd /mydata/nodedata-2/
chmod +x *.sh
./start.sh #The node has not been registered to the blockchain, thus no package info will be seen.  
ctrl-c quit
ps -ef |grep fisco-bcos #You can find the process here
```

> The process is running :

```log
app  9656     1  4 16:10 ?        00:00:01 fisco-bcos --genesis /mydata/nodedata-2/genesis.json --config /mydata/nodedata-2/config.json
```

> Shut down the node, restart it after you finish the registration.

```shell
./stop.sh 
```



## Chapter 7 Register Nodes to FISCO-BCOS Blockchain

Node registration  means that the node is accepted as a member on blockchain by other members. Only the members of blockchain can connect to each other.

> Node registration depends on system contract. Before node registration, make sure:
>
> (1) System contract is deployed correctly.
>
> (2) The *systemproxyaddress* field in *config.json* of every node has been configured with corresponding system proxy contract address.
>
> (3) Nodes have been restarted after configuring *systemproxyaddress* in *config.json*.
>
> (4) *config.js* in the directory */mydata/FISCO-BCOS/systemcontractv2/* has been configured and point to a RPC port of an active node on chain.

### 7.1 Node Registration

Registration procedures are all the same for every nodes. **Register genesis node first, then the other nodes.** 

#### 7.1.1 Registration

Make sure all the registered nodes have been started before registration.

> Register the node with the register configuration file(*node1.json*). (Genesis node should be started before being registered)

```shell
babel-node tool.js NodeAction registerNode node1.json
```

> If success, get

```log
{ HttpProvider: 'http://127.0.0.1:8545',
  Ouputpath: './output/',
  privKey: 'bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd',
  account: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3' }
Soc File :NodeAction
Func :registerNode
SystemProxy address 0x210a7d467c3c43307f11eda35f387be456334fed
node.json=node1.json
NodeAction address 0xcc46c245e6cca918d43bf939bbb10a8c0988548f
send transaction success: 0x9665417c16b636a2a83e13e82d1674e4db72943bae2095cb030773f0a0ba1eef
```

#### 7.1.2 Check node registration

> Check whether the node is already registered in the node list

```shell
babel-node tool.js NodeAction all
```

> If success, get

```log
{ HttpProvider: 'http://127.0.0.1:8545',
  Ouputpath: './output/',
  privKey: 'bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd',
  account: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3' }
Soc File :NodeAction
Func :all
SystemProxy address 0x210a7d467c3c43307f11eda35f387be456334fed
NodeAction address 0xcc46c245e6cca918d43bf939bbb10a8c0988548f
NodeIdsLength= 1
----------node 0---------
id=2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878
ip=127.0.0.1
port=30501
category=1
desc=node1
CAhash=
agencyinfo=node1
blocknumber=427
Idx=0
```

<br>

#### 7.1.3 Register more nodes

Make sure the genesis node is the first registered node before you register more nodes, and all registered nodes are started.

> Repeat the previous steps to register more.

```shell
cd /mydata/FISCO-BCOS/systemcontract/
babel-node tool.js NodeAction register /mydata/nodedata-2/data/node.json
cd /mydata/nodedata-2/
./start.sh #Start the registered node, if success, get sealing info.
```

>Check node registration again

```log
cd /mydata/FISCO-BCOS/systemcontractv2/
babel-node tool.js NodeAction all
```

> If success, get

```log
{ HttpProvider: 'http://127.0.0.1:8545',
  Ouputpath: './output/',
  privKey: 'bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd',
  account: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3' }
Soc File :NodeAction
Func :all
SystemProxy address 0x210a7d467c3c43307f11eda35f387be456334fed
NodeAction address 0xcc46c245e6cca918d43bf939bbb10a8c0988548f
NodeIdsLength= 2
----------node 0---------
id=2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878
ip=127.0.0.1
port=30501
category=1
desc=node1
CAhash=
agencyinfo=node1
blocknumber=427
Idx=0
----------node 1---------
id=838a187e32e72e3889330c2591536d20868f34691f1822fbcd43cb345ef437c7a6568170955802db2bf1ee84271bc9cba64fba87fba84e0dba03e5a05de88a2c
ip=127.0.0.1
port=30502
category=1
desc=node2
CAhash=
agencyinfo=node2
blocknumber=429
Idx=1
```

<br>

### 7.2  Unregister Nodes

> Use *cancel* option of tool.js to unregister a node, taking unregister node2 from block generator nodes as a example:

> attention: /mydata/nodedata-2/data/node.json is the configuration path of node2 used to both register and unregister from block generators.

```shell
cd /mydata/FISCO-BCOS/systemcontract/
babel-node tool.js NodeAction cancel /mydata/nodedata-2/data/node.json
```

> If success

```log
{ HttpProvider: 'http://127.0.0.1:8545',
  Ouputpath: './output/',
  privKey: 'bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd',
  account: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3' }
Soc File :NodeAction
Func :cancelNode
SystemProxy address 0x210a7d467c3c43307f11eda35f387be456334fed
node.json=node2.json
NodeAction address 0xcc46c245e6cca918d43bf939bbb10a8c0988548f
send transaction success: 0x031f29f9fe3b607277d96bcbe6613dd4d2781772ebd0c810a31a8d680c0c49c3
```

> Check node registration, there is no info of the canceled node. The node has quited the network.

```log
babel-node tool.js NodeAction all
#......output message:......
{ HttpProvider: 'http://127.0.0.1:8545',
  Ouputpath: './output/',
  privKey: 'bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd',
  account: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3' }
Soc File :NodeAction
Func :all
SystemProxy address 0x210a7d467c3c43307f11eda35f387be456334fed
NodeAction address 0xcc46c245e6cca918d43bf939bbb10a8c0988548f
NodeIdsLength= 1
----------node 0---------
id=2cd7a7cadf8533e5859e1de0e2ae830017a25c3295fb09bad3fae4cdf2edacc9324a4fd89cfee174b21546f93397e5ee0fb4969ec5eba654dcc9e4b8ae39a878
ip=127.0.0.1
port=30501
category=1
desc=node1
CAhash=
agencyinfo=node1
blocknumber=427
Idx=0
```

<br>

<br>


## Chapter 8 Withdraw Certificates

FISCO-BCOS controls the access mechanism of nodes by certificates. Only authorized nodes with valid certificates can communicate with other nodes. 

FISCO-BCOS provides a tool for administrators of the FISCO-BCOS blockchain, they can either register specified certificates to the **revocation list of certificates** by using **add** option of **CAAction** to prevent specified nodes from accessing the network of blockchain, or remove specified certificates from the **revocation list of certificates** by using **remove** option of **CAAction** to recover the authority of specified nodes to access the network of FISCO-BCOS blockchain.

> Before withdrawing or recovering specified certificates from **revocation list of certificates**, please ensure:
>
> (1) The system contract has been deployed correctly;
>
> (2) The fields *systemproxyaddress* in *config.json* of all nodes have been configured with the right system proxy contract address;
>
> (3) Node has been restarted to enable system contract after (2);
>
> (4) config.js* in */mydata/FISCO-BCOS/systemcontractv2/* has point to an active node's RPC port;
>
> (5) **Node of which certificate to be withdrawed has already been withdrawed from the list of block generators by referring to [section 7.2](#72-unregister-nodes)**


### 8.1 Add Specified Certificate to **Revocation List of Certificates**

The data path of every node contains the **node.ca** file, node.ca saves certificate related information, you can add specified certificate to the **revocation list of certificates** using commands below (here takes adding the certificates of genesis nodes into revocation list of certificates as a example):

```shell
cd /mydata/FISCO-BCOS/systemcontract/
babel-node tool.js  CAAction add /mydata/nodedata-1/data/node.ca
```

<br>

### 8.2 Checking Revocation List of Certificates

> Use commands below to check revocation list of certificates:

```shell
cd /mydata/FISCO-BCOS/systemcontract/
babel-node tool.js CAAction all
```

> The revocation list of certificates is shown as below:

```log
----------CA 0---------
serial=8A4B2CDE94348D22
pubkey=24b98c6532ff05c2e9e637b3362ee4328c228fb4f6262c1c751f51952012cd68da2cbd8655de5072e49b950a503326942297cfaa9ca919b369be4359b4dccd56
name=A
blocknumber=36
```

<br>


### 8.3 Remove Specified Certificate from **Revocation List of Certificates**

You can recover the network access authority of specified nodes whose certificates is among **Revocation List of Certificates** by remove their certificates from the **Revocation List of Certificates** (here takes removing certificate of the genesis node from the **revocation list of certificates** as a example):

```shell
cd /mydata/FISCO-BCOS/systemcontract/
babel-node tool.js  CAAction remove /mydata/nodedata-1/data/node.ca
```

<br>
<br>

## Chapter 9 Console

The console can connect to the node process directly.  When you login the console, you can get many information of the blockchain.

### 9.1 Login

> Connect *geth.ipc* file

```shell
ethconsole /mydata/nodedata-1/data/geth.ipc
```

> If success, get "Connection successful"

```log
Connecting to node at /mydata/nodedata-1/data/geth.ipc
Connection successful.
Current block number: 37
Entering interactive mode.
> 
```

<br>

### 9.2 Use the Console

#### 9.2.1 Get block information

> For example, you can view the info of the block whose block height is 2.
>
> Get block information of height 2

```js
web3.eth.getBlock(2,console.log)
```

> If success

```log
> null { author: '0x0000000000000000000000000000000000000000',
  difficulty: { [String: '1'] s: 1, e: 0, c: [ 1 ] },
  extraData: '0xd78095312e302b2b302d524c696e75782f672b2b2f496e74',
  gasLimit: 2000000000,
  gasUsed: 33245,
  genIndex: '0x0',
  hash: '0xa3c2b1eda74f26c688e78bffcc71c8561e49dc70fbfbd71b85c3b79a2c16bc81',
  logsBloom: '0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000',
  miner: '0x0000000000000000000000000000000000000000',
  number: 2,
  parentHash: '0x96d095047e68d4db8ae0c74638cf6365d5ee7fbc329d50a5a78a85189e1b105e',
  receiptsRoot: '0xb668c383b2ee76f9e2d0a8b0d9b301fb825896a9a597268ca3cbdd979c4a53da',
  sha3Uncles: '0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347',
  size: 71,
  stateRoot: '0x20993fb96c5a26eb9f158c194bcd4da845afde2c2095e359259001279de23ec2',
  timestamp: 1513087469641,
  totalDifficulty: { [String: '2'] s: 1, e: 0, c: [ 2 ] },
  transactions: [ '0x63749a62851b52f9263e3c9a791369c7380acc5a9b6ee55dabd9c1013634e355' ],
  transactionsRoot: '0xc9c0b09f236fba3bad81f6fb92b9ca03b6ccb1aaf53ef600621c0a0fd0fd4bb1',
  uncles: [] }
```

<br>

#### 9.2.2 Get transaction information

> Get transaction information using transaction hash. Eg: get the *HelloWorld* contract we deployed before.

```js
web3.eth.getTransaction('0x63749a62851b52f9263e3c9a791369c7380acc5a9b6ee55dabd9c1013634e355',console.log)
```

> If success 

```log
> null { blockHash: '0xa3c2b1eda74f26c688e78bffcc71c8561e49dc70fbfbd71b85c3b79a2c16bc81',
  blockNumber: 2,
  from: '0x04804c06677d2009e52ca96c825d38056292cab6',
  gas: 30000000,
  gasPrice: { [String: '0'] s: 1, e: 0, c: [ 0 ] },
  hash: '0x63749a62851b52f9263e3c9a791369c7380acc5a9b6ee55dabd9c1013634e355',
  input: '0x4ed3885e0000000000000000000000000000000000000000000000000000000000000020000000000000000000000000000000000000000000000000000000000000000b48656c6c6f576f726c6421000000000000000000000000000000000000000000',
  nonce: 67506452,
  to: '0x1d2047204130de907799adaea85c511c7ce85b6d',
  transactionIndex: 0,
  value: { [String: '0'] s: 1, e: 0, c: [ 0 ] } }
```

<br>

#### 9.2.3 Get transaction receipt

> Get transaction receipt using transaction hash. Eg: get the receipt of *HelloWorld* contract we deployed before.

```js
web3.eth.getTransactionReceipt('0x63749a62851b52f9263e3c9a791369c7380acc5a9b6ee55dabd9c1013634e355',console.log)
```

> You will get the info like:

```log
> null { blockHash: '0xa3c2b1eda74f26c688e78bffcc71c8561e49dc70fbfbd71b85c3b79a2c16bc81',
  blockNumber: 2,
  contractAddress: '0x0000000000000000000000000000000000000000',
  cumulativeGasUsed: 33245,
  gasUsed: 33245,
  logs: [],
  transactionHash: '0x63749a62851b52f9263e3c9a791369c7380acc5a9b6ee55dabd9c1013634e355',
  transactionIndex: 0 }
```

<br>

#### 9.2.4 Get contract code

> Get transaction code using transaction hash. Eg: get the code of *HelloWorld* contract we deployed before.

```js
web3.eth.getCode('0x1d2047204130de907799adaea85c511c7ce85b6d',console.log)
```

> You will get the binary code of the *HelloWorld* contract:

```log
> null '0x60606040526000357c0100000000000000000000000000000000000000000000000000000000900463ffffffff1680634ed3885e146100485780636d4ce63c146100a557600080fd5b341561005357600080fd5b6100a3600480803590602001908201803590602001908080601f01602080910402602001604051908101604052809392919081815260200183838082843782019150505050505091905050610133565b005b34156100b057600080fd5b6100b861014d565b6040518080602001828103825283818151815260200191508051906020019080838360005b838110156100f85780820151818401526020810190506100dd565b50505050905090810190601f1680156101255780820380516001836020036101000a031916815260200191505b509250505060405180910390f35b80600090805190602001906101499291906101f5565b5050565b610155610275565b60008054600181600116156101000203166002900480601f0160208091040260200160405190810160405280929190818152602001828054600181600116156101000203166002900480156101eb5780601f106101c0576101008083540402835291602001916101eb565b820191906000526020600020905b8154815290600101906020018083116101ce57829003601f168201915b5050505050905090565b828054600181600116156101000203166002900490600052602060002090601f016020900481019282601f1061023657805160ff1916838001178555610264565b82800160010185558215610264579182015b82811115610263578251825591602001919060010190610248565b5b5090506102719190610289565b5090565b602060405190810160405280600081525090565b6102ab91905b808211156102a757600081600090555060010161028f565b5090565b905600a165627a7a7230582021681cb0ea5b1c4364a4f5e37f12e84241310a74ee462b1a19658b3acc26c6cb0029'
```

<br>

#### 9.2.5 Get node's peer

> Get the node's peer your console login

```js
//Different commands between different versions of ethconsole
web3.admin.getPeers(console.log)
//Or
web3.admin.peers(console.log)
```

> If success

```log
> null [ { caps: [ 'eth/62', 'eth/63', 'pbft/63' ],
    height: '0x25',
    id: 'b5adf6440bb0fe7c337eccfda9259985ee42c1c94e0d357e813f905b6c0fa2049d45170b78367649dd0b8b5954ee919bf50c1398a373ca777e6329bd0c4b82e8',
    lastPing: 0,
    name: 'eth/v1.0/Linux/g++/Interpreter/RelWithDebInfo/0/',
    network: { remoteAddress: '127.0.0.1:30403' },
    notes: { ask: 'Nothing', manners: 'nice', sync: 'holding & needed' } } ]
```

<br>
<br>


## Chapter 10  Some Tools

### 10.1 Export the Genesis Block

FISCO-BCOS supports exporting all contracts into a genesis block file. So a new blockchain can use the file as the genesis block to createa new blockchain. The new blockchain inherit all the contracts from the old blockchain.

#### 10.1.1 Stop Specified node

> Stop the node you will export

```shell
cd /mydata/nodedata-1  #stop nodedata-1
./stop.sh
```

<br>

#### 10.1.2 Export the genesis block

> Use *--export-genesis* to assign exported genesis file name. 

```shell
fisco-bcos --genesis ./genesis.json --config ./config.json 	--export-genesis ./new_genesis.json
```

> After a while, *new_genesis.json* will be generated. You can use it as the genesis block file of a new chain.

<br>

### 10.2 Peers and Block Height Monitoring

*monitor.js* can monitor peers and block height of a node. Before you run the script, confirm: 

(1) The node you want to monitor has been started.

(2) *config.js* is configured correctly, and the field *proxy* has been pointed to the RPC port you want to monitor.

> Configure and run

```shell
cd /mydata/FISCO-BCOS/tool/
vim config.js
babel-node monitor.js
```

> If success, get peers number and block height constantly.

```log
current blocknumber 429
the number of connected nodes：0
...........Node 0.........
NodeId:838a187e32e72e3889330c2591536d20868f34691f1822fbcd43cb345ef437c7a6568170955802db2bf1ee84271bc9cba64fba87fba84e0dba03e5a05de88a2c
Host:127.0.0.1:30403
--------------------------------------------------------------
current blocknumber 429
the number of connected nodes：0
...........Node 0.........
NodeId:838a187e32e72e3889330c2591536d20868f34691f1822fbcd43cb345ef437c7a6568170955802db2bf1ee84271bc9cba64fba87fba84e0dba03e5a05de88a2c
Host:127.0.0.1:30403
```

<br>
<br>


## Chapter 11 FISOC BCOS Features

Please refer to these documentations:

1. [AMOP(Advance Messages Onchain Protocol)](../amop使用说明文档.md)
2. [Contract_Name_Service](../CNS_Contract_Name_Service_服务使用说明文档.md)
3. EthCall [Design files](../EthCall设计文档.md) [Instructions](../EthCall说明文档.md)
4. [web3sdk](../web3sdk使用说明文档.md)
5. [Meshchain](../并行计算使用说明文档.md) 
6. [Distributed file system](../分布式文件系统使用说明.md)
7. [Monitoring logs](../监控统计日志说明文档.md)
8. [Homomorphic encryption;](../同态加密说明文档.md)
9. [Certificate Authority](../CA机构身份认证说明文档.md)
10. [Regulatory Zero-Knowledge Proof](../可监管的零知识证明说明.md)
11. [Group Signature and Ring Signature Support](../启用_关闭群签名环签名ethcall.md)
12. [Architecture of Elastic Aliance Consensus](../弹性联盟链共识框架说明文档.md)

<br>
<br>


## Chapter 12 Appendix

### 12.1 Sourece Code Structure

| Source Code Directories | Illustration                             |
| ----------------------- | ---------------------------------------- |
| abi                     | Implementation of CNS(Contract Name Service) |
| eth                     | Main entry of FISCO BCOS, *main.cpp* is included in this directory |
| libchannelserver        | AMOP(Advance Messages Onchain Protocol)  implementation |
| libdevcore              | Implement core and general components of FISCO BCOS, such as I/O operations, read/write locks, TrieDB, SHA3, RLP, Worker Model, etc |
| libdiskencryption       | Disk storage encryption implementation   |
| libethcore              | Implement core data structures of blockchain, including ABI, block header, transaction, etc. Provide key management and pre-compile functions |
| libethereum             | Implement main frameworks of blockchain, including transaction pool, system contract, node management, block, blockchain parameters, etc. |
| libevm                  | Virtual machine, including interpreter, JIT, etc. |
| libevmcore              | OPCODE instruction set                   |
| libp2p                  | P2P network, including handshake, packet signature and verification, session management, etc. |
| libpaillier             | Homomorphic encryption                   |
| libpbftseal             | PBFT consensus plug-in                   |
| libraftseal             | RAFT consensus plug-in                   |
| libstatistics           | Statistics of web3j rpc access frequencyRequest frequency counting and controlling. |
| libweb3jsonrpc          | Web3 RPC                                 |
| sample                  | One-button installation and deployment.  |
| scripts                 | Scripts for installation and deployment. |
| systemproxy             | System contract                          |


<br>


### 12.2 cryptomod.json

FISCO BCOS supports encrypted communications. We can configure the way of encrypted communications in *cryptomod.json*.

| Items             | Illustration                             |
| ----------------- | ---------------------------------------- |
| cryptomod         | The way that NodeId is generated, default is 0. |
| rlpcreatepath     | The path that NodeId is generated. The file name must be *network.rlp* ,like: /mydata/nodedata-1/data/network.rlp. |
| datakeycreatepath | The path that datakey is generated, keep it empty. |
| keycenterurl      | Remote encryption service, keep it empty. |
| superkey          | Key for the local encryption service, keep it empty |

<br>


### 12.3 genesis.json Instructions

Instructions for genesis block file *genesis.json*:

| Items          | Illustration                             |
| -------------- | ---------------------------------------- |
| timestamp      | The timestamp of the genesis block (ms)  |
| god            | Built-in admin account address(Input the address you generate from <u>2.2 Generate God Account</u> ) |
| alloc          | Built-in contract data                   |
| initMinerNodes | The node ID of the genesis node(input the nodeid you get from <u>2.3 Generate Genesis Node ID</u>) |

<br>


### 12.4 config.json Instructions

Instructions for node configurations fiile *config.json*:

| Items              | Illustration                             |
| ------------------ | ---------------------------------------- |
| sealEngine         | Consensus plug-in name(You can choose from PBFT, RAFT and SinglePoint) |
| systemproxyaddress | Address of the system proxy contract address(refer to <u>Chapter 4 Deploy System Contracts</u>) |
| listenip           | Node listen IP                           |
| cryptomod          | Encrypt mode, default is 0.( keep it in accordance with the field *cryptomod* in *cryptomod.json*) |
| rpcport            | RPC listening port(Ports shall not be the same when you have more than 1 nodes in a single server) |
| p2pport            | P2P network listening port(Ports shall not be the same when you have more than 1 nodes in a single server) |
| channelPort        | AMOP listening port(Ports shall not be the same when you have more than 1 nodes in a single server) |
| wallet             | path of the wallet file                  |
| keystoredir        | path of the account file directory       |
| datadir            | path of the node data directory          |
| vm                 | vm engine (default is  interpreter)      |
| networkid          | network ID                               |
| logverbosity       | Verbosity of the logs (the higher you set, the more detailed logs you will get. >8 TRACE logs,4<=x<8 DEBUG logs,<4 INFO logs) |
| coverlog           | Switch for the Coverlog (ON or OFF)      |
| eventlog           | Switch for the Eventlog (ON or OFF)      |
| statlog            | Switch for the Statlog (ON or OFF)       |
| logconf            | path of the log configuration file(refer to the instructions for *log.conf* ) |

<br>


### 12.5 log.conf Instructions

Instructions for log configurations file *log.conf*:

| Items               | Illustration                             |
| ------------------- | ---------------------------------------- |
| FORMAT              | Log format, such as *%level*             |
| FILENAME            | such as */mydata/nodedata-1/log/log_%datetime{%Y%M%d%H}.log* |
| MAX_LOG_FILE_SIZE   | Max size of the log file                 |
| LOG_FLUSH_THRESHOLD | Threshold for log flushed into disk storage |

<br>


### 12.6 System Contract

System contract is the main feature of FISCO-BCOS. It is a group of built-in smart contracts. System contracts reach consensus by all members to control the running strategy of the chain.

System contracts include: system proxy contract, node management contract, institution certificate contract, authorization management contract, network configuration contract, etc.

Once deployed and enable, generally, a system contract will not be changed.

If you have to change or update a system contract, first you need to get  approval from all the nodes in the network, then update the *systemproxyaddress* field in the config file, and restart.

In principle, system contracts can only be called by the administrator account.

Here are some instructions for system contracts' source code path, API, samples and tools.

#### 12.6.1 System proxy contract

System proxy contract is the router for system contracts. 

It provides a map to get other system contracts' address according to system contract name.

Path of the source code: systemcontractv2/SystemProxy.sol

**(1) APIs**

| API Name | Input                                    | Output                                   | Notes                                    |
| -------- | ---------------------------------------- | ---------------------------------------- | ---------------------------------------- |
| getRoute | contract name                            | contract address, cache flag, enable block number |                                          |
| setRoute | contract name, contract address, cache flag | null                                     | If the contract name already exists, it will be replaced. |

Samples for web3(refer to systemcontractv2/deploy.js):
```js
 console.log("register NodeAction.....");
 func = "setRoute(string,address,bool)";
 params = ["NodeAction", NodeAction.address, false];
 receipt = await web3sync.sendRawTransaction(config.account, config.privKey, SystemProxy.address, func, params);
```

**(2) Tools**

View info of all the system contracts:

```shell
babel-node tool.js SystemProxy
```


Sample for the output:

```log
{ HttpProvider: 'http://127.0.0.1:8701',
  Ouputpath: './output/',
  privKey: 'bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd',
  account: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3' }
Soc File :SystemProxy
Func :undefined
SystemProxy address 0x210a7d467c3c43307f11eda35f387be456334fed
-----------------SystemProxy route----------------------
0 )TransactionFilterChain=>0xea8425697a093606309eb85e4447d6f333cff2fe,false,395
1 )ConfigAction=>0x09e4f1b4fa1713339f5aa17b40fa6f9920c7b278,false,396
2 )NodeAction=>0xcc46c245e6cca918d43bf939bbb10a8c0988548f,false,397
3 )CAAction=>0x8ab1175c6e7edb40dd0ed2a52ceaa94afb135a64,false,398
4 )ContractAbiMgr=>0x707024221d2433067b768c4be3a005c5ece8df40,false,399
5 )ConsensusControlMgr=>0x007f2c2751bbcd6c9a630945a87a3bc2af38788c,false,400
```

Now you can get all the route info in the system route list.

<br>


#### 12.6.2 Node management Contract

Node  management contract maintains the node list on blockchain. 

It provides the node register/unregister operation.

Path of the source code : systemcontractv2/NodeAction.sol

**(1)APIs**

| API Name     | Input                                    | Output  | Notes                                    |
| ------------ | ---------------------------------------- | ------- | ---------------------------------------- |
| registerNode | NodeID, IP, port, identitytype, nodedesc, node CA hash, node agency, node number | Boolean | If the NodeID already exists, the request will be ignored. |
| cancelNode   | NodeID                                   | Boolean | If the route name does not exist, the request will be ignored. |

Samples for web3(refer to systemcontractv2/tool.js):
```js
var instance=getAction("NodeAction");
var func = "registerNode(string,string,uint256,uint8,string,string,string,uint256)";
var params = [node.id,node.ip,node.port,node.category,node.desc,node.CAhash,node.agencyinfo,node.idx];
var receipt = web3sync.sendRawTransaction(config.account, config.privKey, instance.address, func, params);
```

**(2)Tools**

Please refer to *6.1 Node Registration[61-node-registration]*


<br>


#### 12.6.3 Institution certificate contract (CAAction contract)

Institution certificate contract maintains the institution certificate information.

Path of the source code: systemcontractv2/CAAction.sol

**(1)APIs**

| API Name     | Input                                    | Output                                   | Notes                                    |
| ------------ | ---------------------------------------- | ---------------------------------------- | ---------------------------------------- |
| update       | certificate hash, public key, org name, certificate start time, certificate end time, certificate status, IP white list, IP black list | Boolean                                  | If the certificate does not exist, it will be created. |
| updateStatus | certificate hash, certificate status     | Boolean                                  | If the route name does not exist, request ignored |
| get          | certificate hash                         | certificate hash, public key, org name, certificate start time, certificate end time, certificate status, blockID |                                          |
| getIp        | certificate hash                         | IP white list, IP black list             |                                          |

Samples for web3(refer to systemcontractv2/tool.js):
```js
 var instance=getAction("CAAction");
 var func = "update(string,string,string,uint256,uint256,uint8,string,string)";
 var params = [ca.hash,ca.pubkey,ca.orgname,ca.notbefore,ca.notafter,ca.status,ca.whitelist,ca.blacklist];
 var receipt = web3sync.sendRawTransaction(config.account, config.privKey, instance.address, func, params);
```

**(2)Tools**

View the certificate list

```shell
babel-node tool.js CAAction all
```

Update certificate

```shell
babel-node tool.js CAAction update
```

Update certificate status

```shell
babel-node tool.js CAAction updateStatus
```


<br>


#### 12.6.4 Authorization management contract

The authorization management contract fulfills FISCO-BCOS blockchain authority model.

An outer-account should only be owned by one role, and a role owns one authorization list.

An authorization is exclusively identified by a contract address and a contract interface. 

Path of the source code :systemcontractv2/AuthorityFilter.sol	

Transaction authorization filter: systemcontractv2/Group.sol

**(1) APIs**

| Contract                     | API Name      | Input                                    | Output         | Notes |
| ---------------------------- | ------------- | ---------------------------------------- | -------------- | ----- |
| Role                         | setPermission | Contract address, contract interface, authority flag |                |       |
|                              | getPermission | Contract address, contract interface     | authority flag |       |
| Transaction Authority Filter | setUserGroup  | outer-account, role contract             |                |       |
|                              | process       | outer-account, transaction from account, contract address, contract interface, transaction input data |                |       |

Samples for web3(refer to systemcontractv2/deploy.js):
```js
var GroupReicpt= await web3sync.rawDeploy(config.account, config.privKey, "Group");
var Group=web3.eth.contract(getAbi("Group")).at(GroupReicpt.contractAddress);
#......ignore some codes......
abi = getAbi0("Group");
params  = ["Group","Group","",abi,GroupReicpt.contractAddress];
receipt = await web3sync.sendRawTransaction(config.account, config.privKey, ContractAbiMgrReicpt.contractAddre
ss, func, params);
```

**(2) Tools**

Check the authority of the outer-account

```shell
babel-node tool.js AuthorityFilter outer-account,contract address, contract interface
```

**(3) Customization**

You can inherit *TransactionFilterBase* to write a new transaction filter contract, and register the new filter into *TransactionFilterChain* via the *addFilter* interface.

<br>

#### 12.6.5 Network configuration contract

Network configuration contract maintains some configuration of the running network.

All members reach consensus of network configuration contract, making that all configuration of members are the same.

Path of the source code: systemcontractv2/ConfigAction.sol

**(1) Network config illustration**

| items                | illustration                             | default   | recommended |
| -------------------- | ---------------------------------------- | --------- | ----------- |
| maxBlockHeadGas      | Max block gas                       | 2,000,000,000 | 2,000,000,000 |
| intervalBlockTime    | Block interval (ms)                 | 1000      | 1000        |
| maxBlockTransactions | Max block transactions              | 1000      | 1000        |
| maxNonceCheckBlock   | Range of the max block when nonce checking | 1000      | 1000        |
| maxBlockLimit        | Allowed blockLimit that exceeds the block number right now  | 1000      | 1000        |
| maxTransactionGas    | Max transaction gas                 | 30,000,000  | 30,000,000    |
| CAVerify             | Institution certificate authority verify switch | false     | false       |

**(2) APIs**

| API Name | Input                | Output                 | Notes                           |
| -------- | -------------------- | ---------------------- | ------------------------------- |
| set      | config, config value | null                   | If existed, it will be replaced |
| get      | config               | config value, block ID |                                 |

Samples for web3(refer to systemcontractv2/tool.js):
```js
var func = "set(string,string)";
var params = [key,value];
var receipt = web3sync.sendRawTransaction(config.account, config.privKey, instance.address, func, params);
console.log("config :"+key+","+value);
```

**(3) Instructions**

View the config

```shell
babel-node tool.js ConfigAction get config
```

Set the config

```shell
babel-node tool.js ConfigAction set config config value
```

<br>
<br>

## Chapter 12 Frequently Asked Questions

### 1. Invalid Format of Shell Scripts

** Format differences of shell scripts between windows platform and linux platform will lead to belowing error when executing shell scripts such as install_deps.sh, build.sh, etc. :**

``` log
xxxxx.sh: line x： $'\r':command not found
xxxxx.sh: line x： $'\r':command not found
xxxxx.sh: line x： $'\r':command not found
xxxxx.sh: line x： $'\r':command not found
```

**Dos2unix tool of linux can solve this format error by converting format of shell script from windows to linux: **

``` shell
sudo yum -y install dos2unix
dos2unix xxxxx.sh
```


