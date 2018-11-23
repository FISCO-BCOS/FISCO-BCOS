/**
 * @file: compile.js
 * @author: fisco-dev
 * 
 * @date: 2017
 */

var Web3= require('web3');
var config=require('../web3lib/config');
var fs=require('fs');
var execSync =require('child_process').execSync;
var web3sync = require('../web3lib/web3sync');
var BigNumber = require('bignumber.js');


if (typeof web3 !== 'undefined') {
	web3 = new Web3(web3.currentProvider);
} else {
	web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

function docompile(filename){
	try{ 
        //用FISCO-BCOS的合约编译器fisco-solc进行编译
        execSync("fisco-solc --abi  --bin --overwrite -o " + config.Ouputpath + "  " + filename + ".sol" + " &>/dev/null");
        console.log(filename+'compile success!' );
    } catch(e){
        console.log(filename+'compile failed!' + e);
    }

}

(async function() {
   
    console.log("output path ："+config.Ouputpath);
    await docompile("SystemProxy");
    await docompile("TransactionFilterChain");
    await docompile("AuthorityFilter");
    await docompile("Group");
    await docompile("CAAction");
    await docompile("NodeAction");
    await docompile("ConfigAction");
    await docompile("ContractAbiMgr");
    await docompile("ConsensusControlMgr");

})();
