
/**
 * @file: demoOk.js
 * @author: fisco-dev
 * 
 * @date: 2017
 */

var Web3= require('web3');
var config=require('../web3lib/config');
var fs=require('fs');
var execSync =require('child_process').execSync;
var web3sync = require('../web3lib/web3sync');
var BigNumber = require('bignumber.js');


if (typeof web3 !== 'undefined') {
  web3 = new Web3(web3.currentProvider);
} else {
  web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

console.log(config);




var filename="Ok";




var address=fs.readFileSync(config.Ouputpath+filename+'.address','utf-8');
var abi=JSON.parse(fs.readFileSync(config.Ouputpath/*+filename+".sol:"*/+filename+'.abi', 'utf-8'));
var contract = web3.eth.contract(abi);
var instance = contract.at(address);



console.log("contract address:"+address);



(async function(){

  var func = "trans(uint256)";
  var params = [15];
  var receipt = await web3sync.sendRawTransaction(config.account, config.privKey, address, func, params);
  console.log(receipt);

  var num=instance.get();
  console.log("num="+num.toString());

 

})()