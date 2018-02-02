var Web3=require('web3');
var config=require('./config');
var execSync =require('child_process').execSync;
var web3sync = require('./web3sync');
var fs=require('fs');
var sha3 = require("./sha3")

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
console.log("SystemProxy合约地址"+SystemProxy.address);

function getAction(filename){
  var address = SystemProxy.getRoute(filename)[0].toString();
  console.log(filename+"合约地址"+address);
  var contract = web3.eth.contract(getAbi(filename));
  return contract.at(address);  
}

// 获取TransactionFilterChain
var TransactionFilterChain = getAction("TransactionFilterChain");

async function sendTx(group, sol, funcParam) {
  return new Promise((resolve, reject) => {
	var funcDesc = "合约"+sol.address+"函数"+funcParam;
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
    console.log(desc+funcParam+"权限结果："+(ret?"Success":"Fail"));
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
  
  // 添加Filter到TransactionFilterChain
  var AuthorityFilter;
  console.log("添加Filter到TransactionFilterChain");
  // Filter名称、版本、描述
  await web3sync.sendRawTransaction(account, privKey, TransactionFilterChain.address, "addFilterAndInfo(string,string,string)", ["ARPI Filter","1.0","FISCO BCOS权限管理模型"]);
  console.log("添加Filter操作完成，当前Filter个数："+TransactionFilterChain.getFiltersLength());
  for (var i = 0; i < TransactionFilterChain.getFiltersLength(); i++) {
    AuthorityFilter = web3.eth.contract(getAbi("AuthorityFilter")).at(TransactionFilterChain.getFilter(i));
    console.log("Filter[" + i + "]信息：" + AuthorityFilter.address + "," + AuthorityFilter.getInfo());
  }
  
  // 创建四个Group，对应不同权限列表
  var contract = web3.eth.contract(getAbi("Group"));
  var desc;
  
  // Group1
  console.log("\n创建Group1及设置Group描述");
  await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, "setUserToNewGroup(address)", [account]);
  var Group1 = contract.at(AuthorityFilter.getUserGroup(account));
  await web3sync.sendRawTransaction(account, privKey, Group1.address, "setDesc(string)", ["Manager Filter-1.0-链/Filter管理者"]);
  console.log("Group1地址为："+Group1.address+"，描述："+Group1.getDesc());
  // 添加权限
  console.log("Group1添加权限");
  // SystemProxy的getRoute、setRoute、getRouteNameByIndex、getRouteSize
  desc = "Group1设置SystemProxy的";
  await addPermission(Group1, SystemProxy, "getRoute(string)", desc);
  await addPermission(Group1, SystemProxy, "setRoute(string,address,bool)", desc);
  await addPermission(Group1, SystemProxy, "getRouteNameByIndex(uint)", desc);
  await addPermission(Group1, SystemProxy, "getRouteSize()", desc);
  // CAAction的update、updateStatus、get、getIp、getHashsLength、getHash
  var CAAction = getAction("CAAction");
  desc = "Group1设置CAAction的";
  await addPermission(Group1, CAAction, "update(string,string,string,uint256,uint256,uint8,string,string)", desc);
  await addPermission(Group1, CAAction, "updateStatus(string,uint8)", desc);
  await addPermission(Group1, CAAction, "get(string)", desc);
  await addPermission(Group1, CAAction, "getIp(string)", desc);
  await addPermission(Group1, CAAction, "getHashsLength()", desc);
  await addPermission(Group1, CAAction, "getHash(uint)", desc);
  // NodeAction的registerNode、cancelNode、getNode、getNodeIdx、getNodeIdsLength、getNodeId
  var NodeAction = getAction("NodeAction");
  desc = "Group1设置NodeAction的";
  await addPermission(Group1, NodeAction, "registerNode(string,string,uint256,uint8,string,string,string,uint256)", desc);
  await addPermission(Group1, NodeAction, "cancelNode(string)", desc);
  await addPermission(Group1, NodeAction, "getNode(string)", desc);
  await addPermission(Group1, NodeAction, "getNodeIdx(string)", desc);
  await addPermission(Group1, NodeAction, "getNodeIdsLength()", desc);
  await addPermission(Group1, NodeAction, "getNodeId(uint)", desc);
  // ContractAbiMgr的addAbi、updateAbi
  var ContractAbiMgr = getAction("ContractAbiMgr");
  desc = "Group1设置ContractAbiMgr的";
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
  console.log("Group1添加权限完毕，具有以下权限：");
  getPermissionList(Group1);
  
  // Group2
  console.log("\n创建Group2及设置Group描述");
  await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, "setUserToNewGroup(address)", [account]);
  var Group2 = contract.at(AuthorityFilter.getUserGroup(account));
  await web3sync.sendRawTransaction(account, privKey, Group2.address, "setDesc(string)", ["Operations Filter-1.0-运维管理人员"]);
  console.log("Group2地址为："+Group2.address+"，描述："+Group2.getDesc());
  // 启用发布合约
  console.log("启用该角色发布合约功能");
  await web3sync.sendRawTransaction(account, privKey, Group2.address, "setCreate(bool)", [true]);
  console.log("Group2发布合约功能启用结果："+(Group2.getCreate()?"Success":"Fail"));
  // 添加权限
  console.log("Group2添加权限");
  // SystemProxy的getRoute、setRoute、getRouteNameByIndex、getRouteSize
  desc = "Group2设置SystemProxy的";
  await addPermission(Group2, SystemProxy, "getRoute(string)", desc);
  await addPermission(Group2, SystemProxy, "setRoute(string,address,bool)", desc);
  await addPermission(Group2, SystemProxy, "getRouteNameByIndex(uint)", desc);
  await addPermission(Group2, SystemProxy, "getRouteSize()", desc);
  // ConfigAction的set、get
  var ConfigAction = getAction("ConfigAction");
  desc = "Group2设置ConfigAction的";
  await addPermission(Group2, ConfigAction, "set(string,string)", desc); 
  await addPermission(Group2, ConfigAction, "get(string)", desc); 
  console.log("Group2添加权限完毕，具有以下权限：");
  getPermissionList(Group2);
 
  // Group3-1
  console.log("\n创建Group3-1及设置Group描述");
  await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, "setUserToNewGroup(address)", [account]);
  var Group3 = contract.at(AuthorityFilter.getUserGroup(account));
  await web3sync.sendRawTransaction(account, privKey, Group3.address, "setDesc(string)", ["Business Filter-1.0-一类交易人员"]);
  console.log("Group3-1地址为："+Group3.address+"，描述："+Group3.getDesc());
  // 部署合约A
  console.log("部署合约ContractA");
  execSync("fisco-solc --abi --bin --overwrite -o "+config.Ouputpath+" ContractA.sol");
  await web3sync.rawDeploy(account, privKey, "ContractA");
  var ContractA = web3.eth.contract(getAbi("ContractA")).at(getAddress("ContractA"));
  console.log("Group3-1添加权限");
  // 添加权限
  await addPermission(Group3, ContractA, "set1(string)", "Group3设置ContractA的"); 
  console.log("Group3添加权限完毕，具有以下权限：");
  getPermissionList(Group3);
  
  // Group3-2
  console.log("\n创建Group3-2及设置Group描述");
  await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, "setUserToNewGroup(address)", [account]);
  var Group4 = contract.at(AuthorityFilter.getUserGroup(account));
  await web3sync.sendRawTransaction(account, privKey, Group4.address, "setDesc(string)", ["Business Filter-1.0-二类交易人员"]);
  console.log("Group3-2地址为："+Group4.address+"，描述："+Group4.getDesc());
  console.log("Group3-1添加权限");
  // 添加权限
  await addPermission(Group4, ContractA, "set2(string)", "Group4设置ContractA的"); 
  console.log("Group4添加权限完毕，具有以下权限：");
  getPermissionList(Group4);

  // 启动Filter权限管理
  console.log("\n启动Filter权限控制");
  await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, "setEnable(bool)", [true]);
  console.log("该Filter权限控制结果启动："+(AuthorityFilter.getEnable()?"Success":"Fail"));

  console.log('........................ARPI_Model Init End........................');	
})();