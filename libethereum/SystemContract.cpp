/*
	This file is part of FISCO-BCOS.

	FISCO-BCOS is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	FISCO-BCOS is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: SystemContract.cpp
 * @author: toxotguo
 * @date: 2018
 */

#include <libdevcore/CommonJS.h>
#include <libdevcore/Common.h>
#include <libdevcore/easylog.h>
#include <abi/ContractAbiMgr.h>

#include "SystemContract.h"
#include <libdevcore/SHA3.h>
#include <netinet/in.h>
#include <netdb.h>
using namespace dev::eth;


void SystemContract::updateSystemContract(std::shared_ptr<Block> block)
{


    Timer t;

    LOG(TRACE) << "SystemContract::updateSystemContract m_systemproxyaddress=" << toString(m_systemproxyaddress) << ",number=" << m_client->blockChain().number() << "," << m_client->blockChain().info();
   
    DEV_WRITE_GUARDED(m_blocklock)
    {

        m_tempblock = block;
        m_tempblock->clearCurrentBytes();
        m_tempblock->setEvmEventLog(true);
        LOG(TRACE) << "SystemContract::updateSystemContract blocknumber=" << m_tempblock->info().number();
    }


    bool configChange = false, nodeChange = false, caChange = false, routeChange = false, coChange = false;
    std::vector<string> configChangeArg;
    configChangeArg.push_back("");
    std::vector<string> nodeChangeArg;
    nodeChangeArg.push_back("");
    std::vector<string> caChangeArg;
    std::vector<string> routeChangeArg;
    routeChangeArg.push_back("");


    bytes nodehash1 = sha3("cancelNode(string)").ref().cropped(0, 4).toBytes();
    bytes nodehash2 = sha3("registerNode(string,string,uint256,uint8,string,string,string,uint256)").ref().cropped(0, 4).toBytes();

    bytes cahash1 = sha3("updateStatus(string,uint8)").ref().cropped(0, 4).toBytes();
    bytes cahash2 = sha3("update(string,string,string,uint256,uint256,uint8,string,string)").ref().cropped(0, 4).toBytes();

    bytes cohash1 = sha3("addAbi(string,string,string,string,address)").ref().cropped(0, 4).toBytes();
    bytes cohash2 = sha3("updateAbi(string,string,string,string,address)").ref().cropped(0, 4).toBytes();

    Address configaction;
    Address nodeAction;
    Address caAction;
    Address contractAbiMgr;
    DEV_READ_GUARDED(m_lockroute)
    {
        configaction = getRoute("ConfigAction");
        nodeAction = getRoute("NodeAction");
        caAction = getRoute("CAAction");
        contractAbiMgr = getRoute("ContractAbiMgr");
    }


    for (auto it = m_tempblock->pending().begin(); it != m_tempblock->pending().end(); ++it)
    {
        LOG(TRACE) << "SystemContract::updateSystemContract ==> abi address => " << contractAbiMgr.hex() << " ,to= > " << (it->to().hex());

        bytes tempdata = it->data();
        bytesRef fundata = ref(tempdata);
        bytes funhash = fundata.cropped(0, 4).toBytes();
        /*
        cout<<"funhash="<<toString(funhash)<<"\n";
        cout<<"cahash1="<<toString(cahash1)<<"\n";
        cout<<"cahash2="<<toString(cahash2)<<"\n";
        cout<<"caAction="<<toString(caAction)<<"\n";
        cout<<"it->to()="<<toString(it->to())<<"\n";*/

        if ( (it->to() == m_systemproxyaddress ) && (dev::ZeroAddress != m_systemproxyaddress) ) //命中
        {
            routeChange = true;
            LOG(TRACE) << "SystemContract::updateSystemContract SystemProxy setRoute! to=" << it->to() << ",sha3=" << toString(it->sha3()) ;
        }
        else if ( (it->to() == configaction   ) && (dev::ZeroAddress != configaction) ) //命中
        {
            configChange = true;
            LOG(TRACE) << "SystemContract::updateSystemContract ConfigAction set! to=" << it->to() << ",sha3=" << toString(it->sha3()) ;
        }
        else if ( (it->to() == nodeAction ) && (dev::ZeroAddress != nodeAction) && ((funhash == nodehash1) || (funhash == nodehash2) ) ) //命中
        {
            nodeChange = true;
            LOG(TRACE) << "SystemContract::updateSystemContract NodeAction cancelNode|registerNode ! to=" << it->to() << ",sha3=" << toString(it->sha3()) ;
        }
        else if ( (it->to() == caAction ) && (dev::ZeroAddress != caAction) && ( (funhash == cahash1) || (funhash == cahash2) ) ) //命中
        {
            string hashkey;

            bytes calldata = fundata.cropped(4, fundata.size() - 4).toBytes();
            bytesConstRef o(&(calldata));

           
            if ( funhash == cahash2 )
            {
                string pubkey;
                string orgname;
                u256 notbefore;
                u256 notafter;
                byte status;// uint8
                string white;
                string black;

                dev::eth::ContractABI().abiOut<>(o, hashkey, pubkey, orgname, notbefore, notafter, status, white, black);
            }
            else if ( funhash == cahash1 )
            {
                byte status;
                dev::eth::ContractABI().abiOut<>(o, hashkey, status);
            }

            caChangeArg.push_back(hashkey);
            caChange = true;
            LOG(TRACE) << "SystemContract::updateSystemContract CAAction updateStatus|update ! hash=" << hashkey << ", to=" << it->to() << ",sha3=" << toString(it->sha3()) ;
        }
        else if ((it->to() == contractAbiMgr) && (dev::ZeroAddress != contractAbiMgr) && (cohash1 == funhash || funhash == cohash2))
        {
            coChange = true;
            //LOG(TRACE) << "SystemContract::updateSystemContract ContractAbiMgr addAbi ! hash=" << funhash << ", to=" << it->to() << ",sha3=" << toString(it->sha3());
        }

    }//for

    DEV_WRITE_GUARDED(m_lockfilter)
    {

        m_filterchecktranscache.clear();
        m_transcount = 0;
        m_transcachehit = 0;

    }



    if ( routeChange || (m_routes.size() < 1) )
    {
       
        routeChange = true;
        updateRoute();
    }
    if ( configChange )
    {
        
        configChange = true;
        updateConfig();
    }

    if ( nodeChange || (m_nodelist.size() < 1) )
    {
        
        nodeChange = true;
        updateNode();
    }//
    if ( caChange || (m_calist.size() < 1) )
    {
      
        caChange = true;
        updateCa();
    }//

    
    if (coChange || (0 == libabi::ContractAbiMgr::getInstance()->getContractC()))
    {
        updateContractAbiInfo();
    }

    

    DEV_READ_GUARDED(m_lockcbs)
    {
        for (std::map<string, std::vector< std::function<void(string)> > >::iterator it = m_cbs.begin(); it != m_cbs.end(); ++it)
        {
            bool  ok = false;
            std::vector<string> changeArg;

            if ( (configChange && "config" == it->first) )
            {
                ok = true;
                changeArg = configChangeArg;
            }
            else if (  (routeChange && "route" == it->first) )
            {
                ok = true;
                changeArg = routeChangeArg;
            }
            else if (  (nodeChange && "node" == it->first) )
            {
                ok = true;
                changeArg = nodeChangeArg;
            }
            else if (  (caChange && "ca" == it->first) )
            {
                ok = true;
                changeArg = caChangeArg;
            }

            if ( ok && (it->second.size() ) )
            {
                LOG(TRACE) << "SystemContract::updateSystemContract Change:" << it->first;
                for (std::vector< std::function<void(string)> >::iterator cbit = it->second.begin(); cbit != it->second.end(); ++cbit)
                {
                    for ( std::vector<string>::iterator argit = changeArg.begin(); argit != changeArg.end(); argit++)
                    {
                        LOG(TRACE) << "SystemContract::updateSystemContract cb=" << &(*cbit) << ",arg=" << *argit;
                        (*cbit)(*argit);
                    }
                }
            }
        }//
    }

    LOG(TRACE) << "SystemContract::updateSystemContract took:" << (t.elapsed() * 1000000);

}

