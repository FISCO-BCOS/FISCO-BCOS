/**
 * @file: sendTransaction.js
 * @author: fisco-dev
 * 
 * @date: 2017
 */

var fs = require("fs");
var Web3 = require('web3');
var  net = require('net');
var conf = require('./config');

//init web3
var web3 = new Web3();
var client = new net.Socket();
web3.setProvider(new web3.providers.IpcProvider(conf.ipc_path,client));

//sendTransaction
function sendTransaction()
{
	var postdata = {
		data: params_data,
		from: conf.account,
		to:conf.contract_addr,
		value:params_value,
		gas: 1000000
	}
	//发送交易
	web3.eth.sendTransaction(postdata, function(err, address) {
		if (!err)
		{
			logger.debug(tx_id + " | 发送交易成功！|",address);
			process.exit();
			return;
		}
		else
		{
			logger.debug(tx_id + "|发送交易失败！",err);
			process.exit();
			return;
		}
	});
}

function callContract()
{
	var test_params = '';
	//调用合约
	var abi_obj = conf.abi_arr;
	var args = test_params;
	console.log(" |args : " + args);
	if( typeof(abi_obj) === 'string' || Object.prototype.toString.call(abi_obj) === '[object String]')
	{
		abi_obj = JSON.parse(abi_obj);
	}
	if( typeof(args) === 'string' || Object.prototype.toString.call(args) === '[object String]')
	{
		args = JSON.parse(args);
	}
	console.log(typeof(abi_obj) + " | " + Object.prototype.toString.call(abi_obj));
	console.log(typeof(args) + " | " + Object.prototype.toString.call(args));
	if(typeof(abi_obj) !== 'object' || Object.prototype.toString.call(abi_obj) !== '[object Array]' || typeof(args) !== 'object' || Object.prototype.toString.call(args) !== '[object Array]')
	{
		console.log("参数格式不合法！ abi_obj : " + JSON.stringify(abi_obj) +" | args : " + JSON.stringify(args));
		process.exit();
		return;
	}
	else
	{
		console.log(" |args : " + args + " | " + JSON.stringify(args));
		var breaked = false;
		var abi_obj_size = abi_obj.length;
		abi_obj.forEach(function (obj,index) 
		{
			if(breaked) return;;
			console.log("obj : " + JSON.stringify(obj) + " | " + abi_obj_size + " | " + index);
			if(obj.constant !== undefined && obj.name !== undefined && obj.name === fun)
			{
				console.log("call fun : " + obj.name);
				try{
					// creation of contract object
					var MyContract = web3.eth.contract(abi_obj);
					// initiate contract for an address
					if( conf.contract_addr === '' || conf.contract_addr === undefined)
					{
						console.log("未传入合约地址！");
						process.exit();
						return;
					}
					var myContractInstance = MyContract.at(conf.contract_addr);
					var f = myContractInstance[fun];
					if(obj.constant)
					{
						console.log(fun+" is a constant function, we should call it.");
						if(typeof(f) === 'function')
						{
							try
							{
								var call_param = args;
								function call_back(err,ret_data){
									console.log("err : " + err + " | ret_data : " + JSON.stringify(ret_data));
									if( !err )
									{
										console.log(f + " result : " + JSON.stringify(ret_data));
										process.exit();
										return;
									}
									else
									{
										console.log(" 调用合约失败");
										process.exit();
										return;
									}
								}
								call_param.push(call_back);
								console.log("f:"+f + " | args : " + JSON.stringify(call_param));
								f.apply(myContractInstance, call_param);
							}
							catch(e)
							{
								console.log("exception:"+e.message);
								process.exit();
								return;
							}
						}
						else
						{
							console.log(f + " 不是合约函数！");
							process.exit();
							return;
						}
					}
					else
					{
						console.log(fun+" is not a constant function, we should send a Transaction.");
						if(typeof(f) === 'function')
						{
							try
							{
								var call_param = args;
								var fromparam = {
									from:conf.account,
									gas:100000000
								};
								//gas: 1000000000
								call_param.push(fromparam);
								function call_back(err,ret_data)
								{
									console.log("err : " + err + " | ret_data : " + JSON.stringify(ret_data));
									if( !err )
									{
										console.log(f + " result : " + JSON.stringify(ret_data));
										process.exit();
										return;
									}
									else
									{
										console.log("调用合约失败！");
										process.exit();
										return;
									}
								}
								call_param.push(call_back);
								console.log("call_param : " + JSON.stringify(call_param));
								f.apply(myContractInstance, call_param);
							}
							catch(e)
							{
								console.log("调用合约失败！ | exception:",e);
								process.exit();
								return;
							}					
						}
						else
						{
							console.log(f + " 不是合约函数！");
							process.exit();
							return;
						}
					}
					breaked = true;
				}
				catch(ex)
				{
					console.log("exception:",ex);
					process.exit();
					return;
				}
			}
			if( parseInt(abi_obj_size) === (parseInt(index)+1) && !breaked )
			{
				console.log("合约未包含该函数！" + fun);
				process.exit();
				return;
			}		 
		});
	}
}

