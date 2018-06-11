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
/**
 * @file: SystemContractApi.h
 * @author: fisco-dev
 * 
 * @date: 2017
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

using Address = h160;

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
    virtual void startStatTranscation(h256){}
	
    virtual void getAllNode(int  ,std::vector< NodeConnParams> & )
    {        
    }
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



