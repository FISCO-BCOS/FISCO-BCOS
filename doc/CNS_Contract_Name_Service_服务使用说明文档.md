[TOC]

# CNS(Contract Name Service)服务
## 一.概述
### 1.合约调用概述
调用合约的流程包括： 编写合约、编译合约、部署合约。  
以一个简单的合约HelloWorld.sol为例子：

```solidity
//HelloWorld.sol
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
对合约进行编译之后可以获取到合约接口abi的描述,数据如下：

```json
[
  {
    "constant": false,
    "inputs": [
      {
        "name": "n",
        "type": "string"
      }
    ],
    "name": "set",
    "outputs": [
      
    ],
    "payable": false,
    "stateMutability": "nonpayable",
    "type": "function"
  },
  {
    "constant": true,
    "inputs": [
      
    ],
    "name": "get",
    "outputs": [
      {
        "name": "",
        "type": "string"
      }
    ],
    "payable": false,
    "stateMutability": "view",
    "type": "function"
  },
  {
    "inputs": [
      
    ],
    "payable": false,
    "stateMutability": "nonpayable",
    "type": "constructor"
  }
]
```
然后将合约部署到区块链,可以获取到一个地址address,比如：0x269ab4bc23b07efeb3c3fd52eecfc4cbe6a50859。
最后使用address结合abi,就可以实现对合约的调用,各种SDK工具的使用方式都有所不同,但是本质上都是对address与abi使用的封装。

### 2. CNS简介
从合约调用流程可以看出来,调用流程必须的元素是合约ABI以及部署合约地址address。这种方式存在以下的问题： 
1. 合约ABI描述是个很长的JSON字符串, 是调用端需要, 对使用者来说并不友好。
2. 合约地址是个魔数, 不方便记忆, 要谨慎维护, 丢失后会导致合约不可访问。
3. 在合约每次重新部署时, 调用方都需要更新合约地址。
4. 不便于进行版本管理以及合约灰度升级。

CNS合约命名服务,提供一种由命名到合约接口调用的映射关系。  
CNS中,调用合约接口时,传入合约映射的name,接口名称以及参数信息。在底层框架的CNS Manager模块维护name与合约信息的映射关系,将根据调用传入的name 、接口、参数, 转换底层EVM需要的字节码供EVM调用。  

下面给出一个图示来说明下CNS服务的优势：  
![](imgs/CNS服务图示.png)  
1. 不在需要维护并不友好的合约ABI和合约地址address。
2. 调用方式更简单友好,只需要合约映射的CNS名称,调用接口名称,参数信息。
3. 内置的版本管理特性, 为合约提供了灰度升级的可能性。

## 二.实现
### 1. 总体框架
![](imgs/命名服务总体框架.png)  
在整个框架中, 命名服务模块提供命名服务, 客户端请求RPC调用合约服务的交易, 交易框架会首先访问合约命名服务模块, 从而解析出要访问的真实合约信息, 构造合约调用需要的信息, 进而对业务合约发出调用，并访问结果给客户端。
### 2. 主要模块  
#### a. 管理合约模块  
在管理合约中保存命名服务中name与合约信息的映射关系,合约具体信息包含合约地址、abi、版本号等,并且提供接口供外部辅助工具cns_manager.js实现添加、更新、覆盖、重置功能。同时,底层交易框架内存中会有一份该合约内容的备份,在该合约内容发生变动时,内存同步更新。  
- 当前CNS中实现的映射关系为 ： 合约名+合约版本号 => 合约详情(abi 合约地址等)
- 合约实现： systemcontractv2/ContractAbiMgr.sol  
- 辅助合约： ContractBase.sol(位于tool/ContractBase.sol)  
- 对部署的合约进行多版本版本管理,可以让合约继承ContractBase.sol,在构造函数中调用ContractBase.sol的构造函数初始化version成员。  
- 注意：ContractAbiMgr合约在系统合约中维护,所以需要使用CNS服务时需要首先部署系统合约。  
#### b. 辅助工具  
调用管理合约提供的接口, 提供添加、更新、查询、重置功能。  
- 工具: tool/cns_manager.js  

```
babel-node cns_manager.js 
 cns_manager.js Usage: 
         babel-node cns_manager.js get    contractName [contractVersion]
         babel-node cns_manager.js add    contractName
         babel-node cns_manager.js update contractName
         babel-node cns_manager.js list [simple]
         babel-node cns_manager.js historylist contractName [contractVersion] [simple]
         babel-node cns_manager.js reset contractName [contractVersion] index

