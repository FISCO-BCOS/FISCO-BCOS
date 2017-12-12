/**
 * @file: sendRawTransaction.js
 * @author: fisco-dev
 * 
 * @date: 2017
 */

var fs = require("fs");
var Web3 = require('web3');
var  net = require('net');
var conf = require('./config');
var txm = require('./transactionManager');
var coder = require('./codeUtils');

//init web3
var web3 = new Web3();
var client = new net.Socket();
web3.setProvider(new web3.providers.IpcProvider(conf.ipc_path,client));

//sendRawTransaction
function sendRawTransaction()
{
	web3.eth.getBlockNumber(function(e,d){
		console.log(e+','+d);
		var blocknumber=d+100;

		var call_fun="add(uint256)";
		var types=['uint256'];
		var params=['15'];
		var tx_data = coder.codeTxData(call_fun,types,params);

		console.log('account:'+conf.account);
		
		var postdata = {
			data: tx_data,
			from: conf.account,
			to:conf.contract_addr,
			gas: 1000000,
			randomid:Math.ceil(Math.random()*100000000),
			blockLimit:blocknumber
		}

		var signTX = txm.signTransaction(postdata,conf.privKey,null);
		console.log("signTX : ",signTX);
		web3.eth.sendRawTransaction(signTX, function(err, address) {
			console.log(err,address);
			if (!err)
			{
				console.log("发送交易成功！|",address);
				process.exit();
				return;
			}
			else
			{
				console.log("发送交易失败！",err);
				process.exit();
				return;
			}
		});


	});

	
}


sendRawTransaction();