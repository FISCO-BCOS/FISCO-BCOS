var Web3=require('web3');
var config=require('../web3lib/config');
var execSync =require('child_process').execSync;
var web3sync = require('../web3lib/web3sync');
var fs=require('fs');
var sha3 = require("../web3lib/sha3")
const coder = require('../web3lib/codeUtils');

if (typeof web3 !== 'undefined') {
  web3 = new Web3(web3.currentProvider);
} else {
  web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}
web3.eth.defaultAccount = config.account;

function getTxData(func, params)
{
  var r = /^\w+\((.+)\)$/g.exec(func);
  var types = r[1].split(',');
  var tx_data = coder.codeTxData(func,types,params).toString();
  return tx_data;
}

function Query(param)
{
  console.log("Param:");
  console.log(JSON.parse(param));
  var result = web3sync.callUTXO([param]);
  console.log("Result:");
  var JRet = JSON.parse(result);
  if (typeof(JRet.data)!='undefined')
  {
    JRet.data = JSON.parse(JRet.data);
  }
  console.log(JRet);
}

function QueryPaged(utxotype, inparam1, inparam2)
{
  var idx = 0;
  var param;
  if (utxotype == "GetVault")
  {
    param = "{\"utxotype\":6,\"queryparams\":[{\"account\":\""+inparam1+"\",\"value\":\""+inparam2+"\",\"cnt\":\"6\"}]}";
  }
  else if (utxotype == "SelectTokens")
  {
    param = "{\"utxotype\":7,\"queryparams\":[{\"account\":\""+inparam1+"\",\"value\":\""+inparam2+"\"}]}";
  }
  else if (utxotype == "TokenTracking")
  {
    param = "{\"utxotype\":8,\"queryparams\":[{\"tokenkey\":\""+inparam1+"\",\"cnt\":\"3\"}]}";
  }
  console.log("Param["+idx.toString()+"]:");
  console.log(JSON.parse(param));
  var result = web3sync.callUTXO([param]);
  console.log("Result["+idx.toString()+"]:");
  var JRet = JSON.parse(result);
  console.log(JRet);
  if (JRet["code"] != 0)
  {
    return;
  }
  else 
  {
    var total = JRet["total"];
    var end = JRet["end"];
    var begin = 0;
    if (total == 0)
    {
      //console.log(JRet);
    }
    else {
      while (end < total - 1)
      {
        idx++;
        begin = end + 1;
        if (utxotype == "GetVault")
        {
          param = "{\"utxotype\":6,\"queryparams\":[{\"account\":\""+inparam1+"\",\"value\":\""+inparam2+"\",\"begin\":\""+ begin.toString() +"\",\"cnt\":\"6\"}]}";
        }
        else if (utxotype == "SelectTokens")
        {
          param = "{\"utxotype\":7,\"queryparams\":[{\"account\":\""+inparam1+"\",\"value\":\""+inparam2+"\",\"begin\":\""+ begin.toString() +"\",\"cnt\":\"6\"}]}";
        }
        else if (utxotype == "TokenTracking")
        {
          param = "{\"utxotype\":8,\"queryparams\":[{\"tokenkey\":\""+inparam1+"\",\"begin\":\""+ begin.toString() +"\",\"cnt\":\"6\"}]}";
        }
        console.log("Param["+idx.toString()+"]:");
        console.log(JSON.parse(param));
        var result = web3sync.callUTXO([param]);
        console.log("Result["+idx.toString()+"]:");
        var JRet = JSON.parse(result);
        console.log(JRet);
        if (JRet["code"] != 0)
        {
          return;
        }
        else 
        {
          end = JRet["end"];
        }
      }
    }
  }
  return;
}

var options = process.argv;
var utxotype = options[2].toString();
var inparam1;
var inparam2;
if (options.length > 3)
{
  inparam1 = options[3].toString();
}
if (options.length > 4)
{
  inparam2 = options[4].toString();
}

async function initTokens()
{
  // 基础交易
  var param = "{\"utxotype\":1,\"txout\":[{\"to\":\"0x3ca576d469d7aa0244071d27eb33c5629753593e\",\"value\":\"100\",\"checktype\":\"P2PK\"}]}";

  /*
  // 限制Token的使用账号为特定账号
  var initContractAddr = "0x7dc38c5e144cbbb4cd6e8a65091da52a78d584f5";                    // 模板合约地址，用于创建实例合约，创建Token传入
  // tx_data为调用模板合约的函数说明及参数，创建Token传入
  var initFunc = "newUserCheckContract(address[])";                                       // 模板合约中的函数说明（含参数，没有空格）
  var initParams = [[config.account]];                                                    // 调用函数传入的参数列表
  var init_tx_data = getTxData(initFunc, initParams);                                     // ABI序列化
  console.log("InitData:"+init_tx_data);
  var param = "{\"utxotype\":1,\"txout\":[{\"to\":\"0x3ca576d469d7aa0244071d27eb33c5629753593e\",\"value\":\"100\",\"checktype\":\"P2PK\",\"initcontract\":\""+initContractAddr+"\",\"initfuncandparams\":\""+init_tx_data+"\",\"oridetail\":\"Only userd by config.account\"}]}";
  */

  /*
  // 限制某一账号的日转账限额
  var validationContractAddr = "0x3dbac83f7050e377a9205fed1301ae4239fa48e1";              // 通用合约地址，创建Token传入
  var param = "{\"utxotype\":1,\"txout\":[{\"to\":\"0x3ca576d469d7aa0244071d27eb33c5629753593e\",\"value\":\"100\",\"checktype\":\"P2PK\",\"validationcontract\":\""+validationContractAddr+"\",\"oridetail\":\"Account with Limitation per day\"}]}";
  */

  console.log("Param:");
  console.log(JSON.parse(param));
  var receipt = await web3sync.sendUTXOTransaction(config.account, config.privKey, [param]);
  console.log("Receipt:");
  console.log(receipt);
}

