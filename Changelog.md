### V1.1.0 (2018-03-27)  

* Update  
1. js文件整理: 删除accounttool目录。
2. 将tool、systemcontractv2目录中的公共js代码放入web3lib中。 

* Add:
1. CNS添加对java客户端的支持。
2. 添加支持CNS方式调用的rpc接口, CNS方式调用添加的新的接口如下：
CNS方式调用接口                 原接口
eth_getCodeCNS             =>    eth_getCode
eth_getBalanceCNS           =>    eth_getBalance
eth_getStorageAtCNS         =>    eth_getStorageAt
eth_getTransactionCountCNS   =>    eth_getTransactionCount

* Fix:
1. CNS调用合约支持合约重载的接口。  

### V2.0.0 (2018-03-28)  
* Update
1. 为查询block的RPC接口增加更多的返回字段。  
2. FISCO BCOS用户手册更新, a. 在web3lib中需要增加cnpm install的操作  b. config.js文件放入weblib3目录中。  

* Add:
1. 添加打印监控日志的功能，适配于区块链浏览器的report agent。  
2. scripts/install_deps.sh 依赖脚本添加Linux Oracle Server的支持。  

### 2018-03-29  
* Fix
1. 修正一键安装脚本编译完成之后, 启动nodejs模块缺失的问题
