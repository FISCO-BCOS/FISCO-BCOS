var crypto = require('crypto')
var utils = require('./utils.js')

function newAccount(pwd)
{
	var pKey ="bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad77dd"; 
	var privKey =new Buffer(pKey, 'hex');
	var pubKey = utils.privateToPublic(privKey);
	var address = utils.privateToAddress(privKey);
	var str_pri = '0x' + privKey.toString('hex');
	var str_pub = '0x' + pubKey.toString('hex');
	var str_addr = '0x' + address.toString('hex');
	console.log("privKey : ",privKey," - " , str_pri," | pubKey : " ,pubKey," - ", str_pub," | address : ",address," - ",str_addr);
}

newAccount(123);



