
var Web3= require('web3');
var config=require('./config');
var fs=require('fs');
var execSync =require('child_process').execSync;
var web3sync = require('./web3sync');
var BigNumber = require('bignumber.js');


if (typeof web3 !== 'undefined') {
  web3 = new Web3(web3.currentProvider);
} else {
  web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

console.log(config);




var filename="Testout";




var address=fs.readFileSync(config.Ouputpath+filename+'.address','utf-8');
var abi=JSON.parse(fs.readFileSync(config.Ouputpath/*+filename+".sol:"*/+filename+'.abi', 'utf-8'));
var contract = web3.eth.contract(abi);
var instance = contract.at(address);



console.log(filename+"合约address:"+address);

async function sleep(timeout) {  
  return new Promise((resolve, reject) => {
    setTimeout(function() {
      resolve();
    }, timeout);
  });
}

(async function(){

var i=0;
//while(true)
{
  sleep(1000);
  
	/*
   var name=instance.test();
  console.log("接口调用前读取接口返回:"+name.toString());
  i++;
  var func = "write(string)";
  var params = ["HelloWorld!"+i];
  var receipt = await web3sync.sendRawTransaction(config.account, config.privKey, address, func, params);



  console.log("调用更新接口设置name=\"HelloWorld\""+'(交易哈希：'+receipt.transactionHash+')');
*/
  //var name=instance.test();
  var put=instance.put();
  console.log("put="+put);
  //console.log("接口调用后读取接口返回:"+name.toString());

}

 

})()