void SystemContract::updateRoute()
{
    DEV_WRITE_GUARDED(m_lockroute)
    {
        bytes inputdata5 = abiIn("getRouteSize()");
        ExecutionResult ret5 = call(m_systemproxyaddress, inputdata5);
        u256 routesize = abiOut<u256>(ret5.output);
        LOG(TRACE) << "SystemContract::updateRoute RouteSize" << routesize;

        m_routes.clear();
        for ( size_t i = 0; i < (size_t)routesize; i++)
        {
        
            bytes inputdata6 = abiIn("getRouteNameByIndex(uint256)", (u256)i);
            ExecutionResult ret6 = call(m_systemproxyaddress, inputdata6);
            string routename = abiOut<string>(ret6.output);
            
            bytes inputdata7 = abiIn("getRoute(string)", routename);
            ExecutionResult ret7 = call(m_systemproxyaddress, inputdata7);
            Address route = abiOut<Address>(ret7.output);

            m_routes.push_back({route, routename});
            LOG(TRACE) << "SystemContract::updateRoute [" << i << "]=0x" << toString(route) << "," << routename;
        }//for
    }
    DEV_WRITE_GUARDED(m_lockfilter)
    {
        m_transactionfilter.filter = getRoute("TransactionFilterChain");
        m_transactionfilter.name = "TransactionFilterChain";

        m_filterchecktranscache.clear();
        m_transcount = 0;
        m_transcachehit = 0;
    }

    DEV_READ_GUARDED(m_lockroute)
    {
        m_abiMgrAddr = getRoute("ContractAbiMgr");
    }
}

