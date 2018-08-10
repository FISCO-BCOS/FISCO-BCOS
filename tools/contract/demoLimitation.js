
/**
 * @file: demoLimitation.js
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

var filename="Limitation";
var address=fs.readFileSync(config.Ouputpath+filename+'.address','utf-8');
var abi=JSON.parse(fs.readFileSync(config.Ouputpath/*+filename+".sol:"*/+filename+'.abi', 'utf-8'));
var contract = web3.eth.contract(abi);
var instance = contract.at(address);
console.log("合约address:"+address);

web3.eth.defaultAccount = config.account;

(async function(){
  var func;
  var params;
  var receipt;

  func = "setLimitation(address,uint256)";
  params = [config.account,500];
  receipt = await web3sync.sendRawTransaction(config.account, config.privKey, address, func, params);

  var limit = instance.getLimit(config.account);
  console.log("limit="+limit.toString());
  var spent = instance.getSpent(config.account);
  console.log("spent1="+spent.toString());

  /*func = "addSpent(address,uint256)";
  params = [config.account,300];
  receipt = await web3sync.sendRawTransaction(config.account, config.privKey, address, func, params);
  spent = instance.getSpent(config.account);
  console.log("spent2="+spent.toString());
  
  func = "addSpent(address,uint256)";
  params = [config.account,300];
  receipt = await web3sync.sendRawTransaction(config.account, config.privKey, address, func, params);
  spent = instance.getSpent(config.account);
  console.log("spent3="+spent.toString());
  
  func = "addSpent(address,uint256)";
  params = [config.account,100];
  receipt = await web3sync.sendRawTransaction(config.account, config.privKey, address, func, params);
  spent = instance.getSpent(config.account);
  console.log("spent4="+spent.toString());*/
})()