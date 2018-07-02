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
 * @file: SystemContractApi.h
 * @author: toxotguo
 * @date: 2018
 */

#pragma once

#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcrypto/Common.h>

#include "Transaction.h"
#include "NodeConnParamsManager.h"

using namespace std;
using namespace dev::eth;

namespace dev
{
namespace eth
{

class Client;

enum class FilterType {
    Account,
    Node
};

enum class SystemContractCode;

using Address = h160;


/**
 * @brief Main API hub for System Contract
 */
//过滤器
struct SystemFilter {
    Address filter;
    string  name;
};
//行为合约
struct SystemAction {
    Address action;
    string  name;
};

enum class SystemContractCode
{
	Ok,	
    NoDeployPermission, 
    NoTxPermission, 
    NoCallPermission, 
    NoGraoupAdmin,  
    NoAdmin,        
    InterveneAccount,
    InterveneContract,
    InterveneContractFunc,
    NodeNoRegister, 
    NodeNoregister, 
    NodeCAError,     
    NodeCANoExist,   
    NodeSignError,  
    Other
};



/**
 * @brief Main API hub for System Contract
 */



class SystemContractApi
{
    protected:
        Address m_systemproxyaddress;
        Address m_god;
        
        // key :  config| filter | node | ca 
        mutable SharedMutex  m_lockcbs;
        std::map<string,std::vector< std::function<void(string )> > > m_cbs;

       

    public:
    
   
    bool isGod(const Address _address ) const
    {
        return (m_god!=Address() && m_god == _address)?true:false;
    }
    // get god address
    Address const getGodAddress() const { return m_god; }
	/// Constructor.
	SystemContractApi( const  Address _address,const  Address _god):m_systemproxyaddress(_address),m_god(_god) {}

	/// Destructor.
	virtual ~SystemContractApi() {}

	
	virtual u256 transactionFilterCheck(const Transaction &) { return (u256)SystemContractCode::Ok; };

	virtual void updateCache(Address) {};
	
    /*
    *   从系统合约中拉取所有的节点信息，提供接口给NodeConnParamsManager
    *   NodeConnParamsManager封装接口将核心节点列表出来给到共识
    *   SystemContract 负责缓存及处理回调更新 NodeConnParamsManager只处理逻辑即可，逻辑需要拉取列表的时候到SystemContract实时拉取
    */
    virtual void getAllNode(int  /*<0  代表最新块*/ ,std::vector< NodeConnParams> & ){}
    //for ssl 
    virtual void getAllNode(int  /*<0  代表最新块*/ ,std::vector< NodeParams> & ){}

    virtual u256 getBlockChainNumber()
    {
        return 0;
    }
    virtual void getCaInfo(string ,CaInfo & )
    {
    }
    
    virtual void addCBOn(string _name/*route | config | node | ca*/,std::function<void(string)> _cb)
    {
        DEV_WRITE_GUARDED(m_lockcbs)
        {
            if( m_cbs.find(_name ) != m_cbs.end() )
            {
                m_cbs.find(_name )->second.push_back(_cb);
            }
            else
            {
                std::vector< std::function<void(string)> > cbs;
                cbs.push_back(_cb);
                m_cbs.insert( std::pair<string,std::vector< std::function<void(string)> >>(_name,cbs));
            }
        }
        
    }
   
    virtual void    updateSystemContract(std::shared_ptr<Block>)
    {
    }
    
    
	virtual bool isAdmin(const Address & ) 
    {        
        return true;
    }

   
    virtual bool getValue(const string ,string & ) 
    {
        return true;
    }

    //get the contract address
    virtual Address getRoute(const string & _route) const=0;

};


}
}



