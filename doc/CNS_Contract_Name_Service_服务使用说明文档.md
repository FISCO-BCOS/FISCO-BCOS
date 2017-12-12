[toc]  

# CNS(Contract Name Service)服务
## 一.概述
在一般的业务流程中,如果调用方需要调用合约的接口时,需要将部署的合约的地址信息,调用的合约的接口信息保存下来,调用时将这些信息下发给eth的客户端(一般是js客户端web3j或者是java客户端),结合合约编译生成的abi信息,调用对应合约的接口。这种情况下,合约每次重新部署时调用合约的地址都会发生变化,因此调用方都需要更新合约地址信息,适用上非常不方便。  
CNS提供一种由name到调用合约接口的映射关系,调用时只需要传入调用接口的name映射以及参数信息,由底层服务维护合约的地址,abi信息,然后根据传入的name以及参数信息,查找对应的合约的地址和abi,由abi以及入参序列化执行合约接口需要的代码,传给evm进行调用。这样底层以外的模块将不再需要关心合约的地址合约的地址与abi信息,只需要知道合约的接口映射的name既可,在合约发生变动,重新部署,只要需要调用的合约的接口不会发生变化,映射关系即保持不变,传入的name也不变。  
下面给出一个图示来说明下CNS服务的优势：
![](CNS服务.png)

1. 调用方式更简单,只需要传入调用合约的名称,接口,参数信息。  
2. 由底层的Contract Manager对需要调用的合约进行版本管理,方便在各个版本之间相互切换。
3. 接口不改变的情况下合约的升级对于调用者透明,提供灰度升级的可能性。

## 二.实现
### 1. 主要模块简介  
1. contract合约信息缓存模块：用来缓存部署合约的address,abi等信息,维护合约的name到合约信息的映射关系,目前的映射关系是在一个名为ContractAbiMgr的合约中进行维护,在该合约的内容发生变动时,会将变动的内容同步到服务的本地内存中。  
> 注意：ContractAbiMgr合约本省需要在系统合约中维护,所以需要使用CNS服务时需要首先部署系统合约。
2. 辅助工具：用来将需要使用CNS调用方式的合约注册到ContractAbiMgr中。
3. abi序列化模块：  
   - a. 根据合约的abi以及调用传入的参数序列化evm调用需要的机器码。  
   - b. 根据合约的abi以及evm的返回,将evm返回的机器码反序列化调用方能够识别的数据格式,目前是以json数组的形式进行返回。

### 2. 实现  
#### a. 管理合约ContractAbiMgr  
源码位于systemcontractv2/ContractAbiMgr.sol  
维护的合约的结构体名称为struct AbiInfo,主要内容有：  
- string  contractname  //合约名称
- string  version       //合约版本号
- string  abi           //合约编译生成的abi信息
- address addr          //合约的地址  

维护合约信息的结构体为 ：   
mapping(string=>AbiInfo) map_abi_infos //其中mapping的key为 contractname + "/" + version

主要接口有：  
- addAbi(string,string,string,string,address)   //添加一个合约的信息,如果新添加的合约对应版本的信息已经存在,则失败返回。
- updateAbi(string,string,string,string,address)  //更新一个合约的信息,如果需要更新的合约的对应版本的信息不存在,则失败返回。   


辅助类： ContractBase.sol(位于tool/ContractBase.sol)  
- 如果需要对部署的合约进行版本管理,可以让合约继承ContractBase.sol,在构造函数中调用ContractBase.sol的构造函数初始化version成员。

ContractAbiMgr合约在systemcontract中进行维护,在系统合约部署时被部署。  
在部署的同时,ContractAbiMgr会将自己的信息添加到管理中,这样在新增合约的信息需要被ContractAbiMgr管理时,可以同样以CNS的方式调用ContractAbiMgr的接口。(ContractAbiMgr详细部署的流程可以参考systemcontractv2/deploy.js,CNS方式调用ContractAbiMgr参考tool/abi_name_service_tool.js源码)。  



#### b. 辅助工具  
将需要以CNS方式调用的合约的信息注册到ContractAbiMgr中。  

    abi_name_service_tool.js Usage: 
         babel-node abi_name_service_tool.js get    contract-name [contract-version] //获取合约对应版本的信息。
         babel-node abi_name_service_tool.js add    contract-name //将合约的信息添加到ContractAbiMgr中。
         babel-node abi_name_service_tool.js update contract-name //更新合约的信息。
         babel-node abi_name_service_tool.js list  //列出ContractAbiMgr中已经维护的合约的信息。

