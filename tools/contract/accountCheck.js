/**
 * @file: accountCheck.js
 * @author: fisco-dev
 * 
 * @date: 2017
 */

var crypto = require('crypto')
var utils = require('../web3lib/utils.js')

function newAccount(pwd)
{
	var pKey ="ee57ab3dd1db856a4e97c89030786b87775996489ef019472f545245320206e7"; 
	var privKey =new Buffer(pKey, 'hex');
	var pubKey = utils.privateToPublic(privKey);
	var address = utils.privateToAddress(privKey);
	var str_pri = '0x' + privKey.toString('hex');
	var str_pub = '0x' + pubKey.toString('hex');
	var str_addr = '0x' + address.toString('hex');
	console.log("privKey : ",privKey," - " , str_pri," | pubKey : " ,pubKey," - ", str_pub," | address : ",address," - ",str_addr);
}

newAccount(123);



