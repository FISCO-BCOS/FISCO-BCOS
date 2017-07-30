

#pragma once

#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcrypto/Common.h>

#include "Client.h"
#include "Transaction.h"
#include "NodeConnParamsManager.h"
#include <libdevcore/easylog.h>

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
        Address m_god;
        /*系统合约变更回调
        key :  config| filter | node | ca */
        mutable SharedMutex  m_lockcbs;
        std::map<string,std::vector< std::function<void(string )> > > m_cbs;

       

    public:
    
   
    bool isGod(const Address _address ) const
    {
        return (m_god!=Address() && m_god == _address)?true:false;
    }
	/// Constructor.
	SystemContractApi( const  Address _address,const  Address _god):m_systemproxyaddress(_address),m_god(_god) {}

	/// Destructor.
	virtual ~SystemContractApi() {}

	
	virtual u256 transactionFilterCheck(const Transaction &) { return (u256)SystemContractCode::Ok; };

	//更新缓存
	virtual void updateCache(Address) {};
 
	
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



