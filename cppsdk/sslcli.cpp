/**
 * @CopyRight:
 * FISCO-BCOS-CHAIN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS-CHAIN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS-CHAIN.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 tianhe-dev contributors.
 *
 * @brief: a node generate parallel tx by itself
 *
 * @file: main.cpp
 * @author: alvin
 * @date 2018-10-09
 */


#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

//#include "ParamParse.h"
#include "sslcli.h"
#include "CliConfigureInitializer.h"
#include "CLI11.hpp"   

#include <libethcore/ABI.h>
#include <libethcore/Protocol.h>
#include <libethcore/Transaction.h>
#include "ChannelRPCClient.h"
#include <jsonrpccpp/client.h>

#include <unistd.h>
#include <chrono>
#include <ctime>
#include <random>
#include <string>
#include <thread>


#include <iostream>

using namespace jsonrpc;

using namespace std;
using namespace dev;
using namespace channel;
using namespace dev::eth;
//using namespace dev::initializer;




///curl -X POST --data '{"jsonrpc":"2.0","method":"getPbftView","params":[1],"id":1}' http://127.0.0.1:6680
//curl -X POST --data '{"jsonrpc":"2.0","method":"call","params":[1,{"from":"0x6bc952a2e4db9c0c86a368d83e9df0c6ab481102","to":"0xd6f1a71052366dbae2f7ab2d5d5845e77965cf0d","value":"0x1","data":"0x3"}],"id":1}' http://127.0.0.1:6680
// RC1 Request


int  getBlockNumber(Client& c,  boost::property_tree::ptree const& pt )
{
    int blocknumber=0;

    Json::Value addObj =1;
    Json::Value params(Json::arrayValue);
    params.append(addObj);
    //return 0;
    try
    {
        Json::Value ret_val = c.CallMethod("getBlockNumber", params);
		string ret_str = ret_val.asString();
    
        //blocknumber = ret_val.asInt();
		blocknumber = std::stoi(ret_str,nullptr,16);
        std::cout <<" getBlockNumber "<< ret_val <<" convert to int  "<< ret_str <<"  "<< blocknumber<< endl;
        return  blocknumber;
    }
    catch (JsonRpcException& e)
    {
        cerr << e.what() << endl;
        return 0;
    }
}

std::shared_ptr<Transaction>  packRawTx( Client& c, KeyPair const& keyPair, boost::property_tree::ptree const& pt)
{
    int blocknumber= getBlockNumber(c, pt);
    
    u256 value = 0;
    u256 gasPrice = 0;
    u256 gas = 10000000;
    Address dest = Address(0x5002);
    string userA("usera");

    u256 money = 110;
    dev::eth::ContractABI abi;
    //function userSave(string user, uint256 balance) public returns(uint256); solidity contract
    bytes data = abi.abiIn("userSave(string,uint256)", userA, money);  // add 1000000000 to user i

    u256 nonce = u256(utcTime() + rand());

    Transaction::Ptr tx =
            std::make_shared<Transaction>(value, gasPrice, gas, dest, data, nonce);
    auto sig = crypto::Sign(keyPair, tx->hash(WithoutSignature));

    tx->setBlockLimit(u256(blocknumber + 100));
    tx->updateSignature(sig);
    return tx;

}

static void sendRawTxCmd( boost::property_tree::ptree const&  pt )
{
    ///< initialize component
    ChannelRPCClient channelRPCClient;
	channelRPCClient.initChannelRPCClient(pt);

    //ChannelRPCClient   channelclient(rpc_url);
    Client c(channelRPCClient);
	
    auto keyPair = KeyPair::create();
    std::shared_ptr<Transaction>  share_tx =  packRawTx( c, keyPair, pt);
    bytes  tx_bytes;
    share_tx->encode(tx_bytes);	
   
    std::string  tx_str = toHex(tx_bytes);
        
     
    Json::Value addObj =1;
    Json::Value txObj(tx_str);
    Json::Value params(Json::arrayValue);
    params.append(addObj);    
    params.append(txObj);    

    Json::Value ret_val = c.CallMethod("sendRawTransaction", params);
    
    cout<< " sendRawTx ret:  " << ret_val <<std::endl;
}


// thcli -c config.ini
int testSendTx(int argc, const char* argv[])
{
    //Params params = initCommandLine(argc, argv);
    string _path(argv[2]);
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(_path, pt);
    getCliGlobalConfig(pt);
    cout<<"read cfg finished"<<std::endl;
    /// init log
    //getBlockNumber();
    return 0;
}


// thcli -c config.ini
int testgetrule(int argc, const char* argv[])
{
    //Params params = initCommandLine(argc, argv);
    string _path(argv[2]);
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(_path, pt);
    getCliGlobalConfig(pt);
    cout<<"read cfg finished"<<std::endl;
    /// init log

    //getBlockNumber();
    string url(argv[1]);
    //sendCallRpc( rpc_config, url ); // call  select data from  thchain
    return 0;
}


int testmain(int argc,const char* argv[])
{
  try
  {
    if (argc != 3)
    {
      std::cerr << "Usage: client <host> <port>\n";
      return 1;
    }

	string _path(argv[2]);
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(_path, pt);
    getCliGlobalConfig(pt);
	//getBlockNumber(pt);
	sendRawTxCmd(pt);
    //sleep(4);
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}




int main(int argc,const char* argv[])
{
    testmain(argc,argv);
}
