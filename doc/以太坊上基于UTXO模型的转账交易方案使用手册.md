# 以太坊上基于UTXO模型的转账交易方案——使用手册

<br> 

## 目录
<!-- TOC -->

- [1 基本介绍](#1-基本介绍)
    - [1.1 交易原则](#11-交易原则)
    - [1.2 数据类型](#12-数据类型)
- [2 使用说明](#2-使用说明)
    - [2.1 铸币交易及转账交易](#21-铸币交易及转账交易)
        - [2.1.1 铸币交易及转账交易相关的脚本命令](#211-铸币交易及转账交易相关的脚本命令)
        - [2.1.2 交易命令相关字段说明](#212-交易命令相关字段说明)
        - [2.1.3 基础交易例子](#213-基础交易例子)
        - [2.1.4 可扩展转账限制逻辑例子一：限制Token的使用账号为特定账号](#214-可扩展转账限制逻辑例子一：限制Token的使用账号为特定账号)
        - [2.1.5 可扩展转账限制逻辑例子二：限制某一账号的日转账限额](#215-可扩展转账限制逻辑例子二：限制某一账号的日转账限额)
        - [2.1.6 并行交易](#216-并行交易)
    - [2.2 查询功能](#22-查询功能)
        - [2.2.1 回溯](#221-回溯)
        - [2.2.2 查询账号余额](#222-查询账号余额)
        - [2.2.3 获取一账号下能满足支付数额的Token列表](#223-获取一账号下能满足支付数额的Token列表)
        - [2.2.4 查询基础数据对象](#224-查询基础数据对象)
        - [2.2.5 分页查询](#225-分页查询)
    - [2.3 脚本命令返回的“执行结果说明”](#23-脚本命令返回的“执行结果说明”)    
- [3 注意事项](#3-注意事项)
  - [3.1 账号注册](#31-账号注册)
  - [3.2 启用UTXO交易方式](#32-启用UTXO交易方式)
  - [3.3 业务校验逻辑及验证逻辑的传入说明](#33-业务校验逻辑及验证逻辑的传入说明)
  - [3.4 兼容性说明](#34-兼容性说明)
    ​    
    <!-- /TOC -->

=============


## 1 基本介绍

### 1.1 交易原则

本文档描述一种以太坊上基于UTXO模型的转账交易方案。与UTXO模型类似，本方案中的转账交易有以下三个原则：

- 所有交易起于铸币交易；

- 除了铸币交易之外，所有的交易输入都必须来自于前面一个或者几个交易的输出；

- 每一笔的交易支出总额等于交易收入总额。

本方案除提供可追溯Token来源的功能外，还提供**可扩展转账限制逻辑**及**并行交易**的实现。

[返回目录](#目录)

<br> 

### 1.2 数据类型

本方案的基础数据对象为Token、UTXOTx、Vault。Token为转账操作的基本单位，有特定的价值标记及所有权描述。UTXOTx描述一转账交易中来源Token与去向Token的对应关系，Vault记录一账号名下所有已花费及尚未花费的Token。三者关系如下图：

![三者关系](./imgs/UTXO.jpg)

上述三类数据基于原以太坊交易生成，独立于以太坊数据，采用LevelDB进行持久化存储。同时，本方案重点关注Token信息而非Token之间转换信息，为数据记录及使用方便，因此将Token数据记录与UTXOTx数据记录分离。

[返回目录](#目录)

<br> 

## 2 使用说明

### 2.1 铸币交易及转账交易

**2.1.1 铸币交易及转账交易相关的脚本命令**

	babel-node demoUTXO.js InitTokens				// 铸币交易
	babel-node demoUTXO.js SendSelectedTokens		// 转账交易

如无特别说明，本文档中提及的相关文件均位于tool目录下。

[返回目录](#目录)

**2.1.2 交易命令相关json字段说明**

铸币交易脚本中的json字段记录了本次铸币操作所需生成的Token个数、所有权校验类型、所有者信息（转账对象）、数额大小等内容。除上述必须字段外，Token的业务校验逻辑字段及备注字段为可选字段。铸币交易的json字段如下：

	txtype:1（交易类型为铸币操作）
	txout:交易输出列表，铸币操作生成的Token数组（数组最大限制为1000）
		checktype:所有权校验类型，string，必须字段（可选P2PK和P2PKH）
		to:转账对象，string，必须字段（如果checktype为P2PK，本字段为账号地址；如果checktype为P2PKH，本字段为账号地址的哈希值）
		value:数额大小，string，必须字段（限制数额为正整数）
		initcontract:模板合约地址，string，可选字段（与initfuncandparams配对使用）
		initfuncandparams:调用模板合约所需传入的函数及参数，string，可选字段（ABI序列化之后结果，与initcontract配对使用）
		validationcontract:校验合约地址，string，可选字段（本字段如输入为空，使用initcontract和initfuncandparams生成）
		oridetail:新创建Token的备注，string，可选字段

转账交易将脚本中json字段的交易输入Token列表进行消费。交易输入列表中除必须的Token key字段外，为实现业务逻辑校验所传入的字段需要与Token所设置的校验逻辑匹配。转账交易的json字段如下：

	txtype:2（交易类型为转账操作）
	txin:交易输入列表，本次转账消费的Token数组（数组最大限制为1000）
		tokenkey:转账使用的Token，string，必须字段（Token地址）
		callfuncandparams:业务校验逻辑所需传入的函数及参数，string，可选字段（ABI序列化之后结果，所消费的Token中存在校验合约地址时使用，通用合约及实例合约均需）
		exefuncandparams:执行业务校验逻辑（更新数据）所需传入的函数及参数，string，可选字段（ABI序列化之后结果，所消费的Token中存在校验合约地址时使用，只限通用合约需要）
		desdetail:Token的转账备注，string，可选字段
	txout:交易输出列表，转账操作生成的Token数组，内容同铸币交易的txout

[返回目录](#目录)

**2.1.3 基础交易例子**

铸币交易：铸币给账号0x3ca576d469d7aa0244071d27eb33c5629753593e，价值为100单位，所有权校验类型为P2PK，json描述为：

	var param = "{\"utxotype\":1,\"txout\":[{\"to\":\"0x3ca576d469d7aa0244071d27eb33c5629753593e\",\"value\":\"100\",\"checktype\":\"P2PK\"}]}";
	await web3sync.sendUTXOTransaction(config.account, config.privKey, [param]);

相关执行例子如下：

	[fisco-bcos@VM_centos tool]$ babel-node demoUTXO.js InitTokens
	Param:
	{ utxotype: 1,
	  txout: 
	   [ { to: '0x3ca576d469d7aa0244071d27eb33c5629753593e',
	       value: '100',
	       checktype: 'P2PK' } ] }
	send transaction success: 0xa1299435e1cbc019aed361f5c59759411189087471528c5638ad1e66f654e3b1
	Receipt:
	{ blockHash: '0xf986fd0c11c62a08926f23bc05f19db385d9515b7540c1b963fb6298c6978d3c',
	  blockNumber: 31,
	  contractAddress: '0x0000000000000000000000000000000000000000',
	  cumulativeGasUsed: 30000,
	  gasUsed: 30000,
	  logs: [],
	  transactionHash: '0xa1299435e1cbc019aed361f5c59759411189087471528c5638ad1e66f654e3b1',
	  transactionIndex: 0 }

转账交易：账号0x3ca576d469d7aa0244071d27eb33c5629753593e使用上述新铸的Token（记为Token1,在交易哈希为0xa1299435e1cbc019aed361f5c59759411189087471528c5638ad1e66f654e3b1的交易中生成）给账号0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3转账60价值单位，所有权校验类型为P2PK，将找零40价值单位，所有权校验类型为P2PKH，json描述为：

	var Token1 = "0xa1299435e1cbc019aed361f5c59759411189087471528c5638ad1e66f654e3b1_0";
	var shaSendTo = "0x"+sha3("0x3ca576d469d7aa0244071d27eb33c5629753593e").toString();
	var param = "{\"utxotype\":2,\"txin\":[{\"tokenkey\":\""+Token1+"\"}],\"txout\":[{\"to\":\"0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3\",\"value\":\"60\",\"checktype\":\"P2PK\"},{\"to\":\""+shaSendTo+"\",\"value\":\"40\",\"checktype\":\"P2PKH\"}]}";
	await web3sync.sendUTXOTransaction(config.account, config.privKey, [param]);

相关执行例子如下：

	[fisco-bcos@VM_centos tool]$ babel-node demoUTXO.js SendSelectedTokens
	Param:
	{ utxotype: 2,
	  txin: [ { tokenkey: '0xa1299435e1cbc019aed361f5c59759411189087471528c5638ad1e66f654e3b1_0' } ],
	  txout: 
	   [ { to: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3',
	       value: '60',
	       checktype: 'P2PK' },
	     { to: '0x14a940d346b813e2204c74c13bbd556dc7c79a4e78e82617004d5ff77aa3b582',
	       value: '40',
	       checktype: 'P2PKH' } ] }
	send transaction success: 0x06f0e326294e39183be0b04b4f5b60c927b0e43caaf9b709af1b62cd0f74951a
	Receipt:
	{ blockHash: '0x6c35f421fdd8c43cf8163db9620b67f9c08cf0cda79ce1da63e0612637c36b16',
	  blockNumber: 32,
	  contractAddress: '0x0000000000000000000000000000000000000000',
	  cumulativeGasUsed: 30000,
	  gasUsed: 30000,
	  logs: [],
	  transactionHash: '0x06f0e326294e39183be0b04b4f5b60c927b0e43caaf9b709af1b62cd0f74951a',
	  transactionIndex: 0 }

[返回目录](#目录)

**2.1.4 可扩展转账限制逻辑例子一：限制Token的使用账号为特定账号**

铸币及转账交易前需先部署tool目录下的UserCheckTemplate.sol合约。铸币交易时，需添加限制条件，传入合约地址、调用接口及特定账号名单，传入的接口及人员名单使用ABI编码。转账交易时，需传入验证接口及相应账号的ABI编码结果。

对于有不同特定账号的限制条件的Token，上述合约只需部署一次，记合约部署后的地址为0x7dc38c5e144cbbb4cd6e8a65091da52a78d584f5。ABI编码详细信息可参考[https://github.com/ethereum/wiki/wiki/Ethereum-Contract-ABI](https://github.com/ethereum/wiki/wiki/Ethereum-Contract-ABI)。

铸币交易：铸币给账号0x3ca576d469d7aa0244071d27eb33c5629753593e，价值为100单位，所有权校验类型为P2PK，生成的Token只允许config.account使用（不失一般性可描述config.account为0x3ca576d469d7aa0244071d27eb33c5629753593e，即此铸币交易的发起方），json描述为：

	var initContractAddr = "0x7dc38c5e144cbbb4cd6e8a65091da52a78d584f5";                    // 模板合约地址，用于创建实例合约，创建Token传入
	// tx_data为调用模板合约的函数说明及参数，创建Token传入
	var initFunc = "newUserCheckContract(address[])";                                       // 模板合约中的函数说明（含参数，没有空格）
	var initParams = [[config.account]];                                     				// 调用函数传入的参数列表
	var init_tx_data = getTxData(initFunc, initParams);                                     // ABI序列化
	var param = "{\"utxotype\":1,\"txout\":[{\"to\":\"0x3ca576d469d7aa0244071d27eb33c5629753593e\",\"value\":\"100\",\"checktype\":\"P2PK\",\"initcontract\":\""+initContractAddr+"\",\"initfuncandparams\":\""+init_tx_data+"\",\"oridetail\":\"Only userd by config.account\"}]}";
	await web3sync.sendUTXOTransaction(config.account, config.privKey, [param]);

其中detail字段可以用来描述只归某些人所使用的信息。相关执行例子如下：

	[fisco-bcos@VM_centos tool]$ babel-node demoUTXO.js InitTokens
	Param:
	{ utxotype: 1,
	  txout: 
	   [ { to: '0x3ca576d469d7aa0244071d27eb33c5629753593e',
	       value: '100',
	       checktype: 'P2PK',
	       initcontract: '0x7dc38c5e144cbbb4cd6e8a65091da52a78d584f5',
	       initfuncandparams: '0xe8ef9289000000000000000000000000000000000000000000000000000000000000002000000000000000000000000000000000000000000000000000000000000000010000000000000000000000003ca576d469d7aa0244071d27eb33c5629753593e',
	       oridetail: 'Only userd by config.account' } ] }
	send transaction success: 0xf60b1f4e5ff6ebd2b55a8214d8a979b56912b48d1d48033dff64bd445899e24b
	Receipt:
	{ blockHash: '0xdb10177f2cce8f2528c9035de16ac170e2e04031db08b9c9a7d328139c567d90',
	  blockNumber: 35,
	  contractAddress: '0x0000000000000000000000000000000000000000',
	  cumulativeGasUsed: 30000,
	  gasUsed: 30000,
	  logs: [],
	  transactionHash: '0xf60b1f4e5ff6ebd2b55a8214d8a979b56912b48d1d48033dff64bd445899e24b',
	  transactionIndex: 0 }

转账交易：账号0x3ca576d469d7aa0244071d27eb33c5629753593e使用上述新铸的Token（记为Token1，在交易哈希为0xf60b1f4e5ff6ebd2b55a8214d8a979b56912b48d1d48033dff64bd445899e24b的交易中生成）给账号0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3转账60价值单位，所有权校验类型为P2PK，将找零40价值单位，所有权校验类型为P2PKH，所生成的Token不再有使用账号的限制，json描述为：

	var Token1 = "0xf60b1f4e5ff6ebd2b55a8214d8a979b56912b48d1d48033dff64bd445899e24b_0";
	var checkFunc = "check(address)";
	var checkParams = [config.account];
	var check_tx_data = getTxData(checkFunc, checkParams);
	var shaSendTo = "0x"+sha3("0x3ca576d469d7aa0244071d27eb33c5629753593e").toString();
	var param = "{\"utxotype\":2,\"txin\":[{\"tokenkey\":\""+Token1+"\",\"callfuncandparams\":\""+check_tx_data+"\"}],\"txout\":[{\"to\":\"0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3\",\"value\":\"60\",\"checktype\":\"P2PK\"},{\"to\":\""+shaSendTo+"\",\"value\":\"40\",\"checktype\":\"P2PKH\"}]}";
	await web3sync.sendUTXOTransaction(config.account, config.privKey, [param]);

相关执行例子如下：

	[fisco-bcos@VM_centos tool]$ babel-node demoUTXO.js SendSelectedTokens
	Param:
	{ utxotype: 2,
	  txin: 
	   [ { tokenkey: '0xf60b1f4e5ff6ebd2b55a8214d8a979b56912b48d1d48033dff64bd445899e24b_0',
	       callfuncandparams: '0xc23697a80000000000000000000000003ca576d469d7aa0244071d27eb33c5629753593e' } ],
	  txout: 
	   [ { to: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3',
	       value: '60',
	       checktype: 'P2PK' },
	     { to: '0x14a940d346b813e2204c74c13bbd556dc7c79a4e78e82617004d5ff77aa3b582',
	       value: '40',
	       checktype: 'P2PKH' } ] }
	send transaction success: 0xe351f004f9bf163f5f8905d1addd79e6aa46d0295fabae2433bf055f38e299d9
	Receipt:
	{ blockHash: '0xb3898ceff824ce3776f3bbf3bb1ccb89be156ded5c4f452811fa831f4582ce66',
	  blockNumber: 36,
	  contractAddress: '0x0000000000000000000000000000000000000000',
	  cumulativeGasUsed: 30000,
	  gasUsed: 30000,
	  logs: [],
	  transactionHash: '0xe351f004f9bf163f5f8905d1addd79e6aa46d0295fabae2433bf055f38e299d9',
	  transactionIndex: 0 }

[返回目录](#目录)

**2.1.5 可扩展转账限制逻辑例子二：限制某一账号的日转账限额**

铸币及转账交易前需先部署tool目录下的Limitation.sol合约。此合约可用于记录不同用户的日转账限额及当日已转账数额，并提供设置日转账限额的接口。铸币交易时，需传入部署的Limitation.sol合约地址作为验证逻辑入口。转账交易时，需传入相关的调用接口及数据（使用ABI编码，含验证数据及更新数据）。

记合约部署后的地址为0x3dbac83f7050e377a9205fed1301ae4239fa48e1。在UTXO交易前设置账号的日转账限额，设置日转账限额的相关脚本为demoLimitation.js。不失一般性记账号为0x3ca576d469d7aa0244071d27eb33c5629753593e。

铸币交易：铸币给账号0x3ca576d469d7aa0244071d27eb33c5629753593e，价值为100单位，所有权校验类型为P2PK，生成的Token只允许config.account使用（不失一般性可描述config.account为0x3ca576d469d7aa0244071d27eb33c5629753593e，即此铸币交易的发起方），json描述为：

	var validationContractAddr = "0x3dbac83f7050e377a9205fed1301ae4239fa48e1";              // 通用合约地址，创建Token传入
	var param = "{\"utxotype\":1,\"txout\":[{\"to\":\"0x3ca576d469d7aa0244071d27eb33c5629753593e\",\"value\":\"100\",\"checktype\":\"P2PK\",\"validationcontract\":\""+validationContractAddr+"\",\"oridetail\":\"Account with Limitation per day\"}]}";
	await web3sync.sendUTXOTransaction(config.account, config.privKey, [param]);

相关执行例子如下：

	[fisco-bcos@VM_centos tool]$ babel-node demoUTXO.js InitTokens
	Param:
	{ utxotype: 1,
	  txout: 
	   [ { to: '0x3ca576d469d7aa0244071d27eb33c5629753593e',
	       value: '100',
	       checktype: 'P2PK',
	       validationcontract: '0x3dbac83f7050e377a9205fed1301ae4239fa48e1',
	       oridetail: 'Account with Limitation per day' } ] }
	send transaction success: 0xd77d4b655c6f3a7870ef66676b1375249f1e5ff34045374a1fc244f2fdf09be6
	Receipt:
	{ blockHash: '0xccf85aa676b564a5c61c06d4381b379769492a0c4c20a0c6531b63245a09a207',
	  blockNumber: 40,
	  contractAddress: '0x0000000000000000000000000000000000000000',
	  cumulativeGasUsed: 30000,
	  gasUsed: 30000,
	  logs: [],
	  transactionHash: '0xd77d4b655c6f3a7870ef66676b1375249f1e5ff34045374a1fc244f2fdf09be6',
	  transactionIndex: 0 }

转账交易：账号0x3ca576d469d7aa0244071d27eb33c5629753593e使用上述新铸的Token（记为Token1，在交易哈希为0xd77d4b655c6f3a7870ef66676b1375249f1e5ff34045374a1fc244f2fdf09be6的交易中生成）给账号0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3转账60价值单位，所有权校验类型为P2PK，将找零40价值单位，所有权校验类型为P2PKH，所生成的Token不再有日转账限额的限制，json描述为：

	var Token1 = "0xd77d4b655c6f3a7870ef66676b1375249f1e5ff34045374a1fc244f2fdf09be6_0";
	var checkFunc = "checkSpent(address,uint256)";
	var checkParams = [config.account,100];
	var check_tx_data = getTxData(checkFunc, checkParams);
	var updateFunc = "addSpent(address,uint256)";
	var updateParams = [config.account,100];
	var update_tx_data = getTxData(updateFunc, updateParams);
	var shaSendTo = "0x"+sha3("0x3ca576d469d7aa0244071d27eb33c5629753593e").toString();
	var param = "{\"utxotype\":2,\"txin\":[{\"tokenkey\":\""+Token1+"\",\"callfuncandparams\":\""+check_tx_data+"\",\"exefuncandparams\":\""+update_tx_data+"\"}],\"txout\":[{\"to\":\"0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3\",\"value\":\"60\",\"checktype\":\"P2PK\"},{\"to\":\""+shaSendTo+"\",\"value\":\"40\",\"checktype\":\"P2PKH\"}]}";
	await web3sync.sendUTXOTransaction(config.account, config.privKey, [param]);

相关执行例子如下：

	[fisco-bcos@VM_centos tool]$ babel-node demoUTXO.js SendSelectedTokens
	Param:
	{ utxotype: 2,
	  txin: 
	   [ { tokenkey: '0xd77d4b655c6f3a7870ef66676b1375249f1e5ff34045374a1fc244f2fdf09be6_0',
	       callfuncandparams: '0x3be8c7e40000000000000000000000003ca576d469d7aa0244071d27eb33c5629753593e0000000000000000000000000000000000000000000000000000000000000064',
	       exefuncandparams: '0x76d9a04a0000000000000000000000003ca576d469d7aa0244071d27eb33c5629753593e0000000000000000000000000000000000000000000000000000000000000064' } ],
	  txout: 
	   [ { to: '0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3',
	       value: '60',
	       checktype: 'P2PK' },
	     { to: '0x14a940d346b813e2204c74c13bbd556dc7c79a4e78e82617004d5ff77aa3b582',
	       value: '40',
	       checktype: 'P2PKH' } ] }
	send transaction success: 0x278d3b3ffa7380baba00e8029aa1e8fd2455ceb82562b82cbce41c344e4b1584
	Receipt:
	{ blockHash: '0xdd5fd6bc49759b7b750158dacf766998c4493ec32405c99c5d38b5b5e93c3da9',
	  blockNumber: 43,
	  contractAddress: '0x0000000000000000000000000000000000000000',
	  cumulativeGasUsed: 30000,
	  gasUsed: 30000,
	  logs: [],
	  transactionHash: '0x278d3b3ffa7380baba00e8029aa1e8fd2455ceb82562b82cbce41c344e4b1584',
	  transactionIndex: 0 }

[返回目录](#目录)

**2.1.6 并行交易**

本方案对满足以下条件的铸币/转账交易将进行并行处理。并行处理的线程数与机器配置相关，但用户对交易并行无感知。

- 为UTXO类型的交易；

- 同一块中已打包的交易不涉及对同一个Token的使用；

- 交易没有设置需通过合约实现的验证逻辑。

[返回目录](#目录)

<br> 

### 2.2 查询功能

**2.2.1 回溯**

	babel-node demoUTXO.js TokenTracking ${TokenKey}

相关执行例子如下：

	[fisco-bcos@VM_centos tool]$ babel-node demoUTXO.js TokenTracking 0x278d3b3ffa7380baba00e8029aa1e8fd2455ceb82562b82cbce41c344e4b1584_1
	Param[0]:
	{ utxotype: 8,
	  queryparams: 
	   [ { tokenkey: '0x278d3b3ffa7380baba00e8029aa1e8fd2455ceb82562b82cbce41c344e4b1584_1',
	       cnt: '3' } ] }
	Result[0]:
	{ begin: 0,
	  cnt: 3,
	  code: 0,
	  data: 
	   [ '{"in":["0xd77d4b655c6f3a7870ef66676b1375249f1e5ff34045374a1fc244f2fdf09be6_0"],"out":["0x278d3b3ffa7380baba00e8029aa1e8fd2455ceb82562b82cbce41c344e4b1584_0","0x278d3b3ffa7380baba00e8029aa1e8fd2455ceb82562b82cbce41c344e4b1584_1"]}',
	     '{"out":["0xd77d4b655c6f3a7870ef66676b1375249f1e5ff34045374a1fc244f2fdf09be6_0"]}' ],
	  end: 1,
	  msg: 'Success',
	  total: 2 }

上述例子使用了分页查询，查询输入的Param[x]的返回结果为Result[x]，两者一一配对。Param中json字段说明如下：

	txtype:8（交易类型为回溯Token来源交易信息）
	queryparams:回溯参数列表，数组（数组大小为1）
		tokenkey:需回溯的Token Key，string，必须字段（Token地址）
		begin:分页查询的起始位置，string，可选字段（如不传入默认为0）
		cnt:分页查询的查询个数，string，可选字段（如不传入默认为10）

Result中json字段说明如下：

	begin:该页查询的数据在查询结果核心数据中的起始位置（Param传入）
	cnt:该页查询的查询个数（Param传入）
	code:执行结果代码
	data:查询结果核心数据（这里为该Token从铸币开始到目前转账交易的倒序列表）
	end:该页查询的数据在查询结果核心数据中的结束位置
	msg:执行结果说明（与执行结果代码配对，结果说明详见2.3）
	total:查询结果核心数据列表的长度

[返回目录](#目录)

**2.2.2 查询账号余额**
​	
	babel-node demoUTXO.js GetBalance ${Account}

相关执行例子如下：

	[fisco-bcos@VM_centos tool]$ babel-node demoUTXO.js GetBalance 0x3ca576d469d7aa0244071d27eb33c5629753593e 
	Param:
	{ utxotype: 9,
	  queryparams: [ { account: '0x3ca576d469d7aa0244071d27eb33c5629753593e' } ] }
	Result:
	{ balance: 320, code: 0, msg: 'Success' }

[返回目录](#目录)

**2.2.3 获取一账号下能满足支付数额的Token列表**

	babel-node demoUTXO.js SelectTokens ${Account} ${Value}

相关执行例子如下：

	[fisco-bcos@VM_centos tool]$ babel-node demoUTXO.js SelectTokens 0x3ca576d469d7aa0244071d27eb33c5629753593e 245 
	Param[0]:
	{ utxotype: 7,
	  queryparams: 
	   [ { account: '0x3ca576d469d7aa0244071d27eb33c5629753593e',
	       value: '245' } ] }
	Result[0]:
	{ begin: 0,
	  cnt: 10,
	  code: 0,
	  data: 
	   [ '0xbf7d37c245ce96fa72b669a2e2fcc006e4adacfb6dc4c3746bd1311feba0bc0e_0',
	     '0x69cb330a4fe9addae3bceb3550f82f231d7a3c2ce6d7cb2e1a7bff54476562d1_0',
	     '0xe351f004f9bf163f5f8905d1addd79e6aa46d0295fabae2433bf055f38e299d9_1',
	     '0x278d3b3ffa7380baba00e8029aa1e8fd2455ceb82562b82cbce41c344e4b1584_1' ],
	  end: 3,
	  msg: 'Success',
	  total: 4,
	  totalTokenValue: 280 }

[返回目录](#目录)

**2.2.4 查询基础数据对象（Token、UTXOTx、Vault)**

查询Token信息

	babel-node demoUTXO.js GetToken ${TokenKey}

Token查询执行例子如下：

	[fisco-bcos@VM_centos tool]$ babel-node demoUTXO.js GetToken 0x278d3b3ffa7380baba00e8029aa1e8fd2455ceb82562b82cbce41c344e4b1584_1
	Param:
	{ utxotype: 4,
	  queryparams: [ { tokenkey: '0x278d3b3ffa7380baba00e8029aa1e8fd2455ceb82562b82cbce41c344e4b1584_1' } ] }
	Result:
	{ code: 0,
	  data: 
	   { TxHash: '0x278d3b3ffa7380baba00e8029aa1e8fd2455ceb82562b82cbce41c344e4b1584',
	     checkType: 'P2PKH',
	     contractType: '',
	     detail: '',
	     index: 1,
	     owner: '0x14a940d346b813e2204c74c13bbd556dc7c79a4e78e82617004d5ff77aa3b582',
	     state: 1,
	     validationContract: '0x0000000000000000000000000000000000000000',
	     value: 40 },
	  msg: 'Success' }

查询UTXOTx信息

	babel-node demoUTXO.js GetTx ${TxKey}

UTXOTx查询执行例子如下：

	[fisco-bcos@VM_centos tool]$ babel-node demoUTXO.js GetTx 0x278d3b3ffa7380baba00e8029aa1e8fd2455ceb82562b82cbce41c344e4b1584
	Param:
	{ utxotype: 5,
	  queryparams: [ { txkey: '0x278d3b3ffa7380baba00e8029aa1e8fd2455ceb82562b82cbce41c344e4b1584' } ] }
	Result:
	{ code: 0,
	  data: 
	   { in: [ '0xd77d4b655c6f3a7870ef66676b1375249f1e5ff34045374a1fc244f2fdf09be6_0' ],
	     out: 
	      [ '0x278d3b3ffa7380baba00e8029aa1e8fd2455ceb82562b82cbce41c344e4b1584_0',
	        '0x278d3b3ffa7380baba00e8029aa1e8fd2455ceb82562b82cbce41c344e4b1584_1' ] },
	  msg: 'Success' }

查询Vault信息

	// 查询一账号下的Vault，传入需查询的账号及查询Token类型（0为全查询，1为查询尚未花费的Token，2为查询已经花费的Token）
	babel-node demoUTXO.js GetVault ${Account} ${TokenType}

Vault查询执行例子如下：

	[fisco-bcos@VM_centos tool]$ babel-node demoUTXO.js GetVault 0x3ca576d469d7aa0244071d27eb33c5629753593e 0
	Param[0]:
	{ utxotype: 6,
	  queryparams: 
	   [ { account: '0x3ca576d469d7aa0244071d27eb33c5629753593e',
	       value: '0',
	       cnt: '6' } ] }
	Result[0]:
	{ begin: 0,
	  cnt: 6,
	  code: 0,
	  data: 
	   [ '0x06f0e326294e39183be0b04b4f5b60c927b0e43caaf9b709af1b62cd0f74951a_1',
	     '0x278d3b3ffa7380baba00e8029aa1e8fd2455ceb82562b82cbce41c344e4b1584_1',
	     '0x4c1a08acbfa16e496489de87e269aef927ea582e674d6c7e363779f8576873a0_0',
	     '0x69cb330a4fe9addae3bceb3550f82f231d7a3c2ce6d7cb2e1a7bff54476562d1_0',
	     '0xa1299435e1cbc019aed361f5c59759411189087471528c5638ad1e66f654e3b1_0',
	     '0xbf7d37c245ce96fa72b669a2e2fcc006e4adacfb6dc4c3746bd1311feba0bc0e_0' ],
	  end: 5,
	  msg: 'Success',
	  total: 9 }
	Param[1]:
	{ utxotype: 6,
	  queryparams: 
	   [ { account: '0x3ca576d469d7aa0244071d27eb33c5629753593e',
	       value: '0',
	       begin: '6',
	       cnt: '6' } ] }
	Result[1]:
	{ begin: 6,
	  cnt: 6,
	  code: 0,
	  data: 
	   [ '0xd77d4b655c6f3a7870ef66676b1375249f1e5ff34045374a1fc244f2fdf09be6_0',
	     '0xe351f004f9bf163f5f8905d1addd79e6aa46d0295fabae2433bf055f38e299d9_1',
	     '0xf60b1f4e5ff6ebd2b55a8214d8a979b56912b48d1d48033dff64bd445899e24b_0' ],
	  end: 8,
	  msg: 'Success',
	  total: 9 }

**2.2.5 分页查询**
​	
本方案对GetVault、SelectTokens和TokenTracking接口，提供分页查询功能。相关执行例子及字段说明见上。


[返回目录](#目录)

<br>

### 2.3 脚本命令返回的“执行结果说明”

	"Success",						// "执行成功",
	"TokenIDInvalid",				// "Token ID不存在",
	"TxIDInvalid",					// "Tx ID不存在",
	"AccountInvalid",				// "账号不存在",
	"TokenUsed",					// "该Token已经使用",
	"TokenOwnerShipCheckFail",		// "该Token所有权验证失败",
	"TokenLogicCheckFail",			// "该Token逻辑验证失败",
	"TokenAccountingBalanceFail",	// "该交易会计等式验证失败",
	"AccountBalanceInsufficient",	// "该账号余额不足",
	"JsonParamError",				// "输入Json参数错误",
	"UTXOTypeInvalid",				// "UTXO交易类型错误",
	"AccountRegistered",			// "账号已经存在",
	"TokenCntOutofRange",			// "交易用的Token参数超限（TokenMaxCnt）",
	"LowEthVersion",				// "Eth的版本过低，无法处理UTXO交易",
	"OtherFail"						// "其余失败情况"

[返回目录](#目录)

<br>

## 3 注意事项

### 3.1 账号注册

账号只有注册后才能进行GetVault、SelectTokens、GetBalance操作。账号在区块链中注册一次即可，后续链启动后会获取之前已经注册过的账号。

	// 账号注册	
	babel-node demoUTXO.js RegisterAccount ${Account}

[返回目录](#目录)

<br>

### 3.2 启用UTXO交易方式

在发送UTXO相关交易前，需发送交易来启用UTXO交易。启用后，区块链支持原以太坊交易和UTXO交易的发送，因此为发送原以太坊交易不需另行关闭UTXO交易。

	// 启用UTXO交易，Height为区块链当前块高+1
	babel-node tool.js ConfigAction set updateHeight ${Height}

[返回目录](#目录)

<br>

### 3.3 业务校验逻辑及验证逻辑的传入说明

用户进行转账交易时，为了保证交易转账的相关信息（如发送账号、转账数额）和验证逻辑所传入的数据一致，建议系统管理者封装一层接口用于发送交易（含交易中需传入的验证信息），供外部用户调用。

[返回目录](#目录)

<br>

### 3.4 兼容性说明

- 本UTXO交易方案可实现对链原有数据的兼容；

- 本UTXO交易的启用及后续交易的发送需在关闭国密功能的情况下进行。

[返回目录](#目录)

<br>















