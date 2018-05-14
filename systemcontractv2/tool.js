
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


function getAbi(file){
	var abi=JSON.parse(fs.readFileSync(config.Ouputpath+"./"/*+file+".sol:"*/+file+".abi",'utf-8'));
	//var abi=JSON.parse(fs.readFileSync(config.Ouputpath+"./"+file+".sol:"+file+".abi",'utf-8'));
	return abi;
}

function getAddress(file){
  return (fs.readFileSync(config.Ouputpath+"./"+file+".address",'utf-8'));
  
}

var options = process.argv;
if( options.length < 3 )
{
  console.log('Usage: node tool.js SystemProxy');
  console.log('Usage: node tool.js AuthorityFilter  ');

  console.log('Usage: node tool.js NodeAction all|registerNode|cancelNode '); 
  console.log('Usage: node tool.js CAAction all|update|updateStatus ');
  console.log('Usage: node tool.js ConfigAction get|set ');
}

var filename=options[2];

var func=options[3];
console.log('Soc File :'+options[2]);
console.log('Func :'+options[3]);

console.log("SystemProxy address " + getAddress("SystemProxy"));

var initializer = {from: config.account,randomid:Math.ceil(Math.random()*100000000)};


var SystemProxy=web3.eth.contract(getAbi("SystemProxy")).at(getAddress("SystemProxy"));



function getAction(filename){
    var address=SystemProxy.getRoute(filename)[0].toString();
    console.log(filename+" address "+address);
    var contract = web3.eth.contract(getAbi(filename));
    return contract.at(address);
    
}



switch (filename){
  
  case "ConfigAction":
  {
    switch(func){
      case "get":
      {
        if( options.length< 5 ){
          console.log("please input key");
          break;
        }
        var key=options[4];
        var instance=getAction("ConfigAction");
        var value=instance.get(key,  initializer);
        console.log(key+"="+value[0].toString()+","+value[1].toString());

        break;
      }
      case "set":
      {
        if( options.length< 6 ){
            console.log("please input key，value");
          break;
        }
        var key=options[4];
        var value=options[5];
        
        var instance=getAction("ConfigAction");

        var func = "set(string,string)";
        var params = [key,value];
        var receipt = web3sync.sendRawTransaction(config.account, config.privKey, instance.address, func, params);

        console.log("config :"+key+","+value);
       
        break;
      }
      
      default:
        console.log("Mod Error");
        break;
    }
    break;
  }
  case "SystemProxy":
  {
      console.log("-----------------SystemProxy route----------------------")
    
    

    var routelength=SystemProxy.getRouteSize();
    for( var i=0;i<routelength;i++){
        var key=SystemProxy.getRouteNameByIndex(i).toString();
        var route=SystemProxy.getRoute(key);
        console.log(i+" )"+ key+"=>"+route[0].toString()+","+route[1].toString()+","+route[2].toString());
    }
     
     

    break;
  }
 
  case "AuthorityFilter":
  {
        if( options.length< 6 ){
            console.log("please input account、address、function");
            break;
        }
        console.log("origin :"+options[3]);
        console.log("to :"+options[4]);
        console.log("func :"+options[5]);

      var AuthorityFilter=getAction("AuthorityFilter");
      //process(address origin, address from, address to, string func, string input)

      console.log("authority result :" + AuthorityFilter.process(options[3], "", options[4], options[5], ""));

    break;
  }
  case "NodeAction":
  {
     switch(func){
      
      case "all":
      {
          var instance=getAction("NodeAction");
          var len=instance.getNodeIdsLength(initializer);
          console.log("NodeIdsLength= "+len);
          for( var i=0;i<len;i++){
              console.log("----------node "+i+"---------");
              var id=instance.getNodeId(i);
              console.log("id="+id);
              var node=instance.getNode(id);        
              console.log("ip="+node[0].toString());
              console.log("port="+node[1].toString());
              console.log("category="+node[2].toString());
              console.log("desc="+node[3].toString());
              console.log("CAhash="+node[4].toString());
              console.log("agencyinfo="+node[5].toString());
              console.log("blocknumber="+node[6].toString());
              console.log("Idx="+instance.getNodeIdx(id));
          }
          break;
      }
      case "cancelNode":
      {
       
        
       if( options.length< 5 ){
          console.log("please input node.json");
          break;
        }
        console.log("node.json="+options[4]);
        var node=JSON.parse(fs.readFileSync(options[4],'utf-8'));

        var instance=getAction("NodeAction");
        var func = "cancelNode(string)";
        var params = [node.id];
        var receipt = web3sync.sendRawTransaction(config.account, config.privKey, instance.address, func, params);

        

        break;
      }
      case "registerNode":
      {
        if( options.length< 5 ){
          console.log("please input node.json");
          break;
        }
        console.log("node.json="+options[4]);
        var node=JSON.parse(fs.readFileSync(options[4],'utf-8'));

        var instance=getAction("NodeAction");
        var func = "registerNode(string,string,uint256,uint8,string,string,string,uint256)";
        var params = [node.id,node.ip,node.port,node.category,node.desc,node.CAhash,node.agencyinfo,node.idx]; 
        var receipt = web3sync.sendRawTransaction(config.account, config.privKey, instance.address, func, params);

        //console.log(receipt);

        

        break;
      }
      default:
      console.log("Func Error"); 
      break;
    }
    break;
  }
  case "CAAction":
  {
     switch(func){
      
      case "all":
      {
          var instance=getAction("CAAction");
          var len=instance.getHashsLength(initializer);
          console.log("HashsLength= "+len);
          for( var i=0;i<len;i++){
              console.log("----------CA "+i+"---------");
              var hash=instance.getHash(i);
              console.log("hash="+hash);
              var ca=instance.get(hash);        
             
              console.log("pubkey="+ca[1].toString());
              console.log("orgname="+ca[2].toString());
              console.log("notbefore="+ca[3].toString());
              console.log("notafter="+ca[4].toString());
              console.log("status="+ca[5].toString());
              console.log("blocknumber="+ca[6].toString());
              
              var iplist=instance.getIp(hash);
              console.log("whitelist="+iplist[0].toString());
              console.log("blacklist="+iplist[1].toString());
          }
          break;
      }
      case "update":
      {
       
       if( options.length< 5 ){
          console.log("ca.json");
          break;
        }
        console.log("ca.json="+options[4]);
        var ca=JSON.parse(fs.readFileSync(options[4],'utf-8'));


         var instance=getAction("CAAction");
        var func = "update(string,string,string,uint256,uint256,uint8,string,string)";
        var params = [ca.hash,ca.pubkey,ca.orgname,ca.notbefore,ca.notafter,ca.status,ca.whitelist,ca.blacklist]; 
        var receipt = web3sync.sendRawTransaction(config.account, config.privKey, instance.address, func, params);

        

        break;
      }
      
      case "updateStatus":
      {
        if( options.length< 5 ){
          console.log("please input path ： ca.json");
          break;
        }
        console.log("ca.json="+options[4]);
        var ca=JSON.parse(fs.readFileSync(options[4],'utf-8'));

        var instance=getAction("CAAction");
        var func = "updateStatus(string,uint8)";
        var params = [ca.hash,ca.status]; 
        var receipt = web3sync.sendRawTransaction(config.account, config.privKey, instance.address, func, params);

       

        break;
      }
      default:
      console.log("Func Error"); 
      break;
    }
    break;
  }
  default:
    console.log("Mod Error");
    break;
}