void SystemContract::updateNode( )
{

    DEV_WRITE_GUARDED(m_locknode)
    {
        this->getNodeFromContract(std::bind(&SystemContract::call, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3), m_nodelist);

        LOG(TRACE) << "SystemContract::updateNode  m_nodelist.size=" << m_nodelist.size();
    }

    //for test
    //std::vector< NodeConnParams>  nodevector;
    //getAllNode(43738 ,nodevector );
    //cout<<"nodevector.size()="<<nodevector.size()<<"\n";

}
void SystemContract::updateConfig()
{
   
}
void SystemContract::updateCa( )
{
    DEV_WRITE_GUARDED(m_lockca)
    {

        Address caAction;

        DEV_READ_GUARDED(m_lockroute)
        {
            caAction = getRoute("CAAction");
        }
        if ( Address() != caAction )
        {
            bytes inputdata1 = abiIn("getHashsLength()");

            ExecutionResult ret1 = call(caAction, inputdata1);
            u256 hashslen = abiOut<u256>(ret1.output);
            LOG(TRACE) << "SystemContract::updateCa " << toString(caAction) << " HashsLength" << hashslen;

            m_calist.clear();
            for ( size_t i = 0; i < (size_t)hashslen; i++)
            {
               
                bytes inputdata2 = abiIn("getHash(uint256)", (u256)i);
                ExecutionResult ret2 = call(caAction, inputdata2);
                string hashkey = abiOut<string>(ret2.output);

                
                std::string  hash; 
                std::string pubkey;
                std::string orgname;  
                u256 notbefore;
                u256 notafter;
                byte status;
                u256    blocknumber;

                bytes inputdata4 = abiIn("get(string)", hashkey);
                ExecutionResult ret4 = call(caAction, inputdata4);

                bytesConstRef o(&(ret4.output));
                dev::eth::ContractABI().abiOut<>(o, hash, pubkey, orgname, notbefore, notafter, status, blocknumber);

                string white;
                string black;

                bytes inputdata5 = abiIn("getIp(string)", hashkey);
                ExecutionResult ret5 = call(caAction, inputdata5);

                bytesConstRef o2(&(ret5.output));
                dev::eth::ContractABI().abiOut<>(o2, white, black);

                CaInfo cainfo;
                cainfo.hash = hash;
                cainfo.pubkey = pubkey;
                cainfo.orgname = orgname;
                cainfo.notbefore = notbefore;
                cainfo.notafter = notafter;
                cainfo.status = status ? CaStatus::Ok : CaStatus::Invalid;
                cainfo.blocknumber = blocknumber;
                cainfo.white = white;
                cainfo.black = black;

                m_calist.insert(pair<string, CaInfo>(hash, cainfo) );
                LOG(TRACE) << "SystemContract::updateCa Ca[" << i << "]=" << cainfo.toString();

            }//for
        }
        else
        {
            LOG(WARNING) << "SystemContract::updateCa No CAAction!!!!!!!!!!!!";
        }

    }
}