```
功能介绍： 

- 命令  ： add  
  参数  ： contractName 合约名  
  功能  ： 添加contractName的信息到管理合约中  
  注意  :  如果管理合约中contractName对应的信息已经存在,会操作失败。此时可以 1. 更新当前合约的版本号,使用CNS方式调用时,指定版本号 2. 执行update操作,强行覆盖当前信息。
```javascript
//第一次add Test成功
babel-node cns_manager.js add Test
cns add operation => cns_name = Test
         cns_name =>Test
         contract =>Test
         version  =>
         address  =>0x233c777fccb9897ad5537d810068f9c6a4344e4a
         abi      =>[{"constant":false,"inputs":[{"name":"num","type":"uint256"}],"name":"trans","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"uint256"}],"payable":false,"stateMutability":"view","type":"function"},{"constant":false,"inputs":[],"name":"Ok","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"}]

//第二次add,失败
babel-node cns_manager.js add Test
cns_manager.js  ........................Begin........................
 [WARNING] cns add operation failed , ====> contract => Test version =>  is already exist. you can update it or change its version.
```
- 命令  ： get  
  参数  ： 1. contractName 合约名 2. contractVersion 版本号[可省略]  
  功能  ： 获取contractName对应contractVersion版本在管理合约中的信息

```javascript
babel-node cns_manager.js get HelloWorld
cns_manager.js  ........................Begin........................
 ====> contract => HelloWorld ,version => 
         contract    = HelloWorld
         version     = 
         address     = 0x269ab4bc23b07efeb3c3fd52eecfc4cbe6a50859
         timestamp   = 1516866720115 => 2018/1/25 15:52:0:115
         abi         = [{"constant":false,"inputs":[{"name":"n","type":"string"}],"name":"set","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"string"}],"payable":false,"stateMutability":"view","type":"function"},{"inputs":[],"payable":false,"stateMutability":"nonpayable","type":"constructor"}]
```
- 命令  ： update 
  参数  ： contractName 合约名
  功能  ： 更新contractName在管理合约中的信息  
  注意  ： 当contractName对应版本contractVersion在管理合约中不存在时会update失败,此时可以先执行add操作;被覆盖掉的信息可以通过historylist命令查询到,通过reset命令恢复。

```javascript
babel-node cns_manager.js update Test
cns_manager.js  ........................Begin........................
 ====> Are you sure update the cns of the contract ?(Y/N)
Y
cns update operation => cns_name = Test
         cns_name =>Test
         contract =>Test
         version  =>
         address  =>0x233c777fccb9897ad5537d810068f9c6a4344e4a
         abi      =>[{"constant":false,"inputs":[{"name":"num","type":"uint256"}],"name":"trans","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"uint256"}],"payable":false,"stateMutability":"view","type":"function"},{"constant":false,"inputs":[],"name":"Ok","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"}]
发送交易成功: 0x1d3caff1fba49f5ad8af3d195999454d01c64d236d9ac3ba91350dd543b10c13
```
- 命令  ： list 
  参数  :  [simple]
  功能  ： 列出管理合约中所有的信息,没有simple参数时,打印合约的详情,否则只打印合约名称跟版本号。

```javascript
babel-node cns_manager.js list simple
cns_manager.js  ........................Begin........................
 cns total count => 11
        0. contract = ContractAbiMgr ,version = 
        1. contract = SystemProxy ,version = 
        2. contract = TransactionFilterChain ,version = 
        3. contract = AuthorityFilter ,version = 
        4. contract = Group ,version = 
        5. contract = CAAction ,version = 
        6. contract = ConfigAction ,version = 
        7. contract = NodeAction ,version = 
        8. contract = HelloWorld ,version = 
        9. contract = Ok ,version = 
        10. contract = Test ,version = 
```

- 命令  ： historylist 
  参数  :  contractName 合约名称 contractVersion 合约版本号[可省略] 
  功能  ： 列出contrcactName对应版本号contractVersion被update操作覆盖的所有合约信息
```javascript
babel-node cns_manager.js historylist HelloWorld
cns_manager.js  ........................Begin........................
 cns history total count => 3
 ====> cns history list index = 0 <==== 
         contract    = HelloWorld
         version     = 
         address     = 0x1d2047204130de907799adaea85c511c7ce85b6d
         timestamp   = 1516865606159 => 2018/1/25 15:33:26:159
         abi         = [{"constant":false,"inputs":[{"name":"n","type":"string"}],"name":"set","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"string"}],"payable":false,"stateMutability":"view","type":"function"},{"inputs":[],"payable":false,"stateMutability":"nonpayable","type":"constructor"}]
 ====> cns history list index = 1 <==== 
         contract    = HelloWorld
         version     = 
         address     = 0x9c3fb4dd0a3fc5e1ea86ed3d3271b173a7084f24
         timestamp   = 1516866516542 => 2018/1/25 15:48:36:542
         abi         = [{"constant":false,"inputs":[{"name":"n","type":"string"}],"name":"set","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"string"}],"payable":false,"stateMutability":"view","type":"function"},{"inputs":[],"payable":false,"stateMutability":"nonpayable","type":"constructor"}]
 ====> cns history list index = 2 <==== 
         contract    = HelloWorld
         version     = 
         address     = 0x1d2047204130de907799adaea85c511c7ce85b6d
         timestamp   = 1516866595160 => 2018/1/25 15:49:55:160
         abi         = [{"constant":false,"inputs":[{"name":"n","type":"string"}],"name":"set","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"string"}],"payable":false,"stateMutability":"view","type":"function"},{"inputs":[],"payable":false,"stateMutability":"nonpayable","type":"constructor"}]

```
- 命令  ： reset 
  参数  :  1. contractName 合约名称 2. contractVersion 合约版本号[可省略] 3. index 索引  
  功能  ： 将被覆盖的信息恢复,index为historylist查询到的索引

#### c. RPC接口

底层rpc接口的修改,支持CNS方式的调用：
> 注意：只是修改了rpc的接口,原来的请求方式仍然兼容。  
> rpc的格式详情参考：https://github.com/ethereum/wiki/wiki/JSON-RPC  

- eth_call  
```json  
请求：
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

- eth_sendTransaction
```shell
请求：
{
  "jsonrpc": "2.0",
  "method": "eth_sendTransaction",
  "params": [
    {
      "data": {
        "contract": "",   //调用合约名称
        "version": "",    //调用合约的版本号
        "func": "",       //调用合约的接口
        "params": [       //参数
        ]
      },
      "randomid": "2"
    }
  ],
  "id": 1
}

返回：
{
  "id": 1,
  "jsonrpc": "2.0",
  "result": "" //交易hash
}
```

- eth_sendRawTransaction
  rpc请求以及返回格式跟之前完全相同,不同为之前rlp编码data字段为十六进制字符串,现在data的值改为：

```json
"data": {
        "contract": "",   //调用合约名称
        "version": "",    //调用合约的版本号
        "func": "",       //调用合约的接口
        "params": [       //参数信息
        
        ]
      }
```

目前在js的web3j客户端中已经对调用做了封装：  
接口路径：tool/web3sync.js  
callByNameService  
sendRawTransactionByNameService

## 三.使用例子  
本模块提供一些CNS一些场景下使用的例子, 供大家参考

```solidity
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

- 合约部署:  
  babel-node deploy.js HelloWorld  
>   在depoy.js中, 合约部署成功时会默认调用cns_mangager add功能, 而且会默认认为文件名与合约名相同, 如果添加失败, 需要后续部署的人自己决策：  
1. 实际上文件名与合约名不相同, 重新调用cns_manager add  
2. 只是测试合约的部署,不需要处理     
3. 同一个合约修改bug或者因为其他原因需要升级, 此时可以执行update操作 
4. 当前已经add的合约仍然需要CNS方式调用, 可以修改合约的版本号(参考多版本部署)。
```javascript
    //成功例子
    babel-node deploy.js Test0
    deploy.js  ........................Start........................
    Soc File :Test0
    Test0编译成功！
    Test0合约地址 0xfc7055a9dc68ff79a58ce4f504d8c780505b2267
    Test0部署成功！
    cns add operation => cns_name = Test0
             cns_name =>Test0
             contract =>Test0
             version  =>
             address  =>0xfc7055a9dc68ff79a58ce4f504d8c780505b2267
             abi      =>[{"constant":false,"inputs":[{"name":"n","type":"string"}],"name":"set","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"string"}],"payable":false,"stateMutability":"view","type":"function"},{"constant":false,"inputs":[],"name":"HelloWorld","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"}]
    发送交易成功: 0x84d1e6b16c58e3571f79e80588472ab8d12779234e75ceed4ac592ad1d653086

    //失败例子,表示该合约已经存在对应信息
    babel-node deploy.js HelloWorld
    deploy.js  ........................Start........................
    Soc File :HelloWorld
    HelloWorld编译成功！
    HelloWorld合约地址 0xc3869f3d9a5fc728de82cc9c807e85b77259aa3a
    HelloWorld部署成功！
     [WARNING] cns add operation failed , ====> contract => HelloWorld version =>  is already exist. you can update it or change its version.
     
```
-多版本部署  
对于add操作时,因为添加的合约对应的版本号已经存在时, 则会添加失败, 此时可以更新合约的版本号。继承ContractBase.sol指定版本号。
```solidity
pragma solidity ^0.4.4;
contract HelloWorld is ContractBase("v-1.0"){
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
再次部署
```  
babel-node deploy.js HelloWorld
deploy.js  ........................Start........................
Soc File :HelloWorld
HelloWorld编译成功！
HelloWorld合约地址 0x027d156c260110023e5bd918cc243ac12be45b17
HelloWorld部署成功！
cns add operation => cns_name = HelloWorld/v-1.0
         cns_name =>HelloWorld/v-1.0
         contract =>HelloWorld
         version  =>v-1.0
         address  =>0x027d156c260110023e5bd918cc243ac12be45b17
         abi      =>[{"constant":true,"inputs":[],"name":"getVersion","outputs":[{"name":"","type":"string"}],"payable":false,"stateMutability":"view","type":"function"},{"constant":false,"inputs":[{"name":"n","type":"string"}],"name":"set","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"string"}],"payable":false,"stateMutability":"view","type":"function"},{"constant":false,"inputs":[{"name":"version_para","type":"string"}],"name":"setVersion","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"},{"inputs":[],"payable":false,"stateMutability":"nonpayable","type":"constructor"}]
发送交易成功: 0x9a409003f5a17220809dd8e1324a36a425acaf194efd3ef1f772bbf7b49ee67c
```
此时合约版本号为： v-1.0

- RPC调用接口
```shell

1. 调用HelloWorld默认版本（即没有指定版本号）的set接口
curl -X POST --data  '{"jsonrpc":"2.0","method":"eth_sendTransaction","params":[{"data":{"contract":"HelloWorld","version":"","func":"set","params":["call defaut version"]},"randomid":"3"}],"id":1}'  "http://127.0.0.1:8746"  

{"id":1,"jsonrpc":"2.0","result":"0x77218708a73aa8c17fb9370a29254baa8f504e71b12d01d90eae0b2ef9818172"}

2. 调用HelloWorld默认版本（即没有指定版本号）的get接口
curl -X POST --data  '{"jsonrpc":"2.0","method":"eth_call","params":[{"data":{"contract":"HelloWorld","version":"","func":"get","params":[]}},"latest"],"id":1}'  "http://127.0.0.1:8746"  

{"id":1,"jsonrpc":"2.0","result":"[\"call defaut version\"]\n"}

3. 调用HelloWorld v-1.0版本的set接口
curl -X POST --data  '{"jsonrpc":"2.0","method":"eth_sendTransaction","params":[{"data":{"contract":"HelloWorld","version":"v-1.0","func":"set","params":["call v-1.0 version"]},"randomid":"4"}],"id":1}'  "http://127.0.0.1:8746"  

{"id":1,"jsonrpc":"2.0","result":"0xf43349d7be554fd332e8e4eb0c69e23292ffa8d127b0500c21109b60784aaa1d"}

4. 调用HelloWorld v-1.0版本的get接口
 curl -X POST --data  '{"jsonrpc":"2.0","method":"eth_call","params":[{"data":{"contract":"HelloWorld","version":"v-1.0","func":"get","params":[]}},"latest"],"id":1}'  "http://127.0.0.1:8746"  

{"id":1,"jsonrpc":"2.0","result":"[\"call v-1.0 version\"]\n"}
```

- 合约升级  
  如果合约需要升级的情况下, 可以执行执行update操作。  
  对HelloWorld进行升级, 首先重新部署, 因为HelloWorld之前被cns_manager添加, 所以会提示添加失败, 然后执行update操作。
```javascrpt
babel-node cns_manager.js update HelloWorld
cns_manager.js  ........................Begin........................
 ====> Are you sure update the cns of the contract ?(Y/N)
Y
cns update operation => cns_name = HelloWorld
         cns_name =>HelloWorld
         contract =>HelloWorld
         version  =>
         address  =>0x93d62e961a6801d3f614a5add207cdf45b0ff654
         abi      =>[{"constant":false,"inputs":[{"name":"n","type":"string"}],"name":"set","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"string"}],"payable":false,"stateMutability":"view","type":"function"},{"inputs":[],"payable":false,"stateMutability":"nonpayable","type":"constructor"}]
发送交易成功: 0xc8ee384185a1aaa3817474d6db6394ff6871a7bc56a15e564e7b1f57c8bfda1a

再调用get接口：
curl -X POST --data  '{"jsonrpc":"2.0","method":"eth_call","params":[{"data":{"contract":"HelloWorld","version":"","func":"get","params":[]}},"latest"],"id":1}'  "http://127.0.0.1:8746"  
{"id":1,"jsonrpc":"2.0","result":"[\"Hi,Welcome!\"]\n"}

返回 'Hi,Welcome!'。
说明当前调用的合约就是刚才部署的新合约。

```
- CNS合约重置  
  update之后, 需要将原来的合约找回, 可以通过reset进行。  
  首先查找当前合约对应版本有多少update被覆盖的合约。  
```javascript
babel-node cns_manager.js historylist HelloWorld
cns_manager.js  ........................Begin........................
 cns history total count => 4
 ====> cns history list index = 0 <==== 
         contract    = HelloWorld
         version     = 
         address     = 0x1d2047204130de907799adaea85c511c7ce85b6d
         timestamp   = 1516865606159 => 2018/1/25 15:33:26:159
         abi         = [{"constant":false,"inputs":[{"name":"n","type":"string"}],"name":"set","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"string"}],"payable":false,"stateMutability":"view","type":"function"},{"inputs":[],"payable":false,"stateMutability":"nonpayable","type":"constructor"}]
 ====> cns history list index = 1 <==== 
         contract    = HelloWorld
         version     = 
         address     = 0x9c3fb4dd0a3fc5e1ea86ed3d3271b173a7084f24
         timestamp   = 1516866516542 => 2018/1/25 15:48:36:542
         abi         = [{"constant":false,"inputs":[{"name":"n","type":"string"}],"name":"set","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"string"}],"payable":false,"stateMutability":"view","type":"function"},{"inputs":[],"payable":false,"stateMutability":"nonpayable","type":"constructor"}]
 ====> cns history list index = 2 <==== 
         contract    = HelloWorld
         version     = 
         address     = 0x1d2047204130de907799adaea85c511c7ce85b6d
         timestamp   = 1516866595160 => 2018/1/25 15:49:55:160
         abi         = [{"constant":false,"inputs":[{"name":"n","type":"string"}],"name":"set","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"string"}],"payable":false,"stateMutability":"view","type":"function"},{"inputs":[],"payable":false,"stateMutability":"nonpayable","type":"constructor"}]
 ====> cns history list index = 3 <==== 
         contract    = HelloWorld
         version     = 
         address     = 0x269ab4bc23b07efeb3c3fd52eecfc4cbe6a50859
         timestamp   = 1516866720115 => 2018/1/25 15:52:0:115
         abi         = [{"constant":false,"inputs":[{"name":"n","type":"string"}],"name":"set","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"string"}],"payable":false,"stateMutability":"view","type":"function"},{"inputs":[],"payable":false,"stateMutability":"nonpayable","type":"constructor"}]

然后查看需要被找回的合约是哪个。
babel-node cns_manager.js reset HelloWorld 3
cns_manager.js  ........................Begin........................
 ====> Are you sure update the cns of the contract ?(Y/N)
Y
cns update operation => cns_name = HelloWorld
         cns_name =>HelloWorld
         contract =>HelloWorld
         version  =>
         address  =>0x269ab4bc23b07efeb3c3fd52eecfc4cbe6a50859
         abi      =>[{"constant":false,"inputs":[{"name":"n","type":"string"}],"name":"set","outputs":[],"payable":false,"stateMutability":"nonpayable","type":"function"},{"constant":true,"inputs":[],"name":"get","outputs":[{"name":"","type":"string"}],"payable":false,"stateMutability":"view","type":"function"},{"inputs":[],"payable":false,"stateMutability":"nonpayable","type":"constructor"}]
发送交易成功: 0x4809a6105916a483ca70c4efe8e306bc01ca5d937515320d09e94a83f4a91b76

此时再调用HelloWorld的get接口:
curl -X POST --data  '{"jsonrpc":"2.0","method":"eth_call","params":[{"data":{"contract":"HelloWorld","version":"","func":"get","params":[]}},"latest"],"id":1}'  "http://127.0.0.1:8746"  
{"id":1,"jsonrpc":"2.0","result":"[\"call defaut version\"]\n"}

返回为call defaut version, 说明当前CNS调用的合约已经是最后一次比覆盖的合约。
```

- js调用  

```javascript
//调用HelloWorld get接口
var result = web3sync.callByNameService("HelloWorld","get","",[]);  

//调用HelloWorld v-1.0 get接口
var result = web3sync.callByNameService("HelloWorld","get","v-1.0",[]);  

//调用HelloWorld set接口 sendRawTransaction
var result = web3sync.sendRawTransactionByNameService(config.account,config.privKey,"HelloWorld","set","",["test message!"]);  

//调用HelloWorld v-1.0 set接口 sendRawTransaction
var result = web3sync.sendRawTransactionByNameService(config.account,config.privKey,"HelloWorld","set","v-1.0",["test message!"]); 
```