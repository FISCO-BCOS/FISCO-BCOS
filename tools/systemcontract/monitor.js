/**
 * @file: monitor.js
 * @author: fisco-dev
 * 
 * @date: 2017
 */

var Web3= require('web3');
var config=require('../web3lib/config');
var fs=require('fs');
var BigNumber = require('bignumber.js');
var web3sync = require('../web3lib/web3sync');
var post=require('../web3lib/post');

if (typeof web3 !== 'undefined') {
  web3 = new Web3(web3.currentProvider);
} else {
  web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

/*
*   npm install --save-dev babel-cli babel-preset-es2017
*   echo '{ "presets": ["es2017"] }' > .babelrc
*   npm install secp256k1 keccak rlp
*/


async function getPeerCount() {
  
  return new Promise((resolve, reject) => {
    web3.net.getPeerCount(function(e,d){     
      resolve(d);
    });
  });
}

async function sleep(timeout) {  
  return new Promise((resolve, reject) => {
    setTimeout(function() {
      resolve();
    }, timeout);
  });
}

(async function() {



while(1){
    console.log("--------------------------------------------------------------");
    console.log("current blocknumber "+await web3sync.getBlockNumber());
    console.log("the number of connected nodes："+ await getPeerCount() );

    
    
    try{
      var peers=await post.post("admin_peers",[]);
      if( peers == '' )
      {
        await sleep(2000);
        continue;
      }
      peers=JSON.parse(peers);
      if( peers.result )
      {
        for( var i=0;i<peers.result.length;i++){
          console.log("...........Node "+i+".........");
          console.log("NodeId:"+peers.result[i].id);
          console.log("Host:"+peers.result[i].network.remoteAddress);
        }
      }
      await sleep(2000);
    }catch(e){
      console.log("admin_peers result parse failed ！"+peers);
    }
  
  
}




})()