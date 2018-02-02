var Web3 = require('web3');
var config = require('./config');
var fs = require('fs');
var BigNumber = require('bignumber.js');
var web3sync = require('./web3sync');
var sha3 = require("./sha3")

if (typeof web3 !== 'undefined') {
  web3 = new Web3(web3.currentProvider);
} else {
  web3 = new Web3(new Web3.providers.HttpProvider(config.HttpProvider));
}

function getAbi(file){
	return JSON.parse(fs.readFileSync(config.Ouputpath+"./"+file+".abi",'utf-8'));
}

function getAddress(file){
  return (fs.readFileSync(config.Ouputpath+"./"+file+".address",'utf-8'));
}

function Usage() {
  console.log('The Usage of AuthorityManager.js：');
  console.log('      babel-node AuthorityManager.js Info                                  // 显示Config和系统合约Chain的信息');
  console.log('      babel-node AuthorityManager.js FilterChain addFilter                 // 添加Filter到系统合约Chain，后加Filter名称、版本、描述三个参数');
  console.log('                                                 delFilter                 // 从系统合约Chain删除Filter，后加Filter序号参数');
  console.log('                                                 showFilter                // 显示系统合约Chain的Filter');
  console.log('                                                 resetFilter               // 还原系统合约Chain到初始状态');
  console.log('      babel-node AuthorityManager.js Filter      getFilterStatus           // 显示该Filter权限控制的状态，后加序号参数');
  console.log('                                                 enableFilter              // 启用该Filter权限控制，后加序号参数');
  console.log('                                                 disableFilter             // 关闭该Filter权限控制，后加序号参数');
  console.log('                                                 setUsertoNewGroup         // 给账号添加新角色，后加Filter序号、账号两个参数');
  console.log('                                                 setUsertoExistingGroup    // 给账号添加已有角色，后加Filter序号、账号、已有Group角色三个参数');
  console.log('                                                 listUserGroup             // 显示账号的角色，后加Filter序号、账号两个参数');
  console.log('      babel-node AuthorityManager.js Group       getBlackStatus            // 显示该角色黑名单模式的状态，后加Filter序号、账号两个参数');
  console.log('                                                 enableBlack               // 启用该角色黑名单模式，后加Filter序号、账号两个参数');
  console.log('                                                 disableBlack              // 关闭该角色黑名单模式，后加Filter序号、账号两个参数');
  console.log('                                                 getDeployStatus           // 显示该角色发布合约功能的状态，后加Filter序号、账号两个参数');
  console.log('                                                 enableDeploy              // 启用该角色发布合约功能，后加Filter序号、账号两个参数');
  console.log('                                                 disableDeploy             // 关闭该角色发布合约功能，后加Filter序号、账号两个参数');
  console.log('                                                 addPermission             // 给角色添加权限，后加Filter序号、账号、合约部署地址、函数名(参数类型列表)四个参数');
  console.log('                                                 delPermission             // 删除角色的权限，后加Filter序号、账号、合约部署地址、函数名(参数类型列表)四个参数');
  console.log('                                                 checkPermission           // 检查角色的权限，后加Filter序号、账号、合约部署地址、函数名(参数类型列表)四个参数');
  console.log('                                                 listPermission            // 列出账号/角色的权限，后加Filter序号、账号两个参数');
}

web3.eth.defaultAccount = config.account;

