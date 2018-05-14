var Web3=require('web3');
var config=require('../web3lib/config');
var execSync =require('child_process').execSync;
var web3sync = require('../web3lib/web3sync');
var fs=require('fs');
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

web3.eth.defaultAccount = config.account;
var account = config.account;
var privKey = config.privKey;
  
var SystemProxy = web3.eth.contract(getAbi("SystemProxy")).at(getAddress("SystemProxy"));
console.log("SystemProxy contract address: "+SystemProxy.address);

function getAction(filename){
  var address = SystemProxy.getRoute(filename)[0].toString();
  console.log(filename+" contract address:"+address);
  var contract = web3.eth.contract(getAbi(filename));
  return contract.at(address);  
}

// 获取TransactionFilterChain
var TransactionFilterChain = getAction("TransactionFilterChain");

async function sendTx(group, sol, funcParam) {
  return new Promise((resolve, reject) => {
    var funcDesc = "Contract:"+sol.address+", Function:"+funcParam;
    var func = "setPermission(address,string,string,bool)";
    var params = [sol.address,sha3(funcParam).slice(0, 8),funcDesc,true];
    var receipt = web3sync.sendRawTransaction(config.account, config.privKey, group.address, func, params);
    resolve(receipt);
  });
}

async function addPermission(group, sol, funcParam, desc) {
  await sendTx(group, sol, funcParam);
  return new Promise((resolve, reject) => {
    var ret = group.getPermission(sol.address,sha3(funcParam).slice(0, 8));
    console.log(desc+funcParam+" authorized："+(ret?"Success":"Fail"));
    resolve(ret);
  });
}

function getPermissionList(group) {
  var keyCnt = group.getKeys();
  var idx = 0;
  for (var i = 0; i < keyCnt.length; i++) {
    var desc = group.getFuncDescwithPermissionByKey(keyCnt[i]);
    if (desc.length > 0) {
      console.log("["+idx+"]："+desc);
      idx++;
    }
  }
}

