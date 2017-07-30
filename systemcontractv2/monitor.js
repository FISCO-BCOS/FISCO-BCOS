
var Web3= require('web3');
var config=require('./config');
var fs=require('fs');
var BigNumber = require('bignumber.js');
var web3sync = require('./web3sync');
var post=require('./post');

if (typeof web3 !== 'undefined') {
  web3 = new Web3(web3.currentProvider);
} else {
  web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}



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
    for( var i=0;i<peers.result.length;i++){
      console.log("...........Node "+i+".........");
      console.log("NodeId:"+peers.result[i].id);
      console.log("Host:"+peers.result[i].network.remoteAddress);
    }
    

    console.log("");
    console.log("当前块高"+await web3sync.getBlockNumber());
    console.log("--------------------------------------------------------------");
    await sleep(1500);

  }catch(e){
    console.log("admin_peers 返回解析失败！"+peers);
  }
  
  
}




})()