(async function() {
  var options = process.argv;
  if(options.length < 3 || options[2] == "Usage")
  {
	Usage();
	return;
  }
  var param2 = options[2];

  var account = config.account;
  var privKey = config.privKey;
  var SystemProxy = web3.eth.contract(getAbi("SystemProxy")).at(getAddress("SystemProxy"));
  var TransactionFilterChain;
  var AuthorityFilter;
  var Group;
  var routelength = SystemProxy.getRouteSize();
  for (var i = 0; i < routelength; i++) {
    var key = SystemProxy.getRouteNameByIndex(i).toString();
    var route = SystemProxy.getRoute(key);
    if ("TransactionFilterChain" == key) {
      var contract = web3.eth.contract(getAbi("TransactionFilterChain"));
      TransactionFilterChain = contract.at(route[0]);
    }
  }
  
  switch (param2){
    case "Info":
	{
      console.log("Config信息：");
	  console.log(config);
	  console.log("系统合约地址：" + getAddress("SystemProxy"));
      var filterlength = TransactionFilterChain.getFiltersLength();
      console.log("权限过滤链(TransactionFilterChain)地址：" + TransactionFilterChain.address + "，链上权限过滤器(Filter)个数:" + filterlength);
	  break;
	}
	case "FilterChain":
    {
	  var param3 = options[3];
	  switch (param3){
	    case "addFilter":
		{
		  if (options.length != 7) {
            console.log("参数个数错误，请输入Filter名称、版本、描述三个参数，如:babel-node AuthorityManager.js FilterChain addFilter name1 version1 desc1");
          }
		  else {
			console.log("开始addFilter操作：");
			var func = "addFilterAndInfo(string,string,string)";
			var params = [options[4],options[5],options[6]];
			var receipt = await web3sync.sendRawTransaction(account, privKey, TransactionFilterChain.address, func, params);
			console.log("addFilter操作完成，当前Filter个数："+TransactionFilterChain.getFiltersLength());
			var filterAddr = TransactionFilterChain.getFilter(TransactionFilterChain.getFiltersLength() - 1);
			var contract = web3.eth.contract(getAbi("AuthorityFilter"));
			AuthorityFilter = contract.at(filterAddr);
			if (true == AuthorityFilter.getEnable()) {
			  console.log("新添加的Filter权限控制已经启动");
		    }
		    else {
			  console.log("新添加的Filter权限控制尚未启动");
		    }
		  } 
		  break;
		}
		case "delFilter":
		{
		  if (options.length != 5) {
            console.log("参数个数错误，请输入Filter序号参数，如：babel-node AuthorityManager.js FilterChain delFilter 0");
          }
		  else {
			if (TransactionFilterChain.getFiltersLength() <= options[4] || 
			    options[4] < 0) {
			  console.log("序号参数要求为非负数且小于FilterChain长度"+TransactionFilterChain.getFiltersLength()+",选取无效");
			}
			else {
			  console.log("开始delFilter操作：");
			  var func = "delFilter(uint256)";
			  var params = [options[4]];
			  var receipt = await web3sync.sendRawTransaction(account, privKey, TransactionFilterChain.address, func, params);
			  console.log("delFilter操作完成，当前Filter个数："+TransactionFilterChain.getFiltersLength());
			}
		  } 
	      break;
		}
		case "showFilter":
		{
		  var filterlength = TransactionFilterChain.getFiltersLength();
		  console.log("权限过滤链(TransactionFilterChain)地址：" + TransactionFilterChain.address + "，链上权限过滤器(Filter)个数:" + filterlength);
		  for (var j = 0; j < TransactionFilterChain.getFiltersLength(); j++) {
		    var contract = web3.eth.contract(getAbi("AuthorityFilter"));
            var AuthorityFilter = contract.at(TransactionFilterChain.getFilter(j));
			var ret = AuthorityFilter.getInfo();
			var enable;
			if (true == AuthorityFilter.getEnable()) {
			  enable = "该Filter权限控制已经启动";
		    }
		    else {
			  enable = "该Filter权限控制关闭状态";
		    }
			console.log("Filter[" + j + "]信息：" + ret + "," + enable);
	      }
		  break;
		}
		case "resetFilter":
		{
		  console.log("开始还原Chain操作：");
		  var filterlength = TransactionFilterChain.getFiltersLength();
		  for (var j = 0; j < TransactionFilterChain.getFiltersLength(); j++) {
		    var contract = web3.eth.contract(getAbi("AuthorityFilter"));
            var AuthorityFilter = contract.at(TransactionFilterChain.getFilter(j));
			var func = "setEnable(bool)";
		    var params = [false];
		    var receipt = await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, func, params);
	      }
		  for (var j = TransactionFilterChain.getFiltersLength() - 1; ; j--) {
		    var func = "delFilter(uint256)";
			var params = [j];
			var receipt = await web3sync.sendRawTransaction(account, privKey, TransactionFilterChain.address, func, params);
			if (0 == j) break;
		  }
		  console.log("还原Chain操作完成，链上权限过滤器(Filter)个数:" + TransactionFilterChain.getFiltersLength());
		  break;
		}
		default:
	    {
	      console.log("参数错误");
	      Usage();
          break;
	    }
	  }
	  break;
	}
	case "Filter":
	{
	  var param3 = options[3];
	  var AuthorityFilter;
	  if (options.length > 4) {
		if (TransactionFilterChain.getFiltersLength() <= options[4] || 
			options[4] < 0) {
		  console.log("序号参数要求为非负数且小于FilterChain长度"+TransactionFilterChain.getFiltersLength()+",选取无效");
		}
		else {
		  var filterAddr = TransactionFilterChain.getFilter(options[4]);
		  var contract = web3.eth.contract(getAbi("AuthorityFilter"));
		  AuthorityFilter = contract.at(filterAddr);
		}
	  }
	  switch (param3){
		case "getFilterStatus":
		{
		  if (options.length != 5) {
            console.log("参数个数错误，请输入Filter序号参数，如:babel-node AuthorityManager.js Filter getFilterStatus 0");
          }
		  else {
		    if (true == AuthorityFilter.getEnable()) {
			  console.log("该Filter权限控制已经启动");
		    }
		    else {
			  console.log("该Filter权限控制关闭状态");
		    }
		  }
		  break;
		}
		case "enableFilter":
		{
		  if (options.length != 5) {
            console.log("参数个数错误，请输入Filter序号参数，如:babel-node AuthorityManager.js Filter enableFilter 0");
          }
		  else {
		    var func = "setEnable(bool)";
		    var params = [true];
		    var receipt = await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, func, params);
		    var ret = false;
		    ret = AuthorityFilter.getEnable();
		    if (true == ret) {
			  console.log("该Filter权限控制已经启动");
		    }
		    else {
			  console.log("该Filter权限控制关闭状态");
		    }
		  }
		  break;
		}
		case "disableFilter":
		{
		  if (options.length != 5) {
            console.log("参数个数错误，请输入Filter序号参数，如:babel-node AuthorityManager.js Filter disableFilter 0");
          }
		  else {
		    var func = "setEnable(bool)";
		    var params = [false];
		    var receipt = await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, func, params);
		    var ret = true;
		    ret = AuthorityFilter.getEnable();
		    if (true == ret) {
			  console.log("该Filter权限控制已经启动");
		    }
		    else {
			  console.log("该Filter权限控制关闭状态");
		    }
		  }
		  break;
		}
		case "setUsertoNewGroup":
		{
		  if (options.length != 6) {
            console.log("参数个数错误，请输入Filter序号、账号两个参数，如:babel-node AuthorityManager.js Filter setUsertoNewGroup 0 account");
          }
		  else {
			console.log("开始对账号赋予新建的角色操作：");
			var func = "setUserToNewGroup(address)";
			var params = [options[5]];
			var receipt = await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, func, params);
			console.log("账号赋予新建的角色操作完成，角色为："+AuthorityFilter.getUserGroup(options[5]));
		  }
		  break;	
		}
		case "setUsertoExistingGroup":
		{
		  if (options.length != 7) {
            console.log("参数个数错误，请输入Filter序号、账号、已有Group角色三个参数，如:babel-node AuthorityManager.js Filter setUsertoExistingGroup 0 account group");
          }
		  else {
			console.log("开始对账号赋予已有的角色操作：");
			var func = "setUserToGroup(address,address)";
			var params = [options[5],options[6]];
			var receipt = await web3sync.sendRawTransaction(account, privKey, AuthorityFilter.address, func, params);
			console.log("账号赋予已有的角色操作完成");
		  } 	
		  break;	
		}
		case "listUserGroup":
		{
		  if (options.length != 6) {
            console.log("参数个数错误，请输入Filter序号、账号两个参数，如:babel-node AuthorityManager.js Filter listUserGroup 0 account");
          }
		  else {
			  var ret = AuthorityFilter.getUserGroup(options[5]);
			  if (typeof(ret) == "undefined") {
				console.log("该账号尚未设置角色");
			  }
			  else {
				console.log("该账号的角色为："+ret);
			  }
		  } 	
		  break;
		}
		default:
	    {
	      console.log("参数错误");
	      Usage();
          break;
	    }
	  }	
	  break;
	}
	case "Group":
	{
	  var param3 = options[3];
	  var Group;
	  if (TransactionFilterChain.getFiltersLength() <= options[4]) {
		console.log("序号参数要求为非负数且小于FilterChain长度"+TransactionFilterChain.getFiltersLength()+",选取无效");
		break;
	  }
	  else {
	    var filterAddr = TransactionFilterChain.getFilter(options[4]);
		var contract = web3.eth.contract(getAbi("AuthorityFilter"));
		var AuthorityFilter = contract.at(filterAddr);
		var group = AuthorityFilter.getUserGroup(options[5]);
		if (0x0 == group) {
		  console.log("该账号没有设置角色");
		  break;
		}
		else {
		  contract = web3.eth.contract(getAbi("Group"));
		  Group = contract.at(group);
		}
	  }
	  switch (param3){
	    case "getBlackStatus":
		{
		  if (options.length != 6) {
            console.log("参数个数错误，请输入Filter序号、账号两个参数，如:babel-node AuthorityManager.js Group getBlackStatus 0 account");
          }
		  else {
		    if (true == Group.getBlack()) {
			  console.log("该Group黑名单模式已经启用");
		    }
		    else {
			  console.log("该Group黑名单模型尚未启用");
		    }
		  }
		  break;
		}
		case "enableBlack":
		{
		  if (options.length != 6) {
            console.log("参数个数错误，请输入Filter序号、账号两个参数，如:babel-node AuthorityManager.js Group enableBlack 0 account");
          }
		  else {
		    var func = "setBlack(bool)";
		    var params = [true];
		    var receipt = await web3sync.sendRawTransaction(account, privKey, Group.address, func, params);
		    var ret = false;
		    ret = Group.getBlack();
		    if (true == ret) {
			  console.log("该Group黑名单模式已经启用");
		    }
		    else {
			  console.log("该Group黑名单模型尚未启用");
		    }
		  }
		  break;
		}
		case "disableBlack":
		{
		  if (options.length != 6) {
            console.log("参数个数错误，请输入Filter序号、账号两个参数，如:babel-node AuthorityManager.js Group disableBlack 0 account");
          }
		  else {
		    var func = "setBlack(bool)";
		    var params = [false];
		    var receipt = await web3sync.sendRawTransaction(account, privKey, Group.address, func, params);
		    var ret = true;
		    ret = Group.getBlack();
		    if (true == ret) {
			  console.log("该Group黑名单模式已经启用");
		    }
		    else {
			  console.log("该Group黑名单模型尚未启用");
		    }
		  }
		  break;
		}
		case "getDeployStatus":
		{
		  if (options.length != 6) {
            console.log("参数个数错误，请输入Filter序号、账号两个参数，如:babel-node AuthorityManager.js Group getDeployStatus 0 account");
          }
		  else {
		    if (true == Group.getCreate()) {
			  console.log("该Group发布合约功能已经启用");
		    }
		    else {
			  console.log("该Group发布合约功能尚未启用");
		    }
		  }
		  break;
		}
		case "enableDeploy":
		{
		  if (options.length != 6) {
            console.log("参数个数错误，请输入Filter序号、账号两个参数，如:babel-node AuthorityManager.js Group enableDeploy 0 account");
          }
		  else {
		    var func = "setCreate(bool)";
		    var params = [true];
		    var receipt = await web3sync.sendRawTransaction(account, privKey, Group.address, func, params);
		    var ret = false;
		    ret = Group.getCreate();
		    if (true == ret) {
			  console.log("该Group发布合约功能已经启用");
		    }
		    else {
			  console.log("该Group发布合约功能尚未启用");
		    }
		  }
		  break;
		}
		case "disableDeploy":
		{
		  if (options.length != 6) {
            console.log("参数个数错误，请输入Filter序号、账号两个参数，如:babel-node AuthorityManager.js Group disableDeploy 0 account");
          }
		  else {
		    var func = "setCreate(bool)";
		    var params = [false];
		    var receipt = await web3sync.sendRawTransaction(account, privKey, Group.address, func, params);
		    var ret = true;
		    ret = Group.getCreate();
		    if (true == ret) {
			  console.log("该Group发布合约功能已经启用");
		    }
		    else {
			  console.log("该Group发布合约功能尚未启用");
		    }
		  }
		  break;
		}
		case "addPermission":
		{
	      if (options.length != 8) {
            console.log("参数个数错误，请输入Filter序号、账号、合约部署地址、函数名(参数类型列表)四个参数，如:babel-node AuthorityManager.js Group addPermission 0 account A.address fun(string)");
          }
	      else {
			console.log("开始添加权限操作：");
			var funcDesc = "合约"+options[6]+"函数"+options[7];
			var func = "setPermission(address,string,string,bool)";
			var funhash = sha3(options[7]).slice(0, 8);
			var params = [options[6],funhash,funcDesc,true];
			var receipt = await web3sync.sendRawTransaction(account, privKey, Group.address, func, params);
			console.log("添加权限操作完成");
		  } 	
		  break;
		}
		case "delPermission":
		{
		  if (options.length != 8) {
            console.log("参数个数错误，请输入Filter序号、账号、合约部署地址、函数名(参数类型列表)四个参数，如:babel-node AuthorityManager.js Group delPermission 0 account A.address fun(string)");
          }
	      else {
			console.log("开始删除权限操作：");
			var funhash = sha3(options[7]).slice(0, 8);
			if (Group.getPermission(options[6],funhash) == true) {
				var funcDesc = "合约"+options[6]+"函数"+options[7];
				var func = "setPermission(address,string,string,bool)";
				var params = [options[6],funhash,funcDesc,false];
				var receipt = await web3sync.sendRawTransaction(account, privKey, Group.address, func, params);
				console.log("删除权限操作完成");
			}
			else {
				console.log("检查没有该权限");
			}
		  }
		  break;
		}
		case "checkPermission":	
		{
		  if (options.length != 8) {
            console.log("参数个数错误，请输入Filter序号、账号、合约部署地址、函数名(参数类型列表)四个参数，如:babel-node AuthorityManager.js Group checkPermission 0 account A.address fun(string)");
          }
	      else {
			var funhash = sha3(options[7]).slice(0, 8);
			console.log("权限为："+Group.getPermission(options[6],funhash));
		  } 	
		  break;
		}
		case "listPermission":	
		{
		  if (options.length != 6) {
            console.log("参数个数错误，请输入Filter序号、账号、两个参数，如:babel-node AuthorityManager.js Group listPermission 0 account");
          }
	      else {
			console.log("账号"+options[5]+"的权限列表为：");
			var keyCnt = Group.getKeys();
			var idx = 0;
			for (var i = 0; i < keyCnt.length; i++) {
			  var desc = Group.getFuncDescwithPermissionByKey(keyCnt[i]);
			  if (desc.length > 0) {
				console.log("["+idx+"]："+desc);
				idx++;
			  }
			}
		  } 	
		  break;
		}
		default:
		{
	      console.log("Params Error");
	      Usage();
          break;
		}
	  }
	  break;
	}
	default:
	{
	  console.log("参数错误");
	  Usage();
      break;
	}
  }
})();