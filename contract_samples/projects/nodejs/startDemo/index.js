/**
 * @file: index.js
 * @author: fisco-dev
 * 
 * @date: 2017
 */

var Web3 = require('web3');
var config = require('./config');
var fs = require('fs');
var execSync = require('child_process').execSync;
var web3sync = require('./web3sync');
var utils = require('./codeUtils');

if (typeof web3 !== 'undefined') {
    web3 = new Web3(web3.currentProvider);
} else {
    web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

console.log(' ........................Start........................');

var filename = "SimpleStartDemo";
console.log('Soc File :' + filename);

try {
    //用根目录下的FISCO-BCOS的合约编译器fisco-solc进行编译
    execSync("fisco-solc --abi  --bin   --overwrite -o " + config.Ouputpath + "  " + filename + ".sol");

    console.log(filename + '编译成功！');
} catch (e) {

    console.log(filename + '编译失败!' + e);
}

(async function() {

    var Contract = await web3sync.rawDeploy(config.account, config.privKey, filename);
    console.log(filename + "部署成功！")

    var address = fs.readFileSync(config.Ouputpath + filename + '.address', 'utf-8');
    var abi = JSON.parse(fs.readFileSync(config.Ouputpath /*+filename+".sol:"*/ + filename + '.abi', 'utf-8'));
    var contract = web3.eth.contract(abi);
    var instance = contract.at(address);
    console.log("读取" + filename + "合约address:" + address);
    var data = instance.getData();
    console.log("接口调用前读取接口返回:" + data);
    var func = "setData(int256)";
    var params = [10];
    var receipt = await web3sync.sendRawTransaction(config.account, config.privKey, address, func, params);

    console.log("调用更新接口设置data=10" + '(交易哈希：' + receipt.transactionHash + ')');
    data = instance.getData();
    console.log("接口调用后读取接口返回:" + data);
    // print out the log
    var event = instance.AddMsg({}, function(error, result) {
        if (!error) {
            var msg = "AddMsg: " + utils.hex2a(result.args.msg) + " from "
            console.log(msg);
            return;
        } else {
            console.log('it error')
        }
    });
    console.log("==============================End============================================");
})();