void SystemContract::updateContractAbiInfo()
{
    Address contractAbiMgrAddr;

    DEV_READ_GUARDED(m_lockroute)
    {
        contractAbiMgrAddr = getRoute("ContractAbiMgr");
    }

    if (contractAbiMgrAddr != dev::ZeroAddress)
    {
        LOG(INFO) << "[SystemContract::updateContractAbiInfo] update contract abi info ,contract abi address => " + ("0x" + contractAbiMgrAddr.hex());

        ExecutionResult ret = call(contractAbiMgrAddr, abiIn("getAbiCount()"));
        u256 nAbiC          = abiOut<u256>(ret.output);

        LOG(INFO) << "[SystemContract::updateContractAbiInfo] address=0x" << contractAbiMgrAddr.hex() << " ,abi count=" << nAbiC;

        for (u256 i = 0; i < nAbiC; i++)
        {
            ExecutionResult ret = call(contractAbiMgrAddr, abiIn("getAllByIndex(uint256)", i));
            bytesConstRef o(&(ret.output));

            string  abi;
            Address addr;
            string  name;
            string  version;
            dev::u256 blocknumber;
            dev::u256 timestamp;

            dev::eth::ContractABI().abiOut<>(o, abi, addr, name, version, blocknumber, timestamp);
            if (timestamp != 0 && !abi.empty())
            {
                try
                {
                    libabi::ContractAbiMgr::getInstance()->addContractAbi(name, version, abi, addr, blocknumber, timestamp);
                }
                catch (...)
                {
                    LOG(WARNING) << "[SystemContract::updateContractAbiInfo] invalid abi => name|version|address|blocknumber|timestamp|abi=" << name
                                 << "|" << version
                                 << "|" << ("0x" + addr.hex())
                                 << "|" << blocknumber.str()
                                 << "|" << timestamp.str()
                                 << "|" << abi;
                }
            }
            else
            {   //理论上不应该存在
                LOG(WARNING) << "[SystemContract::updateContractAbiInfo] timestamp is zero ,name|version|address|blocknumber|timestamp|index|abi=" << name
                             << "|" << version
                             << "|" << ("0x" + addr.hex())
                             << "|" << blocknumber.str()
                             << "|" << timestamp.str()
                             << "|" << i.str()
                             << "|" << abi;
            }
        }
    }
    else
    {
        LOG(WARNING) << "[SystemContract::updateContractAbiInfo] update contract abi info ,but contract mgr address is zero";
    }
}

