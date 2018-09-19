var Web3 = require('web3');
var config = require('../web3lib/config');
var fs = require('fs');
var execSync = require('child_process').execSync;
var web3sync = require('../web3lib/web3sync');
var BigNumber = require('bignumber.js');
var readline = require('readline');
var cns_tool=require('./cns_tool.js');

//usage
function Usage() {
    console.log(' cns_manager.js Usage: ');
    console.log('\t babel-node cns_manager.js get    contractName [contractVersion]');
    console.log('\t babel-node cns_manager.js add    contractName');
    console.log('\t babel-node cns_manager.js update contractName');
    console.log('\t babel-node cns_manager.js list [simple]');
    console.log('\t babel-node cns_manager.js historylist contractName [contractVersion] [simple]');
    console.log('\t babel-node cns_manager.js reset contractName [contractVersion] index');
    process.exit(0);
}

var options = process.argv;
if (options.length < 3) {
    Usage();
}

console.log('cns_manager.js  ........................Begin........................');

var mod = options[2];
switch (mod) {
    case "add":
        {//add 
            if (options.length < 4) {
                Usage();
            }

            var contract = options[3];
            cns_tool.cnsAdd(contract);
            break;
        }
    case "update":
        {//update 
            if (options.length < 4) {
                Usage();
            }

            var contract = options[3];
            cns_tool.cnsUpdate(contract);
            break;
        }
    case "list":
        {//list 
            var is_simple = false;
            if (options.length >= 4 && options[3] == "simple") {
                is_simple = true;
            }
            cns_tool.cnsList(is_simple);
            break;
        }        
    case "get":
        {//get one abi info from ContractMgr
            if (options.length < 4) {
                Usage();
            }

            var contract = options[3];
            var version = "";
            if (options.length > 4) {
                version = options[4];
            }
            cns_tool.cnsGet(contract,version);
            break;
        }
    case "historylist":
    {
        if (options.length < 4) {
            Usage();
        }
        var contract = options[3];
        var version = "";
        var is_simple = false;
        if(options.length > 5) {
            if(options[5] == "simple") {
                is_simple = true;
            }
            version = options[4];
        } else if (options.length > 4) {
            if(options[4] == "simple") {
                is_simple = true;
            } else {
                version = options[4];
            }
        }
       
        cns_tool.cnsHistoryList(contract,version,is_simple);
        break;
    }
    case "reset":
    {
        if (options.length < 5) {
            Usage();
        }

        var contract = options[3];
        var version = "";
        var index = 0;
        if (options.length > 5) {
            index = parseInt(options[5]);
            version = options[4];
        } else {
            index = parseInt(options[4]);
        }
        cns_tool.cnsReset(contract, version, index);
        break;
    }
    default:
        {//unkown mod
            console.log("unkown mod.");
            Usage();
            break;
        }
}

//console.log('cns_manager.js  ........................End........................');

