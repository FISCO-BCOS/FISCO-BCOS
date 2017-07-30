var Web3= require('web3');
var config=require('./config');
var fs=require('fs');
var execSync =require('child_process').execSync;
var web3sync = require('./web3sync');

if (typeof web3 !== 'undefined') {
  web3 = new Web3(web3.currentProvider);
} else {
  web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}


console.log('deploy.js  ........................Start........................');

// 1 读取文件参数
var options = process.argv;
if( options.length < 3 )
{
	console.log('Usage: node deplay.js xxx.soc ');

}


var filename=options[2];
console.log('Soc File :'+filename);


try{ 
  execSync("solc --abi  --bin   --overwrite -o "+config.Ouputpath+"  "+filename+".sol");
  
  console.log(filename+'编译成功！');
}catch(e){
  
  console.log(filename+'编译失败!'+e);
}

var abi=JSON.parse(fs.readFileSync(config.Ouputpath/*+filename+".sol:"*/+filename+'.abi', 'utf-8'));
var binary=fs.readFileSync(config.Ouputpath+filename+'.bin', 'utf-8');

(async function() {

  var Contract= await web3sync.rawDeploy(config.account, config.privKey,  filename);
  console.log(filename+"部署成功！")

})();

  