void SystemContract::getNodeFromContract(std::function<ExecutionResult(Address const, bytes const, bool cache )> _call, std::vector< NodeConnParams> & _nodelist)
{
    Address nodeAction;
    DEV_READ_GUARDED(m_lockroute)
    {
        nodeAction = getRoute("NodeAction");
    }
    if ( Address() != nodeAction )
    {
        bytes inputdata1 = abiIn("getNodeIdsLength()");

        ExecutionResult ret1 = _call(nodeAction, inputdata1, false);
        u256 nodeidslen = abiOut<u256>(ret1.output);
        LOG(TRACE) << "SystemContract::getNodeFromContract " << toString(nodeAction) << " NodeIdsLength" << nodeidslen;


        _nodelist.clear();
        for ( size_t i = 0; i < (size_t)nodeidslen; i++)
        {
            
            bytes inputdata2 = abiIn("getNodeId(uint256)", (u256)i);
            ExecutionResult ret2 = _call(nodeAction, inputdata2, false);
            string nodeid = abiOut<string>(ret2.output);

           

            string ip = "";    
            u256 port = 0;              
            u256 category = 0;  
            string desc;    
            string cahash = ""; 
            string agencyinfo = "";
            u256 idx;                   
            u256 blocknumber;

            bytes inputdata4 = abiIn("getNode(string)", nodeid);
            ExecutionResult ret4 = _call(nodeAction, inputdata4, false);

            bytesConstRef o(&(ret4.output));
            dev::eth::ContractABI().abiOut<>(o, ip, port, category, desc, cahash, agencyinfo, blocknumber);

            bytes inputdata5 = abiIn("getNodeIdx(string)", nodeid);
            ExecutionResult ret5 = _call(nodeAction, inputdata5, false);

            bytesConstRef o2(&(ret5.output));
            dev::eth::ContractABI().abiOut<>(o2, idx);


            NodeConnParams nodeconnparam;
            nodeconnparam._sNodeId = nodeid;
            nodeconnparam._sAgencyInfo = agencyinfo;
            nodeconnparam._sIP = ip;
            nodeconnparam._iPort = (int) port;
            nodeconnparam._iIdentityType = (int)category;
            nodeconnparam._sAgencyDesc = desc;
            nodeconnparam._sCAhash = cahash;
            nodeconnparam._iIdx = idx;

            _nodelist.push_back(nodeconnparam);
            LOG(TRACE) << "SystemContract::updateNode Node[" << i << "]=" << nodeconnparam.toString();

        }//for

        
        sort(_nodelist.begin(), _nodelist.end(), [&](const NodeConnParams & a, const NodeConnParams &  b) {
            return a._iIdx < b._iIdx;
        });

    }
    else
    {
        LOG(WARNING) << "SystemContract::getNodeFromContract No NodeAction!!!!!!!!!!!!";
    }

}

void SystemContract::tempGetAllNode(int _blocknumber, std::vector< NodeConnParams> & _nodelist )
{

    if ( _blocknumber < 0 )
        _blocknumber = m_client->blockChain().number();
    LOG(TRACE) << "SystemContract::tempGetAllNode blocknumber=" << _blocknumber;

    Block tempblock = m_client->block(_blocknumber );
    tempblock.setEvmEventLog(true);

    auto tempCall = [&](Address const & _to, bytes const & _inputdata, bool ) {
        ExecutionResult ret;
        try
        {
           
            srand((unsigned)utcTime());
            struct timeval tv;
            gettimeofday(&tv, NULL);
            u256 nonce = (u256)(rand() + rand() + tv.tv_usec);

            u256 gas = tempblock.gasLimitRemaining() ;
            u256 gasPrice = 100000000;
            Transaction t(0, gasPrice, gas, _to, _inputdata, nonce);
            t.forceSender(m_god);
            ret = tempblock.execute(m_client->blockChain().lastHashes(), t, Permanence::Reverted);

        }
        catch (...)
        {
            LOG(WARNING) << "SystemContract::tempGetAllNode call Fail!" << toString(_inputdata);
            // TODO: Some sort of notification of failure.
            LOG(WARNING) << boost::current_exception_diagnostic_information() << "\n";
        }
        return ret;
    };

    getNodeFromContract(tempCall, _nodelist);
    LOG(TRACE) << "SystemContract::tempGetAllNode  _nodelist.size=" << _nodelist.size();

}
void SystemContract::getAllNode(int _blocknumber , std::vector< NodeConnParams> & _nodevector )
{
    DEV_READ_GUARDED(m_locknode)
    {

        DEV_READ_GUARDED(m_blocklock)
        {
            LOG(TRACE) << "SystemContract::getAllNode _blocknumber="  << _blocknumber
                       << ",m_tempblock.info().number()=" << m_tempblock->info().number() << ",m_nodelist.size()=" << m_nodelist.size();

            if ( (_blocknumber == m_tempblock->info().number()) || ( _blocknumber < 0 )  )
            {
                for ( size_t i = 0; i < m_nodelist.size(); i++)
                {
                    _nodevector.push_back(m_nodelist[i]);
                }
                return;
            }
        }
        
        tempGetAllNode(_blocknumber, _nodevector);
    }
}//function

