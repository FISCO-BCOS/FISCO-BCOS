var Web3 = require('web3');
var config = require('../web3lib/config');
var fs = require('fs');
var execSync = require('child_process').execSync;
var web3sync = require('../web3lib/web3sync');
var BigNumber = require('bignumber.js');
var readline = require('readline');

if (typeof web3 !== 'undefined') {
    web3 = new Web3(web3.currentProvider);
} else {
    web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

function getAbi(contract) {
    var address = fs.readFileSync(config.Ouputpath + contract + '.address', 'utf-8');
    var abi = fs.readFileSync(config.Ouputpath + contract + '.abi', 'utf-8');
    var abi_json = JSON.parse(abi);
    var bin = fs.readFileSync(config.Ouputpath + contract + '.bin', 'utf-8');
    var contract = web3.eth.contract(abi_json);
    var instance = contract.at(address);
    return [address, abi, bin, contract, instance];
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
   // console.log("\t blocknumber = " + blocknumber);
    console.log("\t timestamp   = " + timestamp + " => " + getFormatTime(timestamp));
    console.log("\t abi         = " + abi);
}

function getFormatTime(utc) {
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

//根据contract version拼接cns_name
function toCNSname(contract, version) {

    if (typeof contract != "string") {
        console.log("contract is not string type.");
        process.exit(0);
    }
    var cns_name = contract;
    if((typeof version == "string") && (version != "")) {
        cns_name = (cns_name + "/" + version);
    }

    return cns_name;
}

function cnsParamsLog(cns_name, contract, version, abi, address) {
    console.log("\t cns_name =>" + cns_name);
    console.log("\t contract =>" + contract);
    console.log("\t version  =>" + version);
    console.log("\t address  =>" + address);
    console.log("\t abi      =>" + abi);
}

//参数检查
function cnsParamsCheck(contract, version, abi, address) {
    //contract
    if ((typeof contract != "string") || (contract == "")) {
        console.log("WARNING : invalid contract , type => " + (typeof contract) + " ,value => " + contract);
        process.exit(0);
    }

    //version
    if (typeof version != "string") {
        console.log("WARNING : invalid version , type => " + (typeof contract) + " ,value => " + version);
        process.exit(0);
    }

    //address
    if ((typeof address != "string") || (address == "")) {
        console.log("WARNING : invalid address , type => " + (typeof address) + " ,value => " + address);
        process.exit(0);  
    }

    //abi
    if ((typeof abi != "string") || (abi == "")) {
        console.log("WARNING : invalid abi , type => " + (typeof abi) + " ,value => " + abi);
        process.exit(0); 
    }

    //abi是否是一个json字符串
    var abi_obj = JSON.parse(abi);
    if(abi_obj == null) {
        console.log("WARNING : abi invalid json object, abi_value => " + abi);
        process.exit(0); 
    }
}

//获取cns
function cnsGetBase(contract ,version) {
    var cns_name = toCNSname(contract, version);
    var cns_info = web3sync.callByNameService("ContractAbiMgr", "getAll", "", [cns_name]);
    return cns_info;
}

//注册cns服务,参数: contract(合约名) version(合约版本号) abi(合约abi) address(合约地址)
function cnsAddBase(contract, version, abi, address) {
     //类型检验
     cnsParamsCheck(contract, version, abi, address);

     if (!cnsIsExist(contract, version)) {
        var cns_name = toCNSname(contract, version);
        console.log("cns add operation => cns_name = " + cns_name);
        cnsParamsLog(cns_name, contract, version, abi, address);
        var reciept = web3sync.sendRawTransactionByNameService(config.account, config.privKey, "ContractAbiMgr", "addAbi", "", [cns_name, contract, version, abi, address]);
    } else {
        console.log(" [WARNING] cns add operation failed , ====> contract => " + contract + " version => " + version + " is already exist. you can update it or change its version.");
    }
}

//更新cns
function cnsUpdateBase(contract, version, abi, address) {
    //类型检验
    cnsParamsCheck(contract, version, abi, address);

    if(cnsIsExist(contract, version)) {
        console.log(" ====> Are you sure update the cns of the contract ?(Y/N)");
        process.stdin.on('readable', () => {
            const read = process.stdin.read();
            if (read !== null) {
                var strRead = read.toString().trim();
                if (strRead == "Y") {
                    var cns_name = toCNSname(contract,version);
                    console.log("cns update operation => cns_name = " + cns_name);
                    cnsParamsLog(cns_name, contract, version, abi, address);
                    var reciept = web3sync.sendRawTransactionByNameService(config.account, config.privKey, "ContractAbiMgr", "updateAbi", "", [cns_name, contract, version, abi, address]);
                    process.stdin.emit('end');
                } else {
                    console.log("nothing to be done.");
                    process.stdin.emit('end');
                }
            }
        });
    } else {
        console.log(" [WARNING] cns update operation failed , ====> contract => " + contract + " ,version => " + version + " ,is not exist. If you need , please add it first.");
    }
}

//通过索引获取cns信息
function cnsGetByIndex(index) {
    var cns_info = web3sync.callByNameService("ContractAbiMgr", "getAllByIndex", "", [index]);
    return cns_info;
}

//获取当前cns维护了合约的数目
function cnsGetTotalC() {
    var result = web3sync.callByNameService("ContractAbiMgr", "getAbiCount", "", []);
    var count = parseInt(result[0]);
    return count;
}

//当前合约对应的版本是否已经注册
function cnsIsExist(contract, version) {
    var cns_info = cnsGetBase(contract, version);
    return !((cns_info[0] == "") && (parseInt(cns_info[5]) == 0));
}

//获取CNS中注册的合约的abi addres信息
function cnsGet(contract, version) {
    var cns_info = cnsGetBase(contract, version);
    console.log(" ====> contract => " + contract + " ,version => " + version);
    printAbi(cns_info);
    return;
}

//获取当前调用的cns的列表
function cnsList(is_simple) {
    var count = cnsGetTotalC();
    console.log(" cns total count => " + count);
    //遍历按索引获取
    for (var index = 0; index < count; ++index) {
        var cns_info = cnsGetByIndex(index);
        if((typeof is_simple == "boolean") && (true == is_simple)) { //只打印合约名跟版本号
            console.log("\t" + index + ". contract = " + cns_info[2] + " ,version = " + cns_info[3]);
        } else { 
            console.log(" ====> cns list index = " + index + " <==== ");
            printAbi(cns_info);
        }
    }
    return;
}

//添加cns
function cnsAdd(contract) {
    var cns_info = getAbi(contract);
    var version = "";
    if (cns_info[4]["getVersion"]) {
        version = cns_info[4].getVersion();
    }

    cnsAddBase(contract, version, cns_info[1], cns_info[0]);
}

//更新cns
function cnsUpdate(contract) {
    var cns_info = getAbi(contract);
    var version = "";
    if (cns_info[4]["getVersion"]) {
        version = cns_info[4].getVersion();
    }

    cnsUpdateBase(contract, version, cns_info[1], cns_info[0]);
}

//获取contract对应版本version,被覆盖的cns数目
function cnsGetHistoryTotalC(contract, version) {
    var cns_name = toCNSname(contract, version);
    var result = web3sync.callByNameService("ContractAbiMgr", "getHistoryAbiC", "", [cns_name]);
    var count = parseInt(result[0]);
    return count;
}

//获取contract对应版本version,被覆盖的索引为index的cns信息
function cnsGetHistoryByIndex(contract ,version ,index) {
    var cns_name = toCNSname(contract, version);
    var cns_info = web3sync.callByNameService("ContractAbiMgr", "getHistoryAllByIndex", "", [cns_name, index]);
    return cns_info;
}

//获取contract对应的version被覆盖的cns的列表,is_simple为true表示时只打印合约名与版本号
function cnsHistoryList(contract, version, is_simple) {
    var count = cnsGetHistoryTotalC(contract, version);
    console.log(" cns history total count => " + count);
    //遍历按索引获取
    for (var index = 0; index < count; ++index) {
        var cns_info = cnsGetHistoryByIndex(contract,version,index);
        console.log(" ====> cns history list index = " + index + " <==== ");
        if(typeof is_simple == "boolean" && is_simple) { //只打印合约名跟版本号
            console.log("\t" + index + ". contract = " + cns_info[2] + " ,version = " + cns_info[3])
        } else { 
            printAbi(cns_info);
        }
    }
    return;
}

//将历史被覆盖索引为index的cns为当前的调用cns
function cnsReset(contract, version, index) {
    var cnt = cnsGetHistoryTotalC(contract, version);
    if(index >= cnt) {
        console.log("index out of range , index = " + index + " total cnt = " + cnt);
        process.exit(0);
    }
    var cns_info = cnsGetHistoryByIndex(contract, version, index);
    if((cns_info[0] == "" && parseInt(cns_info[5]) == 0)) {
        console.log("cannot find history cns info by index = " + index);
        process.exit(0);
    }

    cnsUpdateBase(contract, version, cns_info[0], cns_info[1]);
}

exports.cnsAdd=cnsAdd;
exports.cnsGet=cnsGet;
exports.cnsList=cnsList;
exports.cnsUpdate=cnsUpdate;
exports.cnsHistoryList=cnsHistoryList;
exports.cnsReset=cnsReset;
exports.cnsIsExist=cnsIsExist;