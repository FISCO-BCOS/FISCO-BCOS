/**
 * @file: accountManager.js
 * @author: fisco-dev
 * 
 * @date: 2017
 */

var crypto = require('crypto')
var utils = require('../web3lib/utils.js')

function newAccount(pwd)
{
	var privKey = crypto.randomBytes(32);
	var pubKey = utils.privateToPublic(privKey);
	var address = utils.privateToAddress(privKey);
	var str_pri = '0x' + privKey.toString('hex');
	var str_pub = '0x' + pubKey.toString('hex');
	var str_addr = '0x' + address.toString('hex');
	//console.log("privKey : ",privKey," - " , str_pri," \n pubKey : " ,pubKey," - ", str_pub," \n address : ",address," - ",str_addr);
	console.log("privKey : "+ str_pri+"\npubKey : " + str_pub+"\naddress : "+str_addr);
}

newAccount();



