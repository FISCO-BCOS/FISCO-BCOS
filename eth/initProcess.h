/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file main.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 * EVM Execution tool.
 */
#include <fstream>
#include <iostream>
#include <ctime>
#include <boost/algorithm/string.hpp>
#include <libdevcore/CommonIO.h>
#include <libdevcore/RLP.h>
#include <libdevcore/SHA3.h>
#include <libethcore/SealEngine.h>
#include <libethereum/Block.h>
#include <libethereum/Executive.h>
#include <libethereum/ChainParams.h>
#include <libethashseal/GenesisInfo.h>
#include <libethashseal/Ethash.h>
#include <libethcore/CommonJS.h>
#include <libevm/VM.h>
#include <libevm/VMFactory.h>
#include <libdevcore/CommonIO.h>
#include <json_spirit/JsonSpiritHeaders.h>
#include <stdlib.h>
#include <stdio.h>



using namespace std;
using namespace dev;
using namespace eth;
namespace js = json_spirit;


static const int64_t MaxBlockGasLimit = ChainParams(genesisInfo(Network::HomesteadTest)).u256Param("maxGasLimit").convert_to<int64_t>();

void initHelp()
{
	cout
		<< "Usage ethvm <options> [trace|stats|output|test] (<file>|-)" << endl
		<< "Transaction options:" << endl
		<< "    --value <n>  Transaction should transfer the <n> wei (default: 0)." << endl
		<< "    --gas <n>    Transaction should be given <n> gas (default: block gas limit)." << endl
		<< "    --gas-limit <n>  Block gas limit (default: " << MaxBlockGasLimit << ")." << endl
		<< "    --gas-price <n>  Transaction's gas price' should be <n> (default: 0)." << endl
		<< "    --sender <a>  Transaction sender should be <a> (default: 0000...0069)." << endl
		<< "    --origin <a>  Transaction origin should be <a> (default: 0000...0069)." << endl
		<< "    --input <d>   Transaction code should be <d>" << endl
		<< "    --code <d>    Contract code <d>. Makes transaction a call to this contract" << endl
#if ETH_EVMJIT
		<< endl
		<< "VM options:" << endl
		<< "    --vm <vm-kind>  Select VM. Options are: interpreter, jit, smart. (default: interpreter)" << endl
#endif // ETH_EVMJIT
		<< "Network options:" << endl
		<< "    --network Olympic|Frontier|Morden|Homestead" << endl
		<< endl
		<< "Options for trace:" << endl
		<< "    --flat  Minimal whitespace in the JSON." << endl
		<< "    --mnemonics  Show instruction mnemonics in the trace (non-standard)." << endl
		<< endl
		<< "General options:" << endl
		<< "    -V,--version  Show the version and exit." << endl
		<< "    -h,--help  Show this help message and exit." << endl;
	exit(0);
}

void initVersion()
{
	cout << "ethvm version " << dev::Version << endl;
	cout << "By Gav Wood, 2015." << endl;
	cout << "Build: " << DEV_QUOTED(ETH_BUILD_PLATFORM) << "/" << DEV_QUOTED(ETH_BUILD_TYPE) << endl;
	exit(0);
}

/*
The equivalent of setlocale(LC_ALL, â€œCâ€) is called before any user code is run.
If the user has an invalid environment setting then it is possible for the call
to set locale to fail, so there are only two possible actions, the first is to
throw a runtime exception and cause the program to quit (default behaviour),
or the second is to modify the environment to something sensible (least
surprising behaviour).

The follow code produces the least surprising behaviour. It will use the user
specified default locale if it is valid, and if not then it will modify the
environment the process is running in to use a sensible default. This also means
that users do not need to install language packs for their OS.
*/
void initSetDefaultOrCLocale()
{
#if __unix__
	if (!std::setlocale(LC_ALL, ""))
	{
		setenv("LC_ALL", "C", 1);
	}
#endif
}

enum class Mode
{
	Trace,
	Statistics,
	OutputOnly,

	/// Test mode -- output information needed for test verification and
	/// benchmarking. The execution is not introspected not to degrade
	/// performance.
	Test
};

std::vector<std::string> jsonToVector(std::string const& _json)
{
    std::vector<std::string> ret;
	
	js::mValue val;
	json_spirit::read_string(_json, val);
	js::mObject obj = val.get_obj();	
	std::string privilege = json_spirit::write_string(obj["privilege"], false);
	//cout << "privilege=" << privilege;
	
	json_spirit::mValue arrayVal;
	json_spirit::read_string(privilege, arrayVal);
	json_spirit::mArray arrayObj = arrayVal.get_array();

    for (auto action: arrayObj)
    {
    	ret.push_back(action.get_obj()["action"].get_str());
		//cout << "action=" << action.get_obj()["action"].get_str();
    }
    return ret;
}

