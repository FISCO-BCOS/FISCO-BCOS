/**
 * @file: ViewChangeCheck.js
 * @author: fisco-dev
 * 
 * @date: 2017
 */

var Web3= require('web3');
var config=require('../web3lib/config');
var fs=require('fs');
var BigNumber = require('bignumber.js');


var web3sync = require('../web3lib/web3sync');

if (typeof web3 !== 'undefined') {
  web3 = new Web3(web3.currentProvider);
} else {
  web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

console.log(config);

console.log('ViewChangeCheck.js  ........................Start........................');


async function sleep(timeout) {  
  return new Promise((resolve, reject) => {
    setTimeout(function() {
      resolve();
    }, timeout);
  });
}


var account=config.account;//web3.eth.accounts[0];

(async function() {
    var start=1119000;

        
        var end=await web3sync.getBlockNumber();
        console.log('start='+start+',end blocknumber='+end);
        var lasttime=0;
        var two=0;
        var three=0;
        var four=0;
        var sum=end-start;

        for(var i=start;i<end ;i++){
            if( i%100 == 0 )
                console.log(i+".......");
            
            await sleep(10);
            var block=await web3.eth.getBlock( i);

            if( lasttime != 0 ){
                var diff=block.timestamp - lasttime ;
                if( diff > 2000 && diff <3000){
                    console.log("issue block more than 2s "+i);
                    two++;
                }
                else if( diff > 3000 && diff <4000){
                    console.log("issue block more than 3s "+i);
                    three++;
                }
                else if( diff > 4000) {
                    console.log("issue block more than 4s "+i);
                    four++;
                }
            }
            lasttime=block.timestamp;
        }
        console.log("issue block more than 2s :"+((two*100)/sum).toFixed(2)+"%");
        console.log("issue block more than 3s :"+((three*100)/sum).toFixed(2)+"%");
        console.log("issue block more than 4s :"+((four*100)/sum).toFixed(2)+"%");



        
   
    
})();






  
