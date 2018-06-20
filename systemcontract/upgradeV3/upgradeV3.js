var Web3= require('web3');
var config=require('../web3lib/config');
var fs=require('fs');
var BigNumber = require('bignumber.js');
var web3sync = require('../web3lib/web3sync');

if (typeof web3 !== 'undefined') {
  web3 = new Web3(web3.currentProvider);
} else {
  web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

console.log(config);

function getAbi(file){
	var abi=JSON.parse(fs.readFileSync(config.Ouputpath+"./"+file+".abi",'utf-8'));
	return abi;
}

function getAddress(file){
  return (fs.readFileSync(config.Ouputpath+"./"+file+".address",'utf-8'));
  
}

console.log("系统合约地址"+getAddress("SystemProxy"));

var initializer = {from: config.account,randomid:Math.ceil(Math.random()*100000000)};
var SystemProxy=web3.eth.contract(getAbi("SystemProxy")).at(getAddress("SystemProxy"));

function getAction(filename){
    var address=SystemProxy.getRoute(filename)[0].toString();
    //console.log(filename+"合约地址"+address);
    var contract = web3.eth.contract(getAbi(filename));
    return contract.at(address);
}
function printSystemProxy(){
  var routelength=SystemProxy.getRouteSize();
  for( var i=0;i<routelength;i++){
      var key=SystemProxy.getRouteNameByIndex(i).toString();
      var route=SystemProxy.getRoute(key);
      console.log(i+" )"+ key+"=>"+route[0].toString()+","+route[1].toString()+","+route[2].toString());

      if( "TransactionFilterChain" == key ){
          var contract = web3.eth.contract(getAbi("TransactionFilterChain"));
          var instance = contract.at(route[0]);
          var filterlength=instance.getFiltersLength();
          for( var j=0;j<filterlength;j++){
              var filter=instance.getFilter(j);
              contract = web3.eth.contract(getAbi("TransactionFilterBase"));
              instance = contract.at(filter);
              var name= instance.name();
              var version=instance.version();
              console.log("       "+name+"=>"+version+","+filter);
          }
      }
  }
}
(async function() {
      
    var CAActionReicpt= await web3sync.rawDeploy(config.account, config.privKey,  "CAActionV3");
    var CAAction=web3.eth.contract(getAbi("CAAction")).at(CAActionReicpt.contractAddress);
    var NodeActionReicpt= await web3sync.rawDeploy(config.account, config.privKey,  "NodeActionV3");
    var NodeAction=web3.eth.contract(getAbi("NodeAction")).at(NodeActionReicpt.contractAddress);

    //console.log("升级前系统代理路由表.....");
    //printSystemProxy();
    console.log("注册新NodeAction.....");
    func = "setRoute(string,address,bool)";
    params = ["NodeAction", NodeAction.address, false];
    receipt = await web3sync.sendRawTransaction(config.account, config.privKey, SystemProxy.address, func, params);
  
    console.log("注册新CAAction.....");
    func = "setRoute(string,address,bool)";
    params = ["CAAction", CAAction.address, false];
    receipt = await web3sync.sendRawTransaction(config.account, config.privKey, SystemProxy.address, func, params);
    
    console.log("升级后系统代理路由表.....");
    printSystemProxy();
})();
