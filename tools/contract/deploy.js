/**
 * @file: deploy.js
 * @author: fisco-dev
 * 
 * @date: 2017
 */

var Web3= require('web3');
var config=require('../web3lib/config');
var fs=require('fs');
var execSync =require('child_process').execSync;
var web3sync = require('../web3lib/web3sync');
var cns_tool = require("./cns_tool.js");

if (typeof web3 !== 'undefined') {
  web3 = new Web3(web3.currentProvider);
} else {
  web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}


console.log('deploy.js  ........................Start........................');

// 1 read file parameter
var options = process.argv;
if( options.length < 3 )
{
	console.log('Usage: node deplay.js xxx.soc ');
	process.exit(0)

}


var filename=options[2];
console.log('Soc File :'+filename);

(async function() {

  var Contract= await web3sync.rawDeploy(config.account, config.privKey,  filename);
  console.log(filename+" deploy success!");
  try {
	 //try to add cns service, catch exceptions
	 cns_tool.cnsAdd(filename);   
  } catch(e){
  //console.log(filename+'cns add failed , e = '+e);
}

})();

  
