var Web3= require('web3');
var config=require('../web3lib/config');
var fs=require('fs');
var execSync =require('child_process').execSync;
var web3sync = require('../web3lib/web3sync');
var BigNumber = require('bignumber.js');

/*
*   npm install --save-dev babel-cli babel-preset-es2017
*   echo '{ "presets": ["es2017"] }' > .babelrc
*    npm install secp256k1
*   npm install keccak
*   npm install rlp
*/
if (typeof web3 !== 'undefined') {
	web3 = new Web3(web3.currentProvider);
} else {
	web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

function getAbi(file){
	var abi=JSON.parse(fs.readFileSync(config.Ouputpath+"./"/*+file+".sol:"*/+file+".abi",'utf-8'));
	return abi;
}

function getAbi0(file){
	var abi=fs.readFileSync(config.Ouputpath+"./"/*+file+".sol:"*/+file+".abi",'utf-8');
	return abi;
}

(async function() {
    //var account=web3.eth.accounts[0];

    //console.log('account='+web3.eth.accounts[0]);

	  //部署合约，初始化参数
    var SystemProxyReicpt= await web3sync.rawDeploy(config.account, config.privKey,  "SystemProxy");
    var SystemProxy=web3.eth.contract(getAbi("SystemProxy")).at(SystemProxyReicpt.contractAddress);

    // 权限控制
    //execSync("fisco-solc --abi --bin --overwrite -o "+config.Ouputpath+" AuthorityFilter.sol");
    var AuthorityFilterReicpt= await web3sync.rawDeploy(config.account, config.privKey, "AuthorityFilter");
    var AuthorityFilter=web3.eth.contract(getAbi("AuthorityFilter")).at(AuthorityFilterReicpt.contractAddress);
    //execSync("fisco-solc --abi --bin --overwrite -o "+config.Ouputpath+" Group.sol");
    var GroupReicpt= await web3sync.rawDeploy(config.account, config.privKey, "Group");
    var Group=web3.eth.contract(getAbi("Group")).at(GroupReicpt.contractAddress);
    //execSync("fisco-solc --abi --bin --overwrite -o "+config.Ouputpath+" TransactionFilterChain.sol");
    var TransactionFilterChainReicpt= await web3sync.rawDeploy(config.account, config.privKey, "TransactionFilterChain");
    var TransactionFilterChain=web3.eth.contract(getAbi("TransactionFilterChain")).at(TransactionFilterChainReicpt.contractAddress);
    
    var CAActionReicpt= await web3sync.rawDeploy(config.account, config.privKey,  "CAAction");
    var CAAction=web3.eth.contract(getAbi("CAAction")).at(CAActionReicpt.contractAddress);
    
    var NodeActionReicpt= await web3sync.rawDeploy(config.account, config.privKey,  "NodeAction");
    var NodeAction=web3.eth.contract(getAbi("NodeAction")).at(NodeActionReicpt.contractAddress);
    
    func = "setSystemAddr(address)";
	params = [SystemProxy.address];
	receipt = await web3sync.sendRawTransaction(config.account, config.privKey, NodeAction.address, func, params);

    var ConfigActionReicpt= await web3sync.rawDeploy(config.account, config.privKey,  "ConfigAction");
    var ConfigAction=web3.eth.contract(getAbi("ConfigAction")).at(ConfigActionReicpt.contractAddress);

    var FileInfoManagerReicept= await web3sync.rawDeploy(config.account, config.privKey,  "FileInfoManager");
    var FileInfoManager=web3.eth.contract(getAbi("FileInfoManager")).at(FileInfoManagerReicept.contractAddress);

    var FileServerReicept= await web3sync.rawDeploy(config.account, config.privKey,  "FileServerManager");
    var FileServerManager=web3.eth.contract(getAbi("FileServerManager")).at(FileServerReicept.contractAddress);

    // add consensuscontrol
    var ConsensusControlMgrReicpt= await web3sync.rawDeploy(config.account, config.privKey,  "ConsensusControlMgr");
    var ConsensusControlMgr=web3.eth.contract(getAbi("ConsensusControlMgr")).at(ConsensusControlMgrReicpt.contractAddress);

    //--添加abi管理合约
    var ContractAbiMgrReicpt= await web3sync.rawDeploy(config.account, config.privKey,  "ContractAbiMgr");
    var ContractAbiMgr=web3.eth.contract(getAbi("ContractAbiMgr")).at(ContractAbiMgrReicpt.contractAddress);

    console.log("register system contract to CNS");

    var func = "addAbi(string,string,string,string,address)";
    //ContractAbiMgr
    var abi = getAbi0("ContractAbiMgr");
    var params  = ["ContractAbiMgr","ContractAbiMgr","",abi,ContractAbiMgrReicpt.contractAddress];
    console.log("contract abi manager ,params =>" + params.toString());
    var receipt = await web3sync.sendRawTransaction(config.account, config.privKey, ContractAbiMgrReicpt.contractAddress, func, params);    

    //SystemProxy
    abi = getAbi0("SystemProxy");
    params  = ["SystemProxy","SystemProxy","",abi,SystemProxyReicpt.contractAddress];
    receipt = await web3sync.sendRawTransaction(config.account, config.privKey, ContractAbiMgrReicpt.contractAddress, func, params);

    //TransactionFilterChain
    abi = getAbi0("TransactionFilterChain");
    params  = ["TransactionFilterChain","TransactionFilterChain","",abi,TransactionFilterChainReicpt.contractAddress];
    receipt = await web3sync.sendRawTransaction(config.account, config.privKey, ContractAbiMgrReicpt.contractAddress, func, params);

    //AuthorityFilter
    abi = getAbi0("AuthorityFilter");
    params  = ["AuthorityFilter","AuthorityFilter","",abi,AuthorityFilterReicpt.contractAddress];
    receipt = await web3sync.sendRawTransaction(config.account, config.privKey, ContractAbiMgrReicpt.contractAddress, func, params);

    //Group
    abi = getAbi0("Group");
    params  = ["Group","Group","",abi,GroupReicpt.contractAddress];
    receipt = await web3sync.sendRawTransaction(config.account, config.privKey, ContractAbiMgrReicpt.contractAddress, func, params);

    //CAAction
    abi = getAbi0("CAAction");
    params  = ["CAAction","CAAction","",abi,CAActionReicpt.contractAddress];
    receipt = await web3sync.sendRawTransaction(config.account, config.privKey, ContractAbiMgrReicpt.contractAddress, func, params);

    //ConfigAction
    abi = getAbi0("ConfigAction");
    params  = ["ConfigAction","ConfigAction","",abi,ConfigActionReicpt.contractAddress];
    receipt = await web3sync.sendRawTransaction(config.account, config.privKey, ContractAbiMgrReicpt.contractAddress, func, params);

    //NodeAction
    abi = getAbi0("NodeAction");
    params  = ["NodeAction","NodeAction","",abi,NodeActionReicpt.contractAddress];
    receipt = await web3sync.sendRawTransaction(config.account, config.privKey, ContractAbiMgrReicpt.contractAddress, func, params);

    // ConsensusControlMgr
    abi = getAbi0("ConsensusControlMgr");
    params  = ["ConsensusControlMgr","ConsensusControlMgr","",abi,ConsensusControlMgr.contractAddress];
    receipt = await web3sync.sendRawTransaction(config.account, config.privKey, ContractAbiMgrReicpt.contractAddress, func, params);

//==================setRoute to systemProxy====================================

    console.log("register TransactionFilterChain.....");
	func = "setRoute(string,address,bool)";
	params = ["TransactionFilterChain", TransactionFilterChain.address, false];
	receipt = await web3sync.sendRawTransaction(config.account, config.privKey, SystemProxy.address, func, params);

    console.log("register ConfigAction.....");
	func = "setRoute(string,address,bool)";
	params = ["ConfigAction", ConfigAction.address, false];
	receipt = await web3sync.sendRawTransaction(config.account, config.privKey, SystemProxy.address, func, params);

    console.log("register NodeAction.....");
	func = "setRoute(string,address,bool)";
	params = ["NodeAction", NodeAction.address, false];
	receipt = await web3sync.sendRawTransaction(config.account, config.privKey, SystemProxy.address, func, params);

    console.log("register CAAction.....");
	func = "setRoute(string,address,bool)";
	params = ["CAAction", CAAction.address, false];
    receipt = await web3sync.sendRawTransaction(config.account, config.privKey, SystemProxy.address, func, params);

    console.log("register ContractAbiMgr.....");
	func = "setRoute(string,address,bool)";
	params = ["ContractAbiMgr", ContractAbiMgr.address, false];
    receipt = await web3sync.sendRawTransaction(config.account, config.privKey, SystemProxy.address, func, params);

    console.log("register ConsensusControlMgr.....");
    func = "setRoute(string,address,bool)";
    params = ["ConsensusControlMgr", ConsensusControlMgr.address, false];
    receipt = await web3sync.sendRawTransaction(config.account, config.privKey, SystemProxy.address, func, params);

    console.log("register FileInfoManager.....");
    func = "setRoute(string,address,bool)";
	params = ["FileInfoManager", FileInfoManager.address, false];
	receipt = await web3sync.sendRawTransaction(config.account, config.privKey, SystemProxy.address, func, params);

	console.log("register FileServerManager.....");
    func = "setRoute(string,address,bool)";
	params = ["FileServerManager", FileServerManager.address, false];
	receipt = await web3sync.sendRawTransaction(config.account, config.privKey, SystemProxy.address, func, params);

	console.log("SystemProxy address :" + SystemProxy.address);
    //console.log(SystemProxy);
	console.log("-----------------SystemProxy route ----------------------");
    var routelength=SystemProxy.getRouteSize();
    for( var i=0;i<routelength;i++){
        var key=SystemProxy.getRouteNameByIndex(i).toString();
        var route=SystemProxy.getRoute(key);
        console.log(i+" )"+ key+"=>"+route[0].toString()+","+route[1].toString()+","+route[2].toString());
    }
    

})();
