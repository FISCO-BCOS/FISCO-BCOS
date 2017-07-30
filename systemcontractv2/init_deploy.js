var Web3= require('web3');
var config=require('./config_local');
//var fs=require('fs');
//var execSync =require('child_process').execSync;
//var BigNumber = require('bignumber.js');
//var net = require('net');
//var coder = require('./codeUtils');
var web3sync = require('./web3sync');

if (typeof web3 !== 'undefined') {
	web3 = new Web3(web3.currentProvider);
} else {
	web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

(async function() {
	//部署合约，初始化参数
	var system_proxy = await web3sync.deploy(config.account, "SystemProxy");

	var transaction_filter_chain = await web3sync.deploy(config.account, "TransactionFilterChain");
	var authority = await web3sync.deploy(config.account, "Authority");
	var group = await web3sync.deploy(config.account, "Group");

	var ca_action = await web3sync.deploy(config.account, "CAAction");
	var node_action = await web3sync.deploy(config.account, "NodeAction");
	var config_action = await web3sync.deploy(config.account, "ConfigAction");

	var test = await web3sync.deploy(config.account, "Test");

	var func = "setRoute(string,address,bool)";
	var params = ["transactionFilterChain", transaction_filter_chain.address, false];
	var receipt = await web3sync.sendRawTransaction(config.account, config.privKey, group.address, func, params);

	func = "setRoute(string,address,bool)";
	params = ["CAAction", ca_action.address, false];
	receipt = await web3sync.sendRawTransaction(config.account, config.privKey, group.address, func, params);

	func = "setRoute(string,address,bool)";
	params = ["NodeAction", node_action.address, false];
	receipt = await web3sync.sendRawTransaction(config.account, config.privKey, group.address, func, params);

	func = "setRoute(string,address,bool)";
	params = ["ConfigAction", config_action.address, false];
	receipt = await web3sync.sendRawTransaction(config.account, config.privKey, group.address, func, params);

	console.log("合约部署完成 system_proxy:" + system_proxy.address);
})();
