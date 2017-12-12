/**
 * @file: abi_name_service_tool.js
 * @author: fisco-dev
 * 
 * @date: 2017
 */

var Web3 = require('web3');
var config = require('./config');
var fs = require('fs');
var execSync = require('child_process').execSync;
var web3sync = require('./web3sync');
var BigNumber = require('bignumber.js');
var readline = require('readline');

if (typeof web3 !== 'undefined') {
    web3 = new Web3(web3.currentProvider);
} else {
    web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

//get abi info by filename
function getAbi(filename) {
    var address = fs.readFileSync(config.Ouputpath + filename + '.address', 'utf-8');
    var abi = fs.readFileSync(config.Ouputpath + filename + '.abi', 'utf-8');
    var abi_json = JSON.parse(abi);
    var bin = fs.readFileSync(config.Ouputpath + filename + '.bin', 'utf-8');
    var contract = web3.eth.contract(abi_json);
    var instance = contract.at(address);
    return [address, abi, bin, contract, instance];
}

function getFormatTime(utc){
    if (typeof utc == "string") {
        utc = parseInt(utc);
    }

    var da    = new Date(utc);
    var year  = da.getFullYear();
    var month = da.getMonth()+1;
    var date  = da.getDate();
    var hour  = da.getHours();
    var min   = da.getMinutes();
    var sec   = da.getSeconds();
    var ms    = da.getMilliseconds();
    var str   = year + "/" + month + "/" + date + " " + hour + ":" + min + ":" + sec + ":" + ms;

    return str;
}

//打印abi具体信息
function printAbi(abi_info) {
    var abi = abi_info[0];
    var addr = abi_info[1];
    var contract = abi_info[2];
    var version = abi_info[3];
    var blocknumber = abi_info[4];
    var timestamp = abi_info[5];

    console.log("\t contract    = " + contract);
    console.log("\t version     = " + version);
    console.log("\t address     = " + addr);
    console.log("\t blocknumber = " + blocknumber);
    console.log("\t timestamp   = " + timestamp + " => " + getFormatTime(timestamp));
    console.log("\t abi         = " + abi);
}

//usage
function Usage() {
    console.log(' abi_name_service_tool.js Usage: ');
    console.log('\t babel-node abi_name_service_tool.js get    contract-name [contract-version]');
    console.log('\t babel-node abi_name_service_tool.js add    contract-name');
    console.log('\t babel-node abi_name_service_tool.js update contract-name');
    console.log('\t babel-node abi_name_service_tool.js list');
    process.exit(0);
}

var options = process.argv;
if (options.length < 3) {
    Usage();
}

console.log('abi_name_service_tool.js  ........................Begin........................');

var mod = options[2];
switch (mod) {
    case "add":
        {//add one abi info
            if (options.length < 4) {
                Usage();
            }

            var contract = options[3];
            var version = "";
            var name = contract;
            var abi_info = getAbi(contract);
            var address = abi_info[0];
            var abi = abi_info[1];

            if (abi_info[4]["getVersion"]) {
                version = abi_info[4].getVersion();
                if (version != "") {
                    name = contract + "/" + version;
                }
            }

            //判断下abi类型
            if (typeof abi != "string" || abi == "") {
                console("WARNING ：invalid abi json ,contract => " + contract + " ,version => " + version + " ,abi => " + abi.toString());
                process.exit(0);
            }

            //调用getAll接口查看当前的合约信息是否已经存在
            var abi_info = web3sync.callByNameService("ContractAbiMgr", "getAll", "", [name]);
            if (abi_info[0] == "" && parseInt(abi_info[5]) == 0) {//不存在,可以添加
                console.log(" NOTE : begin add this contract abi info ==>>> ");
                console.log("\t name     =>" + name);
                console.log("\t contract =>" + contract);
                console.log("\t version  =>" + version);
                console.log("\t address  =>" + address);
                console.log("\t abi      =>" + abi);
                var reciept = web3sync.sendRawTransactionByNameService(config.account, config.privKey, "ContractAbiMgr", "addAbi", "", [name, contract, version, abi, address]);
            } else {//已经存在
                console.log(" current abi info is ==>>> ");
                printAbi(abi_info);
                console.log(" [WARNING] add failed , ====> contract => " + contract + " version => " + version + " is already exist. If you need ,please cover it or update its version!!!");                
            }

            console.log('abi_name_service_tool.js  add mod........................End........................');
            break;
        }
    case "update":
        {//update one abi info
            if (options.length < 4) {
                Usage();
            }

            var contract = options[3];
            var version = "";
            var name = contract;
            var abi_info = getAbi(contract);
            var address = abi_info[0];
            var abi = abi_info[1];

            if (abi_info[4]["getVersion"]) {
                version = abi_info[4].getVersion();
                if (version != "") {
                    name = contract + "/" + version;
                }
            }

            //判断下abi类型
            if (typeof abi != "string" || abi == "") {
                console("WARNING ：invalid abi info ,contract => " + contract + " ,version => " + version + " ,abi = " + abi.toString());
                process.exit(0);
            }

            //调用getAll接口查看当前的合约信息是否已经存在
            var abi_info = web3sync.callByNameService("ContractAbiMgr", "getAll", "", [name]);
            if (abi_info[0] == "" && parseInt(abi_info[5]) == 0) {//不存在,无法更新
                //printAbi(abi_info);
                console.log(" [WARNING] updates failed , ====> contract => " + contract + " ,version => " + version + " ,is not exist. If you need , please use add action first!!!");
            } else {//已经存在,可以更新
                console.log(" ====> current abi info is ==>>> ");
                printAbi(abi_info);
                console.log(" ====> Are you sure you want cover it?(Y)");

                process.stdin.on('readable', () => {
                    const read = process.stdin.read();
                    if (read !== null) {
                        var strRead = read.toString().trim();
                        if (strRead == "Y") {
                            console.log(" NOTE : begin update this contract abi info ==>>> ");
                            console.log("\t name     =>" + name);
                            console.log("\t contract =>" + contract);
                            console.log("\t version  =>" + version);
                            console.log("\t address  =>" + address);
                            console.log("\t abi      =>" + abi);
                            web3sync.sendRawTransactionByNameService(config.account, config.privKey, "ContractAbiMgr", "updateAbi", "", [name, contract, version, abi, address]);
                            console.log('abi_name_service_tool.js  update mod........................End........................');
                            process.stdin.emit('end');
                        } else{
                            console.log("not \'Y\' input and will exit.");
                            process.stdin.emit('end');
                        }
                    }
                });
            }

            break;
        }
    case "list":
        {//list all abi info
            //获取abi数目
            var result = web3sync.callByNameService("ContractAbiMgr", "getAbiCount", "", []);
            var abi_count = parseInt(result[0]);
            console.log(" abi name service count => " + abi_count);
            //遍历按索引获取
            for (var index = 0; index < abi_count; ++index) {
                var abi_info = web3sync.callByNameService("ContractAbiMgr", "getAllByIndex", "", [index]);
                console.log(" ====> index = " + index + " <==== ");
                printAbi(abi_info);
            }

            console.log('abi_name_service_tool.js  list mod........................End........................');
            break;
        }        
    case "get":
        {//get one abi info from ContractMgr
            if (options.length < 4) {
                Usage();
            }

            var contract = options[3];
            var version = "";
            var name = contract;
            if (options.length > 4) {
                version = options[4];
                if (version != "") {
                    name = name + "/" + version;
                }
            }

            var abi_info = web3sync.callByNameService("ContractAbiMgr", "getAll", "", [name]);
            console.log(" ====> contract => " + contract + " ,version => " + version + " ,name => " + name);
            printAbi(abi_info);

            console.log('abi_name_service_tool.js  get mod........................End........................');
            break;
        }
    default:
        {//unkown mod
            console.log(" ===> unkown mod!!!");
            Usage();
            break;
        }
}
