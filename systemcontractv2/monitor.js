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
  console.log("已连接节点数："+ await getPeerCount() );
  var peers=await post.post("admin_peers",[]);
 
  try{
    peers=JSON.parse(peers);
    if( peers.result )
    {
       for( var i=0;i<peers.result.length;i++){
        console.log("...........Node "+i+".........");
        console.log("NodeId:"+peers.result[i].id);
        console.log("Host:"+peers.result[i].network.remoteAddress);
      }
    }
   
    console.log("当前块高"+await web3sync.getBlockNumber());
    console.log("--------------------------------------------------------------");
    await sleep(1500);

  }catch(e){
    console.log("admin_peers 返回解析失败！"+peers);
  }
  
  
}




})()