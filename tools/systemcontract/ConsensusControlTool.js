var Web3= require('web3');
var fs=require('fs');
var execSync =require('child_process').execSync;
var config=require('../web3lib/config');
var web3sync = require('../web3lib/web3sync');
// var cns_tool=require('../tool/cns_ConsensusControlTool.js');

// var contractName = "TestConsensusAction";

/*
*   npm install --save-dev babel-cli babel-preset-es2017
*   echo '{ "presets": ["es2017"] }' > .babelrc
*    npm install secp256k1
*   npm install keccak
*   npm install rlp
*/
if (typeof web3 !== 'undefined') {
	web3 = new Web3(web3.currentProvider);
} else {
	web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

function getAbi(file){
	var abi=JSON.parse(fs.readFileSync(config.Ouputpath+"./"/*+file+".sol:"*/+file+".abi",'utf-8'));
	return abi;
}

function getAbi0(file){
	var abi=fs.readFileSync(config.Ouputpath+"./"/*+file+".sol:"*/+file+".abi",'utf-8');
	return abi;
}

function getAddress(file){
    return (fs.readFileSync(config.Ouputpath+"./"+file+".address",'utf-8'));
}

function help() {
    console.log('Usage: node ConsensusControlTool.js deploy');
    console.log('Usage: node ConsensusControlTool.js turnoff');
    console.log('Usage: node ConsensusControlTool.js list');
}

function zip(arrays) {
    return arrays[0].map(function(_,i) {
    return arrays.map( function(array) { return array[i] } )
    });
}

function hexToString (hex) {
    var string = '';
    for (var i = 0; i < hex.length; i += 2) {
        if (hex.substr(i, 2) == "0x") continue;
        string += String.fromCharCode(parseInt(hex.substr(i, 2), 16));
    }
    return string;
}

(async function() {

    var options = process.argv;
    if( options.length < 3 )
    {
        help();
        return;
    }

    var SystemProxy = web3.eth.contract(getAbi("SystemProxy")).at(getAddress("SystemProxy"));
    var ConsensusControlMgr = null;
    var routelength = SystemProxy.getRouteSize();
    for (var i = 0; i < routelength; i++) {
        var key = SystemProxy.getRouteNameByIndex(i).toString();
        var route = SystemProxy.getRoute(key);
        if ("ConsensusControlMgr" == key) {
            var contract = web3.eth.contract(getAbi("ConsensusControlMgr"));
            ConsensusControlMgr = contract.at(route[0]);
        }
    }
    // console.log(ConsensusControlMgr);

    if (ConsensusControlMgr == null) {
        console.log("ERROR! Could not load 'ConsensusControlMgr' from systemProxy");
        return;
    }

    console.log("ConsensusControlMgr addr is " +  ConsensusControlMgr.address);
    var contract = "ConsensusControl";
    var param2 = options[2];
    switch (param2) {
        case "deploy":
            
            var reicpt = await web3sync.rawDeploy(config.account, config.privKey,  contract, ["address"], [getAddress("SystemProxy")]);
            var addr = reicpt.contractAddress;
            console.log("set addr into ConsensusControlMgr!");

            func = "setAddr(address)";
            params = [addr];
            receipt = await web3sync.sendRawTransaction(config.account, config.privKey, ConsensusControlMgr.address, func, params);

            // ConsensusControlMgr.setAddr(addr);
            var currentaddr = ConsensusControlMgr.getAddr();
            if (currentaddr != 0x00) {
                console.log('deploy consensuscontrol successful! the addr is:' + currentaddr.toString());
            } else {
                console.log('deploy consensuscontrol FAILED!');
            }

            break;
        case "turnoff":
            // ConsensusControlMgr.turnoff();
            func = "turnoff()";
            receipt = await web3sync.sendRawTransaction(config.account, config.privKey, ConsensusControlMgr.address, func);
            // func = "setAddr(address)";
            // params = [0x0];
            // receipt = await web3sync.sendRawTransaction(config.account, config.privKey, ConsensusControlMgr.address, func, params);
            console.log("turnoff ConsensusControlMgr!");
            var addr = ConsensusControlMgr.getAddr();
            if (addr == 0x00) {
                console.log('turnoff consensuscontrol successful!');
            } else {
                console.log('turnoff consensuscontrol FAILED!');
            }
            break;
        case "list":
            var addr = ConsensusControlMgr.getAddr();
            if (addr != 0x00) {
                var tmp =  web3.eth.contract(getAbi(contract))
                var ConsensusControl = tmp.at(addr);
                var ret1 = ConsensusControl.lookupAgencyList();
                console.log("list all agnecy and count info:");
                for (i in ret1) {
                    var agency = ret1[i];
                    var count = ConsensusControl.lookupAgencyCount(agency);  
                    console.log("   agency:" + hexToString(agency) + " | count:" + count.toString(10));
                }
                break;
            } else {
                console.log('Consensuscontrol contract not deploy yet!');
            }
        default:
            help();
            break;
    }



    
    

    


    // var addr = ConsensusControlMgr.getAddr();



    // var contract = "ConsensusControl";
    // var version = "";
    
    
    // console.log("合约部署完成, 联盟共识控制合约地址:" + reicpt.contractAddress);

    // if(cns_tool.cnsIsExist(contract,version)) {
    //     cns_tool.cnsUpdate(contract);
    // } else {
    //     cns_tool.cnsAdd(contract);
    // }

    // func = "setRoute(string,address,bool)";
	// params = ["ConsensusControlAction", reicpt.contractAddress, false];
    
	// receipt = await web3sync.sendRawTransaction(config.account, config.privKey, sysAddr, func, params);


    // SystemProxy=web3.eth.contract(getAbi("SystemProxy")).at(sysAddr);
    // var route=SystemProxy.getRoute("ConsensusControlAction");
    // console.log("=>"+route[0].toString()+","+route[1].toString()+","+route[2].toString());
    // console.log("注册联盟共识合约至系统合约");
})();