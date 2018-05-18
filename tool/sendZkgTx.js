/*
 * This file is part of FISCO BCOS.
 * 
 * FISCO BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * FISCO BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with FISCO BCOS.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * 
 * @file: sendZkgTx.js
 * @author: jimmyshi
 * @date: 4th May 2018
 */


var Web3= require('web3');
var config=require('../web3lib/config');
var fs=require('fs');
var execSync =require('child_process').execSync;
var web3sync = require('../web3lib/web3sync');
var BigNumber = require('bignumber');
var JSONBigNumber = require('json-bignum');


if (typeof web3 !== 'undefined') {
	web3 = new Web3(web3.currentProvider);
} else {
	web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

console.log(config);

var filename="TestZkg";
var address=fs.readFileSync(config.Ouputpath+filename+'.address','utf-8');
var abi=JSON.parse(fs.readFileSync(config.Ouputpath/*+filename+".sol:"*/+filename+'.abi', 'utf-8'));

var contract = web3.eth.contract(abi);
var instance = contract.at(address);

async function sleep(timeout) {  
	return new Promise((resolve, reject) => {
			setTimeout(function() {
			resolve();
		}, timeout);
	});
}

var paramsFilePath = process.argv.slice(2)[0];
console.log(paramsFilePath);
var json_params = JSONBigNumber.parse(fs.readFileSync(paramsFilePath, 'utf-8'));
console.log(json_params);
(async function(){
//*
	var params = [
		json_params.s_rts0,   
		json_params.s_rts1,
		json_params.s_sns0,
		json_params.s_sns1,
		json_params.r_cms0,
		json_params.r_cms1,
		json_params.vpub_old.toString(),  
		json_params.vpub_new.toString(),  
		json_params.g,		
		json_params.Gpk,    
		json_params.G_data, 
		json_params.proof
	];
	console.log(params);

var func = "transaction(string,string,string,string,string,string,uint64,uint64,string,string,string,string)"; //不能有空格
var receipt = await web3sync.sendRawTransaction(config.account, config.privKey, address, func, params);
console.log(receipt);

//console.log(eval(receipt).logs);
if (eval(receipt).logs == "" )
{
	console.error("Contract error! Please deploy TestZkg.sol first.");
}
else
{
	var num=instance.get();
	console.log("error_code="+num.toString());
}
})()
