var Web3= require('web3');
var config=require('./config');
var fs=require('fs');
var execSync =require('child_process').execSync;
var web3sync = require('./web3sync');
var BigNumber = require('bignumber.js');


if (typeof web3 !== 'undefined') {
	web3 = new Web3(web3.currentProvider);
} else {
	web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

function docompile(filename){
	try{ 

        execSync("solc --abi  --bin -o " + config.Ouputpath + "  " + filename + ".sol" + " &>/dev/null");
        console.log(filename+'编译成功!' );
        //console.log('编译成功！');
    } catch(e){
        console.log(filename+'编译失败!' + e);
    }

}

(async function() {
   
    console.log("输出路径："+config.Ouputpath);
    await docompile("SystemProxy");
    await docompile("TransactionFilterChain");
    await docompile("AuthorityFilter");
    await docompile("Group");
    await docompile("CAAction");
    await docompile("NodeAction");
    await docompile("ConfigAction");

	

})();