async function sendSelectedTokens()
{
  // 基础交易
  var Token1 = "0xf84c92c047d2f1f02333cf3f400b17c3dc5b1152556987f043f0c48c33596638_0";
  var shaSendTo = "0x"+sha3("0x3ca576d469d7aa0244071d27eb33c5629753593e").toString();
  var param = "{\"utxotype\":2,\"txin\":[{\"tokenkey\":\""+Token1+"\"}],\"txout\":[{\"to\":\"0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3\",\"value\":\"60\",\"checktype\":\"P2PK\"},{\"to\":\""+shaSendTo+"\",\"value\":\"40\",\"checktype\":\"P2PKH\"}]}";

  /*
  // 限制Token的使用账号为特定账号
  var Token1 = "0xf60b1f4e5ff6ebd2b55a8214d8a979b56912b48d1d48033dff64bd445899e24b_0";
  var checkFunc = "check(address)";
  var checkParams = [config.account];
  var check_tx_data = getTxData(checkFunc, checkParams);
  console.log("CheckData:"+check_tx_data);
  var shaSendTo = "0x"+sha3("0x3ca576d469d7aa0244071d27eb33c5629753593e").toString();
  var param = "{\"utxotype\":2,\"txin\":[{\"tokenkey\":\""+Token1+"\",\"callfuncandparams\":\""+check_tx_data+"\"}],\"txout\":[{\"to\":\"0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3\",\"value\":\"60\",\"checktype\":\"P2PK\"},{\"to\":\""+shaSendTo+"\",\"value\":\"40\",\"checktype\":\"P2PKH\"}]}";
  */

  /*
  // 限制某一账号的日转账限额
  var Token1 = "0xd77d4b655c6f3a7870ef66676b1375249f1e5ff34045374a1fc244f2fdf09be6_0";
  var checkFunc = "checkSpent(address,uint256)";
  var checkParams = [config.account,100];
  var check_tx_data = getTxData(checkFunc, checkParams);
  var updateFunc = "addSpent(address,uint256)";
  var updateParams = [config.account,100];
  var update_tx_data = getTxData(updateFunc, updateParams);
  var shaSendTo = "0x"+sha3("0x3ca576d469d7aa0244071d27eb33c5629753593e").toString();
  var param = "{\"utxotype\":2,\"txin\":[{\"tokenkey\":\""+Token1+"\",\"callfuncandparams\":\""+check_tx_data+"\",\"exefuncandparams\":\""+update_tx_data+"\"}],\"txout\":[{\"to\":\"0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3\",\"value\":\"60\",\"checktype\":\"P2PK\"},{\"to\":\""+shaSendTo+"\",\"value\":\"40\",\"checktype\":\"P2PKH\"}]}";
  */

  console.log("Param:");
  console.log(JSON.parse(param));
  var receipt = await web3sync.sendUTXOTransaction(config.account, config.privKey, [param]);
  console.log("Receipt:");
  console.log(receipt);
}

(async function() {
  /*
  InValid = 0,          // 无效
  InitTokens,           // 铸币
  SendSelectedTokens,   // 支付
  RegisterAccount,      // 添加账号
  GetToken,             // 显示Token内容
  GetTx,                // 显示Token交易内容
  GetVault,             // 显示一账号下钱包内容
  SelectTokens,         // 找用于支付的Token
  TokenTracking,        // Token回溯
  GetBalance,           // 查询余额
  */  
  
  var param;
  if (utxotype == "InitTokens")
  {
    await initTokens();
  }
  else if (utxotype == "SendSelectedTokens")
  {
    await sendSelectedTokens();
  }
  else if (utxotype == "RegisterAccount" || 
           utxotype == "GetToken" || 
           utxotype == "GetTx" || 
           utxotype == "GetVault" || 
           utxotype == "SelectTokens" || 
           utxotype == "TokenTracking" || 
           utxotype == "GetBalance" || 
           utxotype == "ShowAll")
  {
    if (utxotype == "RegisterAccount")
    {
      param = "{\"utxotype\":3,\"queryparams\":[{\"account\":\""+inparam1+"\"}]}";
      Query(param);
    }
    else if (utxotype == "GetToken")
    {
      param = "{\"utxotype\":4,\"queryparams\":[{\"tokenkey\":\""+inparam1+"\"}]}";
      Query(param);
    }
    else if (utxotype == "GetTx")
    {
      param = "{\"utxotype\":5,\"queryparams\":[{\"txkey\":\""+inparam1+"\"}]}";
      Query(param);
    }
    else if (utxotype == "GetVault" || 
             utxotype == "SelectTokens" || 
             utxotype == "TokenTracking")
    {
      QueryPaged(utxotype, inparam1, inparam2);
    }
    else if (utxotype == "GetBalance")
    {
      param = "{\"utxotype\":9,\"queryparams\":[{\"account\":\""+inparam1+"\"}]}";
      Query(param);
    }
  }
  else 
  {
    console.log("Invalid Method");
  }
})();