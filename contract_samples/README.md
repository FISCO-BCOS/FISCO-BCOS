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
* BCOS区块链客户端     （参考[BCOS使用文档]）
* JDK
* **fisco-solc** FISCO-BCOS的solidity编译器
    * 直接使用FISCO-BCOS的合约编译器，将根目录下的fisco-solc放到系统/usr/bin目录下
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
HttpService httpService = new HttpService(uri); //uri="http://<ip:port>"
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
BCOS参考中直接提供eclipse [sample完整工程](https://github.com/bcosorg/samples/tree/master/projects/java/sample) ， 可以直接导入工程。
sample工程包括：
 1. lib文件夹， 存放所有依赖开发库；
 2. res文件夹，存放测试钱包文件和配置文件
     * wallet.json，BCOS客户端钱文件（和以太坊钱包文件一样）
     * config.json，工程运行配置文件，详细说明参考readme.txt
3. bin文件夹， 程序运行目录
导入工程后，配置好config.json对应rpc_host和rpc_port即可进行SimpleStorage合约的调测

## Nodejs智能合约客户端开发

### 开发环境及工具
参考Java智能合约客户端开发，需要部署BCOS区块链客户端、安装solidity编译器，此外还需要安装Nodejs。

### 构建客户端程序

#### 编写Nodejs调用代码
1. web3初始化
```
var Web3 = require('web3');
……
var web3sync = require('./web3sync');
……

if (typeof web3 !== 'undefined') {
    web3 = new Web3(web3.currentProvider);
} else {
    web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

```
2. 编译合约

调用fisco-solc编译本地程序，编译合约文件SimpleStartDemo.sol
```
var filename = "SimpleStartDemo";
console.log('Soc File :' + filename);

try {
    //用FISCO-BCOS的合约编译器fisco-solc进行编译
    execSync("fisco-solc --abi  --bin   --overwrite -o " + config.Ouputpath + "  " + filename + ".sol");

    console.log(filename + '编译成功！');
} catch (e) {

    console.log(filename + '编译失败!' + e);
}

```

3. 部署合约

部署完毕后，将合约的address和abi写入output路径下的文件

```
    var Contract = await web3sync.rawDeploy(config.account, config.privKey, filename);
    console.log(filename + "部署成功！")
```

4. 读取合约保存的初始值

通过读取output路径的文件，获取到合约的address和abi，构建合约实例对象
```
    var address = fs.readFileSync(config.Ouputpath + filename + '.address', 'utf-8');
    var abi = JSON.parse(fs.readFileSync(config.Ouputpath /*+filename+".sol:"*/ + filename + '.abi', 'utf-8'));
    var contract = web3.eth.contract(abi);
    var instance = contract.at(address);
    console.log("读取" + filename + "合约address:" + address);
    var data = instance.getData();
    console.log("接口调用前读取接口返回:" + data);
```

5. 调用合约，修改合约保存的值

调用set接口，修改值为10，并且打印出交易哈希

```
    var func = "setData(int256)";
    var params = [10];
    var receipt = await web3sync.sendRawTransaction(config.account, config.privKey, address, func, params);

    console.log("调用更新接口设置data=10" + '(交易哈希：' + receipt.transactionHash + ')');
```
6. 再次读取合约存储的值，验证合约调用是否成功


```
data = instance.getData();
    console.log("接口调用后读取接口返回:" + data);
```


#### startDemo工程使用说明

安装好依赖的开发环境和工具，执行如下命令

``` 
 $ cd startDemo
 $ npm install
 $ babel-node index.js
```
输出结果

```
[root@VM_191_76_centos startDemo]# babel-node  index.js
RPC=http://127.0.0.1:8545
Ouputpath=./output/
 ........................Start........................
Soc File :SimpleStartDemo
SimpleStartDemo编译成功！
SimpleStartDemo合约地址 0x36054aa9ddfd684d47d14eaf17c8d0a1a22fde46
SimpleStartDemo部署成功！
读取SimpleStartDemo合约address:0x36054aa9ddfd684d47d14eaf17c8d0a1a22fde46
接口调用前读取接口返回:5
code_fun :  0xda358a3c
txData_code :  0xda358a3c000000000000000000000000000000000000000000000000000000000000000a
调用更新接口设置data=10(交易哈希：0x1cd8323fcf93d431657a04e292c0c9aebf3019ed4d5b3efd6ca8cd9ce75741ae)
接口调用后读取接口返回:10
==============================End============================================
AddMsg: [in the set() method] from
```
*********************************************************
[web3j开发库]:https://github.com/bcosorg/bcos/blob/master/tool/java/output/
[第三方依赖库]:https://github.com/bcosorg/bcos/blob/master/tool/java/third-part/
[BCOS使用文档]:https://github.com/bcosorg/bcos/blob/master/doc/manual/manual.md
[BCOS工具包]:https://github.com/bcosorg/samples/blob/master/dependencies/compile/BCOS_Tools.tar.gz
[web3j]:https://docs.web3j.io/
