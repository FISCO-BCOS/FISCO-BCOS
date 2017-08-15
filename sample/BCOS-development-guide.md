[TOC]
# BCOS智能合约客户端开发指导

## 概要
本文档主要提供基于BOSC区块链系统的智能合约应用开发指导，使得开发者能够使用流行的编程语言如Java、Nodejs等进行应用开发。 本文档中包含API介绍及其源码工程， 它们通过简单的范例介绍开发智能合约的方法， 帮助开发者快速入门区块链应用开发。

## 运行环境
* 操作系统：Ubuntu （建议16.04） 或 CentOS （建议7.0）
* BCOS区块链客户端

## 智能合约
智能合约语法及细节参考 **[solidity官方文档](https://solidity.readthedocs.io/en/develop/solidity-in-depth.html)** ，  **BCOS区块链客户端**建议使用solidity编译器版本指定为0.4.2及以上， 合约编写可以使用任何文本编辑器(推荐使用sublime或vs code+solidity插件)。

### 合约范例
这里例举一个简单的solidity智能合约，仅仅提供数据设置set和读取get方法，其中set为交易类型（Transaction）方法，get为call类型方法。在本文档中将使用这个范例作为唯一合约，展示如何构建操作SimpleStorage智能合约的应用程序。范例如下：
```
pragma solidity ^0.4.2;

contract SimpleStorage {
    uint storedData;

    function SimpleStorage() {
        storedData = 5;
    }

    function set(uint x) public {
        storedData = x;
    }

    function get() constant public returns (uint _ret) {
        _ret = storedData;
    }
}
```

### 合约编译
1. 首先下载[BCOS工具包]。
2. 解压[BCOS工具包]，比如解压目录： /home/xxx/BCOS-Tools
命令：tar xvzf web3j-2.1.0.tar.gz

其目录如下：  
```
    ├── bin

    │   ├── compile.sh

    │   ├── web3j

    │   └── web3j.bat

    ├── contracts

    │   └── SimpleStorage.sol

    ├── lib

    │   ├── asm-5.0.3.jar

    │   ├── asm-analysis-5.0.3.jar

    │   ├── asm-commons-5.0.3.jar

    │   ├── asm-tree-5.0.3.jar

    │   ├── asm-util-5.0.3.jar

    │   ├── bcprov-jdk15on-1.54.jar

    │   ├── commons-codec-1.9.jar

    │   ├── commons-logging-1.2.jar

    │   ├── core-2.1.0.jar

    │   ├── httpclient-4.5.2.jar

    │   ├── httpcore-4.4.4.jar

    │   ├── jackson-annotations-2.8.0.jar

    │   ├── jackson-core-2.8.5.jar

    │   ├── jackson-databind-2.8.5.jar

    │   ├── javapoet-1.7.0.jar

    │   ├── jffi-1.2.14.jar

    │   ├── jffi-1.2.14-native.jar

    │   ├── jnr-constants-0.9.6.jar

    │   ├── jnr-enxio-0.14.jar

    │   ├── jnr-ffi-2.1.2.jar

    │   ├── jnr-posix-3.0.33.jar

    │   ├── jnr-unixsocket-0.15.jar

    │   ├── jnr-x86asm-1.0.2.jar

    │   ├── rxjava-1.2.4.jar

    │   └── scrypt-1.4.0.jar

    └── output
```
目录说明： contracts为需要编译的合约目录，bin为编译执行目录，lib为依赖库，output为编译后输出的abi、bin及java文件目录

 3. 拷贝编译合约
 
切换到/home/xxx/BCOS-Tools/bin目录，拷贝需要编译的智能合约到/home/xxx/BCOS-Tools/contracts目录

执行命令：cd /home/xxx/BCOS-Tools/bin  以及拷贝需要编译的智能合约到/home/xxx/BCOS-Tools/contracts目录

 4. 智能合约编译及Java Wrap代码生成

    执行命令：sh compile.sh 【参数1：Java包名】
    执行成功后将在output目录生成所有合约对应abi，bin，java文件，其文件名类似： **合约名字.[abi|bin|java]** 。至此编译完成。
    
## Java智能合约客户端开发

### 开发环境及工具
* BCOS区块链客户端	 （参考[BCOS使用文档]）
* JDK
* **solc** solidity编译器（直接使用远程二机制源或源码安装）
	* Ubuntu安装
		```apt install solc ``` 或 源码安装
	* CentOS安装
		```yum install solc``` 或 源码安装
* Java [web3j开发库]及其依赖库

### web3j介绍
**[web3j]** 是一个支持以太坊通信协议的客户端网络库，它提供了轻量、反应式、类型安全的Java和Android程序访问能力，使得开发以太坊应用更加便捷，其详细的教程可以参考[官方网站](https://docs.web3j.io/) 。在本范例中sample程序将使用 **[web3j]** 及相关库来开发Java应用程序，基于web3j的应用开发的流程为：
1. 使用智能合约的abi和bin文件生成智能合约Java Wrapper类， 参考BCOS-Tools工具包的使用;
2.  初始化web3j远程调用对象；
3.  使用智能合约Java Wrapper类提供的load或deploy接口获取智能合约远程调用对象；（其依赖于：web3j远程调用对象）
4.  调用智能合约远程调用对象访问合约，发送交易或call查询。

### 构建客户端程序
#### 创建Java工程
此处以Eclipse为例
1. 新建Java工程；
2. 添加web3j及其拓展依赖库， BCOS仓库的tool/java/output/下的web3j-2.1.0_main.jar、web3j-2.1.0-extension.jar，tool/java/third-jars/下所有jar文件；
3. 拷贝编译生成的java文件到工程， 如：SimpleStorage.java ；
#### 编写Java调用代码
编写区块链应用程序，实际上是通过web3j生成的Java Wrapper类，通过JsonRPC调用和BCOS客户端节点通信，再由客户端返回JsonRPC请求响应。对合约的调用主要包括：web3j的初始，合约对象部署，合约对象加载，合约对象发送交易，合约对象call调用等
1. web3j初始化
```
HttpService httpService = new HttpService(uri);//uri="http://10.10.8.219:8545"
Parity web3j = Parity.build(httpService);
```
2. 部署合约

使用初始化的web3j对象和BcosRawTxManager交易管理器来部署智能合约，如果部署成功， Future对象即会返回合约调用对象
```
Future<SimpleStorage> futureSimpleStorage = SimpleStorage.deploy(web3j, new BcosRawTxManager(web3j, credentials, 100, 100), gasPrice, gasLimited, new BigInteger("0"));
SimpleStorage simpleStorage = futureSimpleStorage.get();
String deployAddr = simpleStorage.getContractAddress();
```
3. 载入已经部署的合约

合约部署成功后，可以获取到已经部署的合约地址：``` String deployAddr = simpleStorage.getContractAddress(); ```
利用获取到部署合约地址，初始化的web3j对象和BcosRawTxManager交易管理器，可以载入智能合约调用对象
```
SimpleStorage simpleStorage = SimpleStorage.load(deployAddr.toString(), web3j, new BcosRawTxManager(web3j, credentials, 100, 100), gasPrice, gasLimited);
```
注意： 部署后可以直接使用返回的智能合约对象， 而无需再load载入！

4. 发送交易

发送交易通过直接调用已经部署或载入的智能合约调用对象执行合约对应接口即可， 比如智能合约SimpleStorage.sol的set方法，对应SimpleStorage.java的set方法，名字是相同的。 交易执行成功后将返回Receipt，Receipt包含交易hash和其他信息（如log）
```
TransactionReceipt receipt = null;
receipt = simpleStorage.set(new Uint256(new BigInteger("1000"))).get();
```
5. call查询

使用已经部署或载入的智能合约调用对象，直接执行对应智能合约中带constant修饰符的函数，即执行call类型调用
```
Uint256 value = simpleStorage.get().get();
```

#### sample工程使用说明
BCOS参考中直接提供eclipse[sample完整工程](https://github.com/bcosorg/bcos/blob/master/sample/projects/java/sample) ， 可以直接导入工程。
sample工程包括：
 1. lib文件夹， 存放所有依赖开发库；
 2. res文件夹，存放测试钱包文件和配置文件
	 * wallet.json，BCOS客户端钱文件（和以太坊钱包文件一样）
	 * config.json，工程运行配置文件，详细说明参考readme.txt
3. bin文件夹， 程序运行目录
导入工程后，配置好config.json对应rpc_host和rpc_port即可进行SimpleStorage合约的调测

## Nodejs智能合约客户端开发

*********************************************************
[web3j开发库]:https://github.com/bcosorg/bcos/blob/master/tool/java/output/
[第三方依赖库]:https://github.com/bcosorg/bcos/blob/master/tool/java/third-part/
[BCOS使用文档]:https://github.com/bcosorg/bcos/blob/master/doc/manual/manual.md
[BCOS工具包]:https://github.com/bcosorg/bcos/blob/master/sample/dependencies/BCOS-Tools.tar.gz
[web3j]:https://docs.web3j.io/