u256 SystemContract::getBlockChainNumber()
{
    u256 blk_no = 0;
    DEV_READ_GUARDED(m_blocklock)
    {
        blk_no = m_tempblock->info().number();
    }

    return blk_no;
}

void SystemContract::getCaInfo(string _hash, CaInfo & _cainfo)
{
    DEV_READ_GUARDED(m_lockca)
    {
        if ( m_calist.find(_hash) != m_calist.end() )
            _cainfo = m_calist.find(_hash)->second;
    }
}//function



Address SystemContract::getRoute(const string & _route) const
{

   
    for ( size_t i = 0; i < m_routes.size(); i++)
    {
        if ( m_routes[i].name == _route )
        {
            return m_routes[i].action;
        }
    }//for

    return Address();
}


h256 SystemContract::filterCheckTransCacheKey(const Transaction & _t) const
{
    RLPStream s;
    s << _t.safeSender() << _t.from() << _t.to() << toString(_t.data()) ;//<< _t.randomid() ;
    return dev::sha3(s.out());
}

u256 SystemContract::transactionFilterCheck(const Transaction & transaction) {
    if ((int)transaction.getUTXOType() != UTXOType::InValid)
    {
        LOG(TRACE) << "SystemContract::transactionFilterCheck UTXO";
        return (u256)SystemContractCode::Ok;
    }

    if ( isGod(transaction.safeSender()))
    {
		LOG(TRACE) << "SystemContract::transactionFilterCheck God sender";
        return (u256)SystemContractCode::Ok;
    }

    LOG(TRACE) << "SystemContract::transactionFilterCheck sender:" << transaction.safeSender();
    
    m_transcount++;


    u256 checkresult = -1;
	LOG(TRACE) << "SystemContract::transactionFilterCheck filter:" << m_transactionfilter.filter;
    if (transaction.isCreation()) {
        Address sender = transaction.safeSender();
        LOG(TRACE) << "SystemContract::transactionFilterCheck sender:" << sender;
        bytes inputBytes = dev::eth::ContractABI().abiIn(
                               "deploy(address)",
                               transaction.safeSender());

        ExecutionResult res = call(m_transactionfilter.filter, inputBytes);

        if (res.output.empty()) {
            
            LOG(TRACE) << "SystemContract::transactionFilterCheck res.output.empty()";
            checkresult = (u256)SystemContractCode::Ok;
        }
        else {
            bool result = false;
            dev::eth::ContractABI().abiOut(bytesConstRef(&res.output), result);
            LOG(TRACE) << "SystemContract::transactionFilterCheck result:" << result;
            checkresult = result ? ((u256)SystemContractCode::Ok) : (u256)SystemContractCode::Other;
        }
    }
    else {
    h256 key = filterCheckTransCacheKey(transaction);

    DEV_READ_GUARDED(m_lockfilter)
    {
        std::map<h256, u256>::iterator iter = m_filterchecktranscache.find(key);

        if (iter != m_filterchecktranscache.end()) {
            checkresult = iter->second;
        }
    }

    if (checkresult != -1) {
        //if ( iter != m_filterchecktranscache.end() ) //命中cache
        //{
        //checkresult = iter->second;
        m_transcachehit++;
		LOG(TRACE) << "SystemContract::transactionFilterCheck hit cache";
    }
    else {
        string input = toHex(transaction.data());
        string func = input.substr(0, 8);
		LOG(TRACE) << "SystemContract::transactionFilterCheck input:" << input << ",func:" << func;
        bytes inputBytes = dev::eth::ContractABI().abiIn(
                               "process(address,address,address,string,string)",
                               transaction.safeSender(), transaction.from(), transaction.to(), func,
                               input);

        ExecutionResult res = call(m_transactionfilter.filter, inputBytes);

        if (res.output.empty()) {
            
			LOG(TRACE) << "SystemContract::transactionFilterCheck res.output.empty()";
            checkresult = (u256)SystemContractCode::Ok;
        }
        else {
            bool result = false;
            dev::eth::ContractABI().abiOut(bytesConstRef(&res.output), result);
			LOG(TRACE) << "SystemContract::transactionFilterCheck result:" << result;
            checkresult = result ? ((u256)SystemContractCode::Ok) : (u256)SystemContractCode::Other;
        }
        DEV_WRITE_GUARDED(m_lockfilter)
        {
            m_filterchecktranscache.insert(pair<h256, u256>(key, checkresult)); //更新cache
        }
    }
	}
    if ( (u256)SystemContractCode::Ok != checkresult )
    {
        LOG(WARNING) << "SystemContract::transactionFilterCheck Fail!"  << toJS(transaction.sha3()) << ",from=" << toJS(transaction.from());
    }
    else {
        LOG(TRACE) << "SystemContract::transactionFilterCheck Suc!"  << toJS(transaction.sha3()) << ",from=" << toJS(transaction.from());
    }

    if (  (0 == (rand() % 1000)  ) && (m_transcount) )
        LOG(TRACE) << "SystemContract Cache Hint:" << ((100 * m_transcachehit) / m_transcount) << "%";

    return checkresult;

}


