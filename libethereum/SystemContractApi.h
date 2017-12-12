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

#include "Client.h"
#include "Transaction.h"
#include "NodeConnParamsManager.h"

using namespace std;
using namespace dev::eth;

namespace dev
{
namespace eth
{

class Client;



enum class SystemContractCode
{
	Ok,	
    NoDeployPermission, //没有部署合约权限
    NoTxPermission, //没有交易权限
    NoCallPermission, //没有查询Call权限
    NoGraoupAdmin,  //不是组管理员帐号
    NoAdmin,        //不是链管理员帐号
    InterveneAccount,//帐号被干预
    InterveneContract,//合约被干预
    InterveneContractFunc,//合约接口被干预
    NodeNoRegister, //节点未登记
    NodeNoregister, //节点未登记
    NodeCAError,     //节点机构证书无效
    NodeCANoExist,   //节点机构证书不存在
    NodeSignError,  //节点签名错误
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
        Address m_god;//上帝帐号
        //系统合约变更回调
        // key :  config| filter | node | ca 
        mutable SharedMutex  m_lockcbs;//锁节点列表更新
        std::map<string,std::vector< std::function<void(string )> > > m_cbs;

       

    public:
    
    //是否是上帝帐号
    bool isGod(const Address _address ) const
    {
        return (m_god!=Address() && m_god == _address)?true:false;
    }
	/// Constructor.
	SystemContractApi( const  Address _address,const  Address _god):m_systemproxyaddress(_address),m_god(_god) {}

	/// Destructor.
	virtual ~SystemContractApi() {}

	//交易的filter检查
	virtual u256 transactionFilterCheck(const Transaction &) { return (u256)SystemContractCode::Ok; };

	//更新缓存
	virtual void updateCache(Address) {};
    virtual void startStatTranscation(h256){}
	
    /*
    *   从系统合约中拉取所有的节点信息，提供接口给NodeConnParamsManager
    *   NodeConnParamsManager封装接口将核心节点列表出来给到共识
    *   SystemContract 负责缓存及处理回调更新 NodeConnParamsManager只处理逻辑即可，逻辑需要拉取列表的时候到SystemContract实时拉取
    */
    virtual void getAllNode(int  /*<0  代表最新块*/ ,std::vector< NodeConnParams> & )
    {        
    }
    virtual u256 getBlockChainNumber()
    {
        return 0;
    }
    virtual void getCaInfo(string ,CaInfo & )
    {
    }
    //注册回调
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
    //这里可以做精细化控制，把块的交易传进来再对关注到的合约及接口做按需更新，建个回调池
    virtual void    updateSystemContract(std::shared_ptr<Block>)
    {
    }
    
     //是否是链的管理员
	virtual bool isAdmin(const Address & ) 
    {        
        return true;
    }

    //获取全网配置项
    virtual bool getValue(const string ,string & ) 
    {
        return true;
    }

    //get the contract address
    virtual Address getRoute(const string & _route) const=0;

};

class SystemContractApiFactory
{
    public:
		static std::shared_ptr<SystemContractApi> create(const Address _address, const Address _god, Client* _client);
};


}
}



