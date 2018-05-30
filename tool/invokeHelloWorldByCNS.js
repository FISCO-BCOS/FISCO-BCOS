
/**
 * @file: invokeHelloWorldByCNS.js
 * @author: sanguohot
 * 
 * @date: 2018
 */

let Web3= require('web3');
let config=require('../web3lib/config');
let request = require('request');

if (typeof web3 !== 'undefined') {
  web3 = new Web3(web3.currentProvider);
} else {
  web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}
console.log(config);
let contractName = "HelloWorld";
let contractVersion = "";
let contractFunction = "get";
let contractParams = [];
let data = {
    "jsonrpc":"2.0",
    "method":"eth_call",
    "params":[{"data":{"contract":contractName,"version":contractVersion,"func":contractFunction,"params":contractParams}},"latest"],
    "id":1
}
request.post({
    url : config.HttpProvider,
    json : true,
    body : data
}, (err,response,body) => {
    if (err){
        return console.error(err);
    }
    if(body && body.error && body.error.code){
        return console.error("CNS invoke error!  Server responded with:",body.error.message);
    }
    console.log("CNS invoke successful!  Server responded with:",body);
})