void SystemContract::updateCache(Address ) {

 
}

bool SystemContract::isAdmin(const Address &)
{
    return false;
}


bool SystemContract::getValue(const string _key, string & _value)
{
    DEV_READ_GUARDED(m_lockroute)
    {
        Address action = getRoute("ConfigAction");
        if ( Address() != action )
        {
           
            bytes inputdata = abiIn("get(string)", _key);

            ExecutionResult ret = call(action, inputdata);
            _value = abiOut<string>(ret.output); 

        }
        else
        {
            LOG(TRACE) << "SystemContract::getValue NO ConfigAction!" ;
        }
    }
    return true;
}


ExecutionResult SystemContract::call(Address const& _to, bytes const& _inputdata, bool )
{


    ExecutionResult ret;
    try
    {
        
        srand((unsigned)utcTime());
        struct timeval tv;
        gettimeofday(&tv, NULL);
        u256 nonce = (u256)(rand() + rand() + tv.tv_usec);

        DEV_WRITE_GUARDED(m_blocklock)
        {
            u256 gas = m_tempblock->gasLimitRemaining() ;

            u256 gasPrice = 100000000;
            Transaction t(0, gasPrice, gas, _to, _inputdata, nonce);
            t.forceSender(m_god);
            LOG(TRACE) << "SystemContract::call gas=" << gas << ",gasPrice=" << gasPrice << ",nonce=" << nonce;
            ret = m_tempblock->execute(m_client->blockChain().lastHashes(), t, Permanence::Reverted);
        }


    }
    catch (...)
    {
        LOG(WARNING) << "SystemContract::call Fail!" << toString(_inputdata);
        // TODO: Some sort of notification of failure.
        LOG(WARNING) << boost::current_exception_diagnostic_information() << "\n";
    }
    return ret;
}
