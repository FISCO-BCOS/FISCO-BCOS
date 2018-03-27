### V2.0.0 (2018-03-27)  

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