具体使用参考下面的使用案例。  
#### c. abi序列化模块  
abi的序列化跟反序列化的规则详情参考 ： [https://github.com/ethereum/wiki/wiki/Ethereum-Contract-ABI](https://github.com/ethereum/wiki/wiki/Ethereum-Contract-ABI)  

### 3. 底层接口修改  

底层rpc接口的修改,支持CNS方式的调用：
> 注意：只是修改了rpc的接口,原来的请求方式仍然兼容。  
rpc的格式详情参考：https://github.com/ethereum/wiki/wiki/JSON-RPC
- eth_call  

```请求：
{
  "jsonrpc": "2.0",
  "method": "eth_call",
  "params": [
    {
      "data": {
        "contract": "",   //调用合约名称
        "version": "",    //调用合约的版本号
        "func": "",       //调用合约的接口
        "params": [       //参数信息

        ]
      }
    },
    "latest"
  ],
  "id": 1
}

返回：
{
  "id": 1,
  "jsonrpc": "2.0",
  "result": {
    "result": [],         //返回结果,格式为json数组
    "ret_code": 0,
    "ret_msg": "success!"
  }
}

```

eth_call的修改,是复用之前eth_call接口的data字段,在data字段为json对象并且能够解析到CNS需要的字段时认为是CNS的调用,否则认为是一般的调用。

- eth_sendRawTransaction
rpc请求以及返回格式跟之前完全相同,不同为之前rlp编码data字段为十六进制字符串,现在data的值改为：

```
"data": {
        "contract": "",   //调用合约名称
        "version": "",    //调用合约的版本号
        "func": "",       //调用合约的接口
        "params": [       //参数信息
        
        ]
      }
```

新添加的接口,方便调试使用：
- eth_jsonCall
```
rpc格式 ：
请求：
{
  "jsonrpc": "2.0",
  "method": "eth_jsonCall",
  "params": [
    {
      "contract": "",     //调用合约名称
      "version": "",      //调用合约的版本号
      "func": "",         //调用合约的接口
      "params": [         //参数信息
      ]
    }
  ],
  "id": 1
}
返回：
{
  "id": 1,
  "jsonrpc": "2.0",
  "result": {
    "result": [],         //返回结果,格式为json数组
    "ret_code": 0,
    "ret_msg": "success!"
  }
}
```

目前在js的web3j客户端中已经对调用做了封装：  
接口路径：tool/web3sync.js  
callByNameService  
sendRawTransactionByNameService

## 三.使用  
本模块提供一个完整的CNS调用方式的例子,仅供参考。

```
// 测试合约
// 路径 tool/HelloWorld.sol
pragma solidity ^0.4.4;
contract HelloWorld{
    string name;
    function HelloWorld(){
       name="Hi,Welcome!";
    }
    function get()constant public returns(string){
        return name;
    }
    function set(string n) public{
        name=n;
    }
}
```

- 确认下CNS服务是否可用。  
执行babel-node abi_name_service_tool.js list,正确的输出如下：

```
babel-node abi_name_service_tool.js list
RPC=http://0.0.0.0:8712
Ouputpath=./output/
abi_name_service_tool.js  ........................Begin........................
 abi name service count => 1
 ====> index = 0 <==== 
         contract    = ContractAbiMgr
         version     = 
         address     = 0x73479ed8162e198b9627b962eb4aae7098bdc770
         blocknumber = 11
         timestamp   = 1510040068662 => 2017/11/7 15:34:28:662
         abi         = [{"constant":true,"inputs":[{"name":"name","type":"string"}],"name":"getAll","outputs":[{"name":"","type":"string"},{"name":"","type":"address"},{"name":"","type":"string"},{"name":"","type":"string"},{"name":"","type":"uint256"},{"name":"","type":"uint256"}],"payable":false,"type":"function"},{"constant":true,"inputs":[{"name":"name","type":"string"}],"name":"getContractName","outputs":[{"name":"","type":"string"}],"payable":false,"type":"function"},{"constant":true,"inputs":[{"name":"index","type":"uint256"}],"name":"getAllByIndex","outputs":[{"name":"","type":"string"},{"name":"","type":"address"},{"name":"","type":"string"},{"name":"","type":"string"},{"name":"","type":"uint256"},{"name":"","type":"uint256"}],"payable":false,"type":"function"},{"constant":true,"inputs":[{"name":"name","type":"string"}],"name":"getVersion","outputs":[{"name":"","type":"string"}],"payable":false,"type":"function"},{"constant":true,"inputs":[{"name":"name","type":"string"}],"name":"getBlockNumber","outputs":[{"name":"","type":"uint256"}],"payable":false,"type":"function"},{"constant":false,"inputs":[{"name":"name","type":"string"},{"name":"contractname","type":"string"},{"name":"version","type":"string"},{"name":"abi","type":"string"},{"name":"addr","type":"address"}],"name":"addAbi","outputs":[],"payable":false,"type":"function"},{"constant":true,"inputs":[{"name":"name","type":"string"}],"name":"getAbi","outputs":[{"name":"","type":"string"}],"payable":false,"type":"function"},{"constant":true,"inputs":[{"name":"name","type":"string"}],"name":"getAddr","outputs":[{"name":"","type":"address"}],"payable":false,"type":"function"},{"constant":false,"inputs":[{"name":"name","type":"string"},{"name":"contractname","type":"string"},{"name":"version","type":"string"},{"name":"abi","type":"string"},{"name":"addr","type":"address"}],"name":"updateAbi","outputs":[],"payable":false,"type":"function"},{"constant":true,"inputs":[{"name":"name","type":"string"}],"name":"getTimeStamp","outputs":[{"name":"","type":"uint256"}],"payable":false,"type":"function"},{"constant":true,"inputs":[],"name":"getAbiCount","outputs":[{"name":"","type":"uint256"}],"payable":false,"type":"function"},{"anonymous":false,"inputs":[{"indexed":false,"name":"name","type":"string"},{"indexed":false,"name":"contractname","type":"string"},{"indexed":false,"name":"version","type":"string"},{"indexed":false,"name":"abi","type":"string"},{"indexed":false,"name":"addr","type":"address"},{"indexed":false,"name":"blocknumber","type":"uint256"},{"indexed":false,"name":"timestamp","type":"uint256"}],"name":"AddAbi","type":"event"},{"anonymous":false,"inputs":[{"indexed":false,"name":"name","type":"string"},{"indexed":false,"name":"contractname","type":"string"},{"indexed":false,"name":"version","type":"string"},{"indexed":false,"name":"abi","type":"string"},{"indexed":false,"name":"addr","type":"address"},{"indexed":false,"name":"blocknumber","type":"uint256"},{"indexed":false,"name":"timestamp","type":"uint256"}],"name":"UpdateAbi","type":"event"}]
abi_name_service_tool.js  list mod........................End........................
```


- 合约部署:  
babel-node deploy.js HelloWorld

- 注册HelloWorld的信息到ContractAbiMgr.sol  
babel-node abi_name_service_tool.js add HelloWorld  
然后检查HelloWorld信息是否已经在ContractAbiMgr中。
```  
babel-node abi_name_service_tool.js get HelloWorld
RPC=http://0.0.0.0:8712
Ouputpath=./output/
Secp256k1 bindings are not compiled. Pure JS implementation will be used.
abi_name_service_tool.js  ........................Begin........................
 ====> contract => HelloWorld ,version =>  ,name => HelloWorld
         contract    = HelloWorld
         version     = 
         address     = 0x9c2a1ae16052435f4d1f5a4b944eacb8b65ac53e
         blocknumber = 3
         timestamp   = 1510041861947 => 2017/11/7 16:4:21:947
         abi         = [{"constant":false,"inputs":[{"name":"n","type":"string"}],"name":"set","outputs":[],"payable":false,"type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"string"}],"payable":false,"type":"function"},{"inputs":[],"payable":false,"type":"constructor"}]
abi_name_service_tool.js  get mod........................End........................
```
- 使用rpc调用HelloWordl的get接口

eth_call请求：
```
curl -X POST --data  '{"jsonrpc":"2.0","method":"eth_call","params":[{"data":{"contract":"HelloWorld","version":"","func":"get","params":[]}},"latest"],"id":1}'  "http://127.0.0.1:8712"  
```  
返回：
```
{"id":1,"jsonrpc":"2.0","result":"[\"Hi,Welcome!\"]\n"}
```
eth_jsonCall请求：  

```
curl -X POST --data  '{"jsonrpc":"2.0","method":"eth_jsonCall","params":[{"contract":"HelloWorld","version":"","func":"get","params":[]}],"id":1}'  "http://127.0.0.1:8712"  
```
返回：  

```
{"id":1,"jsonrpc":"2.0","result":{"result":["Hi,Welcome!"],"ret_code":0,"ret_msg":"success!"}}
```

js调用的示例(代码在tool/目录下可以直接运行)：  

```
var Web3 = require('web3');  
var config = require('./config');  
var fs = require('fs');  
var execSync = require('child_process').execSync;  
var web3sync = require('./web3sync');  
var BigNumber = require('bignumber.js');  
  
if (typeof web3 !== 'undefined') {  
web3 = new Web3(web3.currentProvider);  
} else {  
web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));  
}  
var result = web3sync.callByNameService("HelloWorld","get","",[]);  
console.log("result => " + result.toString());  
console.log('HelloWorld.js  ........................End........................');
```
sendRawTransaction比较复杂,不太适合直接调用rpc接口,使用js的接口做为示例(代码在tool/目录下可以直接运行)：  

```
var Web3 = require('web3');  
var config = require('./config');  
var fs = require('fs');  
var execSync = require('child_process').execSync;  
var web3sync = require('./web3sync');  
var BigNumber = require('bignumber.js');  
if (typeof web3 !== 'undefined') {  
  web3 = new Web3(web3.currentProvider);  
} else {
  web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}  
var result = web3sync.sendRawTransactionByNameService(config.account, config.privKey,"HelloWorld","set","",["test message!"]);  
console.log("result => " + result.toString());  
console.log('HelloWorld.js  ........................End........................');
```