(async function() {
  console.log('\n........................ARPI_Model Init Start........................');	
  
  // Add Filter to TransactionFilterChain
  var AuthorityFilter;
  console.log("Add Filter To TransactionFilterChain");
  // Filter information: name, version, description
  await web3sync.sendRawTransaction(account, privKey, TransactionFilterChain.address, "addFilterAndInfo(string,string,string)", ["ARPI Filter","1.0","Authority Management Model of FISCO BCOS"]);
  console.log("Add Filter Success，Current Count of Filter："+TransactionFilterChain.getFiltersLength());
  for (var i = 0; i < TransactionFilterChain.getFiltersLength(); i++) {
    AuthorityFilter = web3.eth.contract(getAbi("AuthorityFilter")).at(TransactionFilterChain.getFilter(i));
    console.log("Information of Filter[" + i + "]：" + AuthorityFilter.address + "," + AuthorityFilter.getInfo());
  }
  
  // create four Groups, each related to different authority list
  var contract = web3.eth.contract(getAbi("Group"));
  var desc;
  
  // Group1
  console.log("\nCreate Group1 and Set Description for Group1");
  await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, "setUserToNewGroup(address)", [account]);
  var Group1 = contract.at(AuthorityFilter.getUserGroup(account));
  await web3sync.sendRawTransaction(account, privKey, Group1.address, "setDesc(string)", ["Manager Filter-1.0-/Filter Manager"]);
  console.log("Address of Group1："+Group1.address+", Description:"+Group1.getDesc());
  // add Permissions to Group1
  console.log("Add Permissions to Group1 (getRoute, setRoute, getRouteNameByIndex, getRouteSize)");
  // add getRoute, setRoute, getRouteNameByIndex, getRouteSize of SystemProxy to Group1
  desc = "Set Access Permissions to SystemProxy for Group1, Function:";
  await addPermission(Group1, SystemProxy, "getRoute(string)", desc);
  await addPermission(Group1, SystemProxy, "setRoute(string,address,bool)", desc);
  await addPermission(Group1, SystemProxy, "getRouteNameByIndex(uint)", desc);
  await addPermission(Group1, SystemProxy, "getRouteSize()", desc);
  // add access permissions to update, updateStatus, get, getIp,  getHashsLength, getHash of CAAction to Group1
  var CAAction = getAction("CAAction");
  desc = "Set Access Pemissions to CAAction for Group1, Function:";
  await addPermission(Group1, CAAction, "update(string,string,string,uint256,uint256,uint8,string,string)", desc);
  await addPermission(Group1, CAAction, "updateStatus(string,uint8)", desc);
  await addPermission(Group1, CAAction, "get(string)", desc);
  await addPermission(Group1, CAAction, "getIp(string)", desc);
  await addPermission(Group1, CAAction, "getHashsLength()", desc);
  await addPermission(Group1, CAAction, "getHash(uint)", desc);
  //add access authorities to registerNode, cancelNode, getNode, getNodeIdx, getNodeIdsLength, getNodeId of NodeAction for Group1
  var NodeAction = getAction("NodeAction");
  desc = "Set Access permissions to NodeAction for Group1, Function:";
  await addPermission(Group1, NodeAction, "registerNode(string,string,uint256,uint8,string,string,string,uint256)", desc);
  await addPermission(Group1, NodeAction, "cancelNode(string)", desc);
  await addPermission(Group1, NodeAction, "getNode(string)", desc);
  await addPermission(Group1, NodeAction, "getNodeIdx(string)", desc);
  await addPermission(Group1, NodeAction, "getNodeIdsLength()", desc);
  await addPermission(Group1, NodeAction, "getNodeId(uint)", desc);
  // add access permissions to addAbi and updateAbi of ContractAbiMgr to Group1
  var ContractAbiMgr = getAction("ContractAbiMgr");
  desc = "Set Access permissions to ContractAbiMgr for Group1, Function:";
  await addPermission(Group1, ContractAbiMgr, "addAbi(string,string,string,string,address)", desc);
  await addPermission(Group1, ContractAbiMgr, "updateAbi(string,string,string,string,address)", desc);
  await addPermission(Group1, ContractAbiMgr, "getAddr(string)", desc);
  await addPermission(Group1, ContractAbiMgr, "getAbi(string)", desc);
  await addPermission(Group1, ContractAbiMgr, "getContractName(string)", desc);
  await addPermission(Group1, ContractAbiMgr, "getVersion(string)", desc);
  await addPermission(Group1, ContractAbiMgr, "getBlockNumber(string)", desc);
  await addPermission(Group1, ContractAbiMgr, "getTimeStamp(string)", desc);
  await addPermission(Group1, ContractAbiMgr, "getAbiCount()", desc);
  await addPermission(Group1, ContractAbiMgr, "getAll(string)", desc);
  await addPermission(Group1, ContractAbiMgr, "getAllByIndex(uint256)", desc);
  console.log("Add Permissions for Group1 Succ, Permissions of Group1:");
  getPermissionList(Group1);
  
  // Group2
  console.log("\nCreate Group2 and Set Descriptions for Group2:");
  await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, "setUserToNewGroup(address)", [account]);
  var Group2 = contract.at(AuthorityFilter.getUserGroup(account));
  await web3sync.sendRawTransaction(account, privKey, Group2.address, "setDesc(string)", ["Operations Filter-1.0-Operation-Manager"]);
  console.log("Address of Group2: "+Group2.address+", Description:"+Group2.getDesc());
  // enable create contracts permission
  console.log("enable permission of deploying contracts");
  await web3sync.sendRawTransaction(account, privKey, Group2.address, "setCreate(bool)", [true]);
  console.log("Eable Contract Deployment Permission for Group2:"+(Group2.getCreate()?"Success":"Fail"));
  // add permissions for Group2
  console.log("add Permissions for Group2");
  // add access permissions to getRoute, setRoute, getRouteNameByIndex, getRouteSize of SystemProxy to Group2
  desc = "Set Access Permissions to SystemProxy for Group2, Function:";
  await addPermission(Group2, SystemProxy, "getRoute(string)", desc);
  await addPermission(Group2, SystemProxy, "setRoute(string,address,bool)", desc);
  await addPermission(Group2, SystemProxy, "getRouteNameByIndex(uint)", desc);
  await addPermission(Group2, SystemProxy, "getRouteSize()", desc);
  // add access permissions to set, get of ConfigAction for Group2
  var ConfigAction = getAction("ConfigAction");
  desc = "Set Access Permissions to ConfigAction for Group2, Function:";
  await addPermission(Group2, ConfigAction, "set(string,string)", desc); 
  await addPermission(Group2, ConfigAction, "get(string)", desc); 
  console.log("Add Permissions for Group2 Succ, Permissions of Group2: ");
  getPermissionList(Group2);
 
  // Group3-1
  console.log("\ncreate Group3-1 and set descriptions for Group3-1");
  await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, "setUserToNewGroup(address)", [account]);
  var Group3 = contract.at(AuthorityFilter.getUserGroup(account));
  await web3sync.sendRawTransaction(account, privKey, Group3.address, "setDesc(string)", ["Business Filter-1.0-Trader-of-type-One"]);
  console.log("Adress of Group3-1: "+Group3.address+" Description:"+Group3.getDesc());
  // Deploy ContractA
  console.log("Deploy ContractA");
  execSync("fisco-solc --abi --bin --overwrite -o "+config.Ouputpath+" ContractA.sol");
  await web3sync.rawDeploy(account, privKey, "ContractA");
  var ContractA = web3.eth.contract(getAbi("ContractA")).at(getAddress("ContractA"));
  console.log("Add Permissions for Group3-1");
  // add access permissions to set1 of ContractA for Group3-1
  await addPermission(Group3, ContractA, "set1(string)", "Set access permission to ContractA for Group3, Function:"); 
  console.log("Add Permissions for Group3 Succ, Permissions of Group3:");
  getPermissionList(Group3);
  
  // Group3-2
  console.log("\nCreate Group3-2 and Set Description for Group3-2:");
  await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, "setUserToNewGroup(address)", [account]);
  var Group4 = contract.at(AuthorityFilter.getUserGroup(account));
  await web3sync.sendRawTransaction(account, privKey, Group4.address, "setDesc(string)", ["Business Filter-1.0-Trader-of-type-Two"]);
  console.log("Address of Group3-2:"+Group4.address+", Description: "+Group4.getDesc());
  console.log("Add Permission for Group3-1");
  // add permission
  await addPermission(Group4, ContractA, "set2(string)", "Set access permission to ContractA for Group4, Function:"); 
  console.log("Add Permission for Group4 Succ, Permissions of Group4:");
  getPermissionList(Group4);

  // enable authority management to Filter
  console.log("\nenable authority management to Filter");
  await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, "setEnable(bool)", [true]);
  console.log("Enable Authority Management to Filter:"+(AuthorityFilter.getEnable()?"Success":"Fail"));

  console.log('........................ARPI_Model Init End........................');	
})();