std::string string2val(const std::string &str)
{
	h256 staticPartLength(32);
	h256 dynamicPartLength(str.length());

	string ret = toHex(str);
	
	int len = ((ret.length() + 31)/32)*32 - ret.length();
	ret.append(len, '0');

	return staticPartLength.hex() + dynamicPartLength.hex() + ret;
}



void initProcess(int argc, char** argv)
{
	initSetDefaultOrCLocale();
	string inputFile;
	Mode mode = Mode::Statistics;
	VMKind vmKind = VMKind::Interpreter;
	State state(0);
	Address sender;
	Address origin = Address(69);
	u256 value = 0;
	u256 gas = MaxBlockGasLimit;
	u256 gasPrice = 0;
	//bool styledJson = true;
	StandardTrace st;
	EnvInfo envInfo;
	Network networkName = Network::HomesteadTest;
	envInfo.setGasLimit(MaxBlockGasLimit);
	bytes data;
	bytes code;
	Address innerAddr;
	std::string strConfigJSON;
	std::string strBlockChainName;  //Çø¿éÁ´Ãû³Æ
	std::string strCommonName;//»ú¹¹CN
	std::string strid;//»ú¹¹id
	std::string strName;//»ú¹¹Ãû³Æ
	std::string strIpList;//ipÁÐ±í
	std::string strFileIdList;//caÖ¤Êéid
	std::vector<std::string> vecPrivilegeJSON;
	std::vector<std::string> vecDepartMentJSON;
	ChainParams cp;
	
	Ethash::init();
	NoProof::init();

	for (int i = 1; i < argc; ++i)
	{
		string arg = argv[i];
		if(arg == "init")
		{
			continue;
		}
		else if(arg == "--inner" && i + 1 < argc)
		{
			strConfigJSON = contentsString(argv[++i]);
			
			cp = cp.loadInnerInfo(strConfigJSON);
			//cout << "cp.size=" << cp.innerInfo.size();
			
		}
		else if(arg == "--action" && i + 1 < argc)
		{
			std::string strPrivilege;
			strPrivilege = contentsString(argv[++i]);
			vecPrivilegeJSON = jsonToVector(strPrivilege);
			
			//cout << "size = " << vecPrivilegeJSON.size();
			//std::vector<std::string>::iterator iter;  
    		//for (iter=vecPrivilegeJSON.begin();iter!=vecPrivilegeJSON.end();iter++) 
    		//{
			//	cout << "action=" << *iter;
				
			//}
			//return;
			//cp = cp.loadInnerInfo(strConfigJSON);
			//cout << "cp.size=" << cp.innerInfo.size();
			
		}
		else if ((arg == "-a" || arg == "--address" || arg == "--author") && i + 1 < argc)
		{
			try 
			{
				sender = h160(fromHex(argv[++i], WhenError::Throw));
			}
			catch(...)
			{
				cerr << "address argument error: " << arg << '\n';
			}
		}
		else if(arg == "--block-chain-name" && i + 1 < argc)//department's 
		{
			strBlockChainName = argv[++i];
		}
		else if(arg == "--common-name" && i + 1 < argc)//department's 
		{
			strCommonName = argv[++i];
		}
		else if(arg == "--id" && i + 1 < argc)//department's 
		{
			strid = argv[++i];
		}
		else if(arg == "--name" && i + 1 < argc)//department's 
		{
			strName = argv[++i];
		}
		else if(arg == "--ip-list" && i + 1 < argc)//ip's 
		{
			strIpList = argv[++i];
		}
		else if(arg == "--file-id-list" && i + 1 < argc)//ca's id 
		{
			strFileIdList = argv[++i];
		}
		else
		{
			cerr << "Unknown argument: " << arg << '\n';
		}
	}


	VMFactory::setKind(vmKind);
	
	//Address contractDestination("1122334455667788990011223344556677889900");	
	std::unordered_map<Address, Account> map;
	bytes output;
	//sunordered_map<byte, pair<unsigned, bigint>> counts;
	for(std::map<Address,std::string>:: iterator it = cp.innerInfo.begin(); it != cp.innerInfo.end(); it++)
	{
		code = fromHex(it->second);
		Account account(0,0);
		account.setNewCode(bytes{code});
		map[it->first] = account;	
	}

	std::string strOut = "{\"accounts\":{";
	state.populateFrom(map);
	unique_ptr<SealEngineFace> se(ChainParams(genesisInfo(networkName)).createSealEngine());

		
	int i = 0;
	for(std::map<Address,std::string>:: iterator it = cp.innerInfo.begin(); it != cp.innerInfo.end(); it++)
	{
		//cout << "address=" << it->first.hex() << "i = "<< i;
		Transaction t;
				
		t = Transaction(value, gasPrice, gas, it->first, data, i);
		i++;
		
		state.addBalance(sender, value);		
		Executive executive(state, envInfo, *se);
		ExecutionResult res;
		executive.setResultRecipient(res);
		t.forceSender(sender);

		unordered_map<byte, pair<unsigned, bigint>> counts;
		unsigned total = 0;
		bigint memTotal;
		auto onOp = [&](uint64_t step, uint64_t PC, Instruction inst, bigint m, bigint gasCost, bigint gas, VM* vm, ExtVMFace const* extVM) {
			if (mode == Mode::Statistics)
			{
				counts[(byte)inst].first++;
				counts[(byte)inst].second += gasCost;
				total++;
				if (m > 0)
					memTotal = m;
			}
			else if (mode == Mode::Trace)
				st(step, PC, inst, m, gasCost, gas, vm, extVM);
		};
		

		executive.initialize(t);
		if (!code.empty())
			executive.call(it->first, sender, value, gasPrice, &data, gas);
		else
			executive.create(sender, value, gasPrice, gas, &data, origin);

		Timer timer;
		if ((mode == Mode::Statistics || mode == Mode::Trace) && vmKind == VMKind::Interpreter)
			executive.go(onOp);
		else
			executive.go();
		executive.finalize();
		output = std::move(res.output);
		state.setNewCode(it->first, bytes{output});

	}	
 
	//insert inner contrator data
	for (std::vector<std::string>::iterator iter=vecPrivilegeJSON.begin();iter!=vecPrivilegeJSON.end();iter++)
	{
		//the address of actionmanage
		Address innerContract("0000000000000000000000000000000000000015");
		h256 InnerTo(dev::padded(innerContract.asBytes(),32));
		
		Transaction t;
		t = Transaction(value, gasPrice, gas, innerContract, data, i);
		++i;
		
		//the called function
		h256 funsha = sha3(bytesConstRef("insert(string)"));
    	std::string strSha = funsha.hex();
			
		std::string strParam = string2val(*iter);
		std::string strData = std::string("0x") + strSha.substr(0,8) + strParam;
		bytes data = jsToBytes(strData);
		//cout << "strData=" << strData;
		
		Executive e(state, envInfo, *se);
		ExecutionResult res;
		e.setResultRecipient(res);
		t.forceSender(sender);

		unordered_map<byte, pair<unsigned, bigint>> counts;
		unsigned total = 0;
		bigint memTotal;
		auto onOp = [&](uint64_t step, uint64_t PC, Instruction inst, bigint m, bigint gasCost, bigint gas, VM* vm, ExtVMFace const* extVM) {
			if (mode == Mode::Statistics)
			{
				counts[(byte)inst].first++;
				counts[(byte)inst].second += gasCost;
				total++;
				if (m > 0)
					memTotal = m;
			}
			else if (mode == Mode::Trace)
				st(step, PC, inst, m, gasCost, gas, vm, extVM);
		};

		//cout << "run here";
		e.initialize(t);
		if (!e.call(innerContract, sender, value, gasPrice, bytesConstRef(&data), gas))
			e.go(onOp);
		e.finalize();
		//output = std::move(res.output);
		//cout << "output=" << output << endl;		
	}

	{
		std::string strParam1 = "{\"id\":\"admin\",\"name\":";
		strParam1 = strParam1 + "\"" + strBlockChainName + "\"" + ",\"serialNum\":0,\"parentId\":\"\",\"description\":\"blockchain plat\",\"commonName\":";
		strParam1 = strParam1 + "\"Juzhen\"" + ",\"stateName\":\"guangdong\",\"countryName\":\"china\",\"email\":\"test@126.com\",\"type\":0,\"admin\":";
			strParam1 = strParam1 + "\"" + sender.hex() +"\"" + ",\"ipList\":" + "[]" + ",\"roleIdList\":[],\"fileIdList\":"  + strFileIdList +  "}";
		//cout << "strParam1=" << strParam1;

		std::string strParam2 = std::string("{\"id\":") + "\"" + strid + "\"" + ",\"name\":";
		strParam2 = strParam2 + "\"" + strName + "\"" + ",\"serialNum\":1,\"parentId\":\"admin\",\"description\":\"blockchain plat\",\"commonName\":";
		strParam2 = strParam2 + "\"" + strCommonName + "\"" + ",\"stateName\":\"guangdong\",\"countryName\":\"china\",\"email\":\"test@126.com\",\"type\":1,\"admin\":";
			strParam2 = strParam2 + "\"" + sender.hex() +"\"" + ",\"ipList\":" + strIpList + ",\"roleIdList\":[],\"fileIdList\":"  + strFileIdList + "}";
		//cout << "strParam2=" << strParam2;
			//"[\"192.168.1.1\",\"10.71.1.1\",\"127.0.0.1\"]	
		vecDepartMentJSON.push_back(strParam1);
		vecDepartMentJSON.push_back(strParam2);

    	for (std::vector<std::string>::iterator iter=vecDepartMentJSON.begin();iter!=vecDepartMentJSON.end();iter++) 
    	{
			//the address of departmentmanage
			Address innerContract("0000000000000000000000000000000000000014");
			h256 InnerTo(dev::padded(innerContract.asBytes(),32));
			
	
			
			//the called function
			h256 funsha = sha3(bytesConstRef("insert(string)"));
	    	std::string strSha = funsha.hex();
				
			std::string strParam = string2val(*iter);
			std::string strData = std::string("0x") + strSha.substr(0,8) + strParam;
			bytes data = jsToBytes(strData);
			//cout << "strData=" << strData;

			Transaction t;
			t = Transaction(value, gasPrice, gas, innerContract, data, i);
			++i;
			
			Executive e(state, envInfo, *se);
			ExecutionResult res;
			e.setResultRecipient(res);
			t.forceSender(sender);

			unordered_map<byte, pair<unsigned, bigint>> counts;
			unsigned total = 0;
			bigint memTotal;
			auto onOp = [&](uint64_t step, uint64_t PC, Instruction inst, bigint m, bigint gasCost, bigint gas, VM* vm, ExtVMFace const* extVM) {
				if (mode == Mode::Statistics)
				{
					counts[(byte)inst].first++;
					counts[(byte)inst].second += gasCost;
					total++;
					if (m > 0)
						memTotal = m;
				}
				else if (mode == Mode::Trace)
					st(step, PC, inst, m, gasCost, gas, vm, extVM);
			};

			//cout << "run here";
			e.initialize(t);
			if (!e.call(innerContract, sender, value, gasPrice, bytesConstRef(&data), gas))
				e.go(onOp);
			e.finalize();
			//output = std::move(res.output);
		}
		
	}

	
	if (mode == Mode::Statistics)
	{	
		int j = 0;
		std::unordered_map<Address, Account> state_cache;
		state_cache = (*state.getStorageCash());
		for(std::unordered_map<Address, Account>::const_iterator itc = state_cache.begin(); itc != state_cache.end(); itc++)
		{
			if(0 == itc->second.code().size())
			{
				continue;
			}
			if(0 == j)
			{
				strOut = strOut + "\"" + (itc->first).hex() + "\": {\"code\":\"0x" + toHex(itc->second.code()) + "\"";
			}
			else
			{
				strOut = strOut + ",\"" + (itc->first).hex() + "\": {\"code\":\"0x" + toHex(itc->second.code()) + "\"";
			}

			int k = 0;
			int ksize = itc->second.storageOverlay().size() - 1;
			//cout << "ksize=" << ksize + 1;
			std::unordered_map<u256, u256> accountCash = itc->second.storageOverlay();
			for(std::unordered_map<u256, u256>::const_iterator iter = accountCash.begin(); iter != accountCash.end(); iter++)
			{
				if(0 == k)
				{
					strOut = strOut + ",\"storage\": {\"" + dev::toJS(toCompactBigEndian(iter->first, 32)) + "\":" + "\"" + dev::toJS(toCompactBigEndian(iter->second, 32)) + "\"";
				}
				else
				{
					strOut = strOut + ",\"" + dev::toJS(toCompactBigEndian(iter->first, 32)) + "\":" + "\"" + dev::toJS(toCompactBigEndian(iter->second, 32)) + "\"";
				}
				

				if(k == ksize)
				{
					strOut = strOut + "}";
				}

				k++;
			}
			j++;
			strOut += "}";
			
		}
				
		strOut += "}";					
	}
	
	strOut += "}";
	//cout << "strOut" << strOut;
	writeFile("./config/innerConfig.json",strOut);
	//cout << "strOut=" << strOut;
}
