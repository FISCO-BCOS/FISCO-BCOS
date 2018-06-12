var Web3 = require('web3');
var config = require('../web3lib/config');
var fs = require('fs');
var BigNumber = require('bignumber.js');
var web3sync = require('../web3lib/web3sync');
var sha3 = require("../web3lib/sha3")

if (typeof web3 !== 'undefined') {
	web3 = new Web3(web3.currentProvider);
} else {
	web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

function getAbi(file){
	return JSON.parse(fs.readFileSync(config.Ouputpath+"./"+file+".abi",'utf-8'));
}

function getAddress(file){
	return (fs.readFileSync(config.Ouputpath+"./"+file+".address",'utf-8'));
}

function Usage() {
	console.log('The Usage of AuthorityManager.js：');
	console.log('      babel-node AuthorityManager.js Info                                  // Show the info of config and system chain');
	console.log('      babel-node AuthorityManager.js FilterChain addFilter                 // pass in the name, version and description of the filter');
	console.log('                                                 delFilter                 // Pass in the filter number');
	console.log('                                                 showFilter                // Show the info of filters');
	console.log('                                                 resetFilter               // Reset filters');
	console.log('      babel-node AuthorityManager.js Filter      getFilterStatus           // Pass in the filter number');
	console.log('                                                 enableFilter              // Pass in the filter number');
	console.log('                                                 disableFilter             // Pass in the filter number');
	console.log('                                                 setUsertoNewGroup         // Pass in the filter number and account');
	console.log('                                                 setUsertoExistingGroup    // Pass in the filter number, account and existing group');
	console.log('                                                 listUserGroup             // Pass in the filter number and account');
	console.log('      babel-node AuthorityManager.js Group       getBlackStatus            // Pass in the filter number and account');
	console.log('                                                 enableBlack               // Pass in the filter number and account');
	console.log('                                                 disableBlack              // Pass in the filter number and account');
	console.log('                                                 getDeployStatus           // Pass in the filter number and account');
	console.log('                                                 enableDeploy              // Pass in the filter number and account');
	console.log('                                                 disableDeploy             // Pass in the filter number and account');
	console.log('                                                 addPermission             // Pass in the filter number, account, contract address and function');
	console.log('                                                 delPermission             // Pass in the filter number, account, contract address and function');
	console.log('                                                 checkPermission           // Pass in the filter number, account, contract address and function');
	console.log('                                                 listPermission            // Pass in the filter number and account');
}

web3.eth.defaultAccount = config.account;

(async function() {
	var options = process.argv;
	if(options.length < 3 || options[2] == "Usage")
	{
		Usage();
		return;
	}
	var param2 = options[2];

	var account = config.account;
	var privKey = config.privKey;
	var SystemProxy = web3.eth.contract(getAbi("SystemProxy")).at(getAddress("SystemProxy"));
	var TransactionFilterChain;
	var AuthorityFilter;
	var Group;
	var routelength = SystemProxy.getRouteSize();
	for (var i = 0; i < routelength; i++) {
		var key = SystemProxy.getRouteNameByIndex(i).toString();
		var route = SystemProxy.getRoute(key);
		if ("TransactionFilterChain" == key) {
			var contract = web3.eth.contract(getAbi("TransactionFilterChain"));
			TransactionFilterChain = contract.at(route[0]);
		}
	}
  
	switch (param2){
		case "Info":
		{
			console.log("Config info:");
			console.log(config);
			console.log("System Contract:" + getAddress("SystemProxy"));
			var filterlength = TransactionFilterChain.getFiltersLength();
			console.log("The address of transactionFilterChain is " + TransactionFilterChain.address + ". The size of filters is " + filterlength);
			break;
		}
		case "FilterChain":
		{
			var param3 = options[3];
			switch (param3){
				case "addFilter":
				{
					if (options.length != 7) {
						console.log("The size of parameters is wrong, please pass in the name, version and description of the filter, eg:babel-node AuthorityManager.js FilterChain addFilter name1 version1 desc1");
					}
					else {
						console.log("Start to add filter:");
						var func = "addFilterAndInfo(string,string,string)";
						var params = [options[4],options[5],options[6]];
						var receipt = await web3sync.sendRawTransaction(account, privKey, TransactionFilterChain.address, func, params);
						console.log("Completed! The size of filters is "+TransactionFilterChain.getFiltersLength());
						var filterAddr = TransactionFilterChain.getFilter(TransactionFilterChain.getFiltersLength() - 1);
						var contract = web3.eth.contract(getAbi("AuthorityFilter"));
						AuthorityFilter = contract.at(filterAddr);
						if (true == AuthorityFilter.getEnable()) {
							console.log("The functionality of filter added just now has been enabled.");
						}
						else {
							console.log("The functionality of filter added just now has been disabled.");
						}
					} 
					break;
				}
				case "delFilter":
				{
					if (options.length != 5) {
						console.log("The size of parameters is wrong, please pass in filter number, eg：babel-node AuthorityManager.js FilterChain delFilter 0");
					}
					else {
						if (TransactionFilterChain.getFiltersLength() <= options[4] || options[4] < 0) {
							console.log("The filter number is non negative and less than the size of chain("+TransactionFilterChain.getFiltersLength()+")");
						}
						else {
							console.log("Start to del filter:");
							var func = "delFilter(uint256)";
							var params = [options[4]];
							var receipt = await web3sync.sendRawTransaction(account, privKey, TransactionFilterChain.address, func, params);
							console.log("Completed! The size of filters is "+TransactionFilterChain.getFiltersLength());
						}
					} 
					break;
				}
				case "showFilter":
				{
					var filterlength = TransactionFilterChain.getFiltersLength();
					console.log("The address of TransactionFilterChain is " + TransactionFilterChain.address + ". The size of filters is " + filterlength);
					for (var j = 0; j < TransactionFilterChain.getFiltersLength(); j++) {
						var contract = web3.eth.contract(getAbi("AuthorityFilter"));
						var AuthorityFilter = contract.at(TransactionFilterChain.getFilter(j));
						var ret = AuthorityFilter.getInfo();
						var enable;
						if (true == AuthorityFilter.getEnable()) {
							enable = "The functionality of filter has been enabled.";
						}
						else {
							enable = "The functionality of filter has been disabled.";
						}
						console.log("Filter[" + j + "] info:" + ret + "," + enable);
					}
					break;
				}
				case "resetFilter":
				{
					console.log("Start to reset Chain:");
					var filterlength = TransactionFilterChain.getFiltersLength();
					if (filterlength == 0) 
					{
						console.log("Completed! The size of filters is " + TransactionFilterChain.getFiltersLength());
						break;
					}
					for (var j = 0; j < TransactionFilterChain.getFiltersLength(); j++) {
						var contract = web3.eth.contract(getAbi("AuthorityFilter"));
						var AuthorityFilter = contract.at(TransactionFilterChain.getFilter(j));
						var func = "setEnable(bool)";
						var params = [false];
						var receipt = await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, func, params);
					}
					for (var j = TransactionFilterChain.getFiltersLength() - 1; ; j--) {
						var func = "delFilter(uint256)";
						var params = [j];
						var receipt = await web3sync.sendRawTransaction(account, privKey, TransactionFilterChain.address, func, params);
						if (0 == j) break;
					}
					console.log("Completed! The size of filters is " + TransactionFilterChain.getFiltersLength());
					break;
				}
				default:
				{
					console.log("Params Error!");
					Usage();
					break;
				}
			}
			break;
		}
		case "Filter":
		{
			var param3 = options[3];
			var AuthorityFilter;
			if (options.length > 4) {
				if (TransactionFilterChain.getFiltersLength() <= options[4] || options[4] < 0) {
					console.log("The filter number is non negative and less than the size of chain("+TransactionFilterChain.getFiltersLength()+")");
				}
				else {
					var filterAddr = TransactionFilterChain.getFilter(options[4]);
					var contract = web3.eth.contract(getAbi("AuthorityFilter"));
					AuthorityFilter = contract.at(filterAddr);
				}
			}
			switch (param3){
				case "getFilterStatus":
				{
					if (options.length != 5) {
						console.log("The size of parameters is wrong，please pass in filter number , eg:babel-node AuthorityManager.js Filter getFilterStatus 0");
					}
					else {
						if (true == AuthorityFilter.getEnable()) {
							console.log("The functionality of filter has been enabled.");
						}
						else {
							console.log("The functionality of filter has been disabled.");
						}
					}
					break;
				}
				case "enableFilter":
				{
					if (options.length != 5) {
						console.log("The size of parameters is wrong, please pass in filter number, eg:babel-node AuthorityManager.js Filter enableFilter 0");
					}
					else {
						var func = "setEnable(bool)";
						var params = [true];
						var receipt = await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, func, params);
						var ret = false;
						ret = AuthorityFilter.getEnable();
						if (true == ret) {
							console.log("The functionality of filter has been enabled.");
						}
						else {
							console.log("The functionality of filter has been disabled.");
						}
					}
					break;
				}
				case "disableFilter":
				{
					if (options.length != 5) {
						console.log("The size of parameters is wrong, please pass in filter number, eg:babel-node AuthorityManager.js Filter disableFilter 0");
					}
					else {
						var func = "setEnable(bool)";
						var params = [false];
						var receipt = await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, func, params);
						var ret = true;
						ret = AuthorityFilter.getEnable();
						if (true == ret) {
							console.log("The functionality of filter has been enabled.");
						}
						else {
							console.log("The functionality of filter has been disabled.");
						}
					}
					break;
				}
				case "setUsertoNewGroup":
				{
					if (options.length != 6) {
						console.log("The size of parameters is wrong, please pass in filter number and account, eg:babel-node AuthorityManager.js Filter setUsertoNewGroup 0 account");
					}
					else {
						console.log("Start to set user to new group:");
						var func = "setUserToNewGroup(address)";
						var params = [options[5]];
						var receipt = await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, func, params);
						console.log("Completed! The group is "+AuthorityFilter.getUserGroup(options[5]));
					}
					break;	
				}
				case "setUsertoExistingGroup":
				{
					if (options.length != 7) {
						console.log("The size of parameters is wrong, please pass in filter number, account and existing group, eg:babel-node AuthorityManager.js Filter setUsertoExistingGroup 0 account group");
					}
					else {
						console.log("Start to set user to existing group:");
						var func = "setUserToGroup(address,address)";
						var params = [options[5],options[6]];
						var receipt = await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, func, params);
						console.log("Completed!");
					} 	
					break;	
				}
				case "listUserGroup":
				{
					if (options.length != 6) {
						console.log("The size of parameters is wrong, please pass in filter number and account, eg:babel-node AuthorityManager.js Filter listUserGroup 0 account");
					}
					else {
						var ret = AuthorityFilter.getUserGroup(options[5]);
						if (typeof(ret) == "undefined") {
							console.log("No group to this account!");
						}
						else {
							console.log("The group is "+ret);
						}
					} 	
					break;
				}
				default:
				{
					console.log("Params Error!");
					Usage();
					break;
				}
			}	
			break;
		}
		case "Group":
		{
			var param3 = options[3];
			var Group;
			if (TransactionFilterChain.getFiltersLength() <= options[4]) {
				console.log("The filter number is non negative and less than the size of chain("+TransactionFilterChain.getFiltersLength()+")");
				break;
			}
			else {
				var filterAddr = TransactionFilterChain.getFilter(options[4]);
				var contract = web3.eth.contract(getAbi("AuthorityFilter"));
				var AuthorityFilter = contract.at(filterAddr);
				var group = AuthorityFilter.getUserGroup(options[5]);
				if (0x0 == group) {
					console.log("No group to this account!");
					break;
				}
				else {
					contract = web3.eth.contract(getAbi("Group"));
					Group = contract.at(group);
				}
			}
			switch (param3){
				case "getBlackStatus":
				{
					if (options.length != 6) {
						console.log("The size of parameters is wrong, please pass in filter number and account, eg:babel-node AuthorityManager.js Group getBlackStatus 0 account");
					}
					else {
						if (true == Group.getBlack()) {
							console.log("The functionality of blacklist has been enabled.");
						}
						else {
							console.log("The functionality of blacklist has been disabled.");
						}
					}
					break;
				}
				case "enableBlack":
				{
					if (options.length != 6) {
						console.log("The size of parameters is wrong, please pass in filter number and account, eg:babel-node AuthorityManager.js Group enableBlack 0 account");
					}
					else {
						var func = "setBlack(bool)";
						var params = [true];
						var receipt = await web3sync.sendRawTransaction(account, privKey, Group.address, func, params);
						var ret = false;
						ret = Group.getBlack();
						if (true == ret) {
							console.log("The functionality of blacklist has been enabled.");
						}
						else {
							console.log("The functionality of blacklist has been disabled.");
						}
					}
					break;
				}
				case "disableBlack":
				{
					if (options.length != 6) {
						console.log("The size of parameters is wrong, please pass in filter number and account, eg:babel-node AuthorityManager.js Group disableBlack 0 account");
					}
					else {
						var func = "setBlack(bool)";
						var params = [false];
						var receipt = await web3sync.sendRawTransaction(account, privKey, Group.address, func, params);
						var ret = true;
						ret = Group.getBlack();
						if (true == ret) {
							console.log("The functionality of blacklist has been enabled.");
						}
						else {
							console.log("The functionality of blacklist has been disabled.");
						}
					}
					break;
				}
				case "getDeployStatus":
				{
					if (options.length != 6) {
						console.log("The size of parameters is wrong, please pass in filter number and account, eg:babel-node AuthorityManager.js Group getDeployStatus 0 account");
					}
					else {
						if (true == Group.getCreate()) {
							console.log("The functionality of deploying contract has been enabled.");
						}
						else {
							console.log("The functionality of deploying contract has been disabled.");
						}
					}
					break;
				}
				case "enableDeploy":
				{
					if (options.length != 6) {
						console.log("The size of parameters is wrong, please pass in filter number and account, eg:babel-node AuthorityManager.js Group enableDeploy 0 account");
					}
					else {
						var func = "setCreate(bool)";
						var params = [true];
						var receipt = await web3sync.sendRawTransaction(account, privKey, Group.address, func, params);
						var ret = false;
						ret = Group.getCreate();
						if (true == ret) {
							console.log("The functionality of deploying contract has been enabled.");
						}
						else {
							console.log("The functionality of deploying contract has been disabled.");
						}
					}
					break;
				}
				case "disableDeploy":
				{
					if (options.length != 6) {
						console.log("The size of parameters is wrong, please pass in filter number and account, eg:babel-node AuthorityManager.js Group disableDeploy 0 account");
					}
					else {
						var func = "setCreate(bool)";
						var params = [false];
						var receipt = await web3sync.sendRawTransaction(account, privKey, Group.address, func, params);
						var ret = true;
						ret = Group.getCreate();
						if (true == ret) {
							console.log("The functionality of deploying contract has been enabled.");
						}
						else {
							console.log("The functionality of deploying contract has been disabled.");
						}
					}
					break;
				}
				case "addPermission":
				{
					if (options.length != 8) {
						console.log("The size of parameters is wrong, please pass in the name, version and description of the filter, eg:babel-node AuthorityManager.js Group addPermission 0 account A.address fun(string)");
					}
					else {
						console.log("Start to add permission:");
						var funcDesc = "Contract:"+options[6]+", function:"+options[7];
						var func = "setPermission(address,string,string,bool)";
						var funhash = sha3(options[7]).slice(0, 8);
						var params = [options[6],funhash,funcDesc,true];
						var receipt = await web3sync.sendRawTransaction(account, privKey, Group.address, func, params);
						console.log("Completed!");
					} 	
					break;
				}
				case "delPermission":
				{
					if (options.length != 8) {
						console.log("The size of parameters is wrong, please pass in the name, version and description of the filter, eg:babel-node AuthorityManager.js Group delPermission 0 account A.address fun(string)");
					}
					else {
						console.log("Start to del permission:");
						var funhash = sha3(options[7]).slice(0, 8);
						if (Group.getPermission(options[6],funhash) == true) {
							var funcDesc = "Contract:"+options[6]+", function:"+options[7];
							var func = "setPermission(address,string,string,bool)";
							var params = [options[6],funhash,funcDesc,false];
							var receipt = await web3sync.sendRawTransaction(account, privKey, Group.address, func, params);
							console.log("Completed!");
						}
						else {
							console.log("Without permission!");
						}
					}
					break;
				}
				case "checkPermission":	
				{
					if (options.length != 8) {
						console.log("The size of parameters is wrong, please pass in the name, version and description of the filter, eg:babel-node AuthorityManager.js Group checkPermission 0 account A.address fun(string)");
					}
					else {
						var funhash = sha3(options[7]).slice(0, 8);
						console.log("The result of authorization:"+Group.getPermission(options[6],funhash));
					} 	
					break;
				}
				case "listPermission":	
				{
					if (options.length != 6) {
						console.log("The size of parameters is wrong, please pass in filter number and account, eg:babel-node AuthorityManager.js Group listPermission 0 account");
					}
					else {
						console.log("The authorization list of account "+options[5]+" is:");
						var keyCnt = Group.getKeys();
						var idx = 0;
						for (var i = 0; i < keyCnt.length; i++) {
							var desc = Group.getFuncDescwithPermissionByKey(keyCnt[i]);
							if (desc.length > 0) {
								console.log("["+idx+"]:"+desc);
								idx++;
							}
						}
					} 	
					break;
				}
				default:
				{
					console.log("Params Error!");
					Usage();
					break;
				}
			}
			break;
		}
		default:
		{
			console.log("Params Error!");
			Usage();
			break;
		}
	}
})();