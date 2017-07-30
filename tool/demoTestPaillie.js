
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

var filename="TestPaillier";
var address=fs.readFileSync(config.Ouputpath+filename+'.address','utf-8');
var abi=JSON.parse(fs.readFileSync(config.Ouputpath/*+filename+".sol:"*/+filename+'.abi', 'utf-8'));
var paillierjson = fs.readFileSync('./java/Pailler/paillier.json', 'utf-8');
var paillie=JSON.parse(fs.readFileSync('./java/Pailler/paillier.json', 'utf-8'));
var contract = web3.eth.contract(abi);
var instance = contract.at(address);

async function sleep(timeout) {  
	return new Promise((resolve, reject) => {
			setTimeout(function() {
			resolve();
		}, timeout);
	});
}

(async function(){
	var put=instance.put();
	console.log("put="+put);

	var testsucceed = 0;
	var testerr = 0;

	for(var key in paillie){
		//console.log("test begin paillie = ", paillie[key]);

		var c1 = paillie[key].c1;
		var c2 = paillie[key].c2;
		var c3 = paillie[key].c3;

		var o1 = paillie[key].o1;
		var o2 = paillie[key].o2;
		var o3 = paillie[key].o3;
		
		var result = instance.add(c1, c2);
		if(c3 === result){
			testsucceed++;
			console.log("test succeed key=", key, ",length=", Object.keys(paillie).length, ",o1=", o1, ",o2=", o2, ",o3=", o3);
		}else{
			testerr++;
			console.log("result=", result, ",c3=", c3, ",c1=", c1, ",c2=", c2);
		}
	}

	console.log("testsucceed=", testsucceed, ",testerr=", testerr, ",length=", Object.keys(paillie).length);
})()
