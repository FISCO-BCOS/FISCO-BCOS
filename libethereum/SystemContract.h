


#pragma once
#include <libdevcore/Guards.h>
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/CommonIO.h>
#include "Client.h"
#include "SystemContractApi.h"

using namespace std;
using namespace dev;

using namespace dev::eth;


namespace dev
{
namespace eth
{

enum class FilterType {
    Account,
    Node
};

class SystemContractApi;
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

struct SystemAction {
    Address action;
    string  name;
};

class SystemContract: public SystemContractApi
{
public:
    /// Constructor.
    SystemContract(const Address _address, const Address _god, Client* _client) : SystemContractApi(  _address, _god), m_client(_client), m_tempblock(0)
    {
      
        std::shared_ptr<Block> tempblock(new Block(0));
        *tempblock = m_client->block(m_client->blockChain().number());
        updateSystemContract(tempblock);
    }

    /// Destructor.
    virtual ~SystemContract() {}

    //所有的filter检查
    virtual u256 transactionFilterCheck(const Transaction & transaction) override;

   

    virtual void updateCache(Address address) override;

    //是否是链的管理员
    virtual bool isAdmin(const Address & _address) override;
    //获取全网配置项
    virtual bool getValue(const string _key, string & _value) override;

    // 获取节点列表 里面已经含有idx
    virtual void getAllNode(int _blocknumber/*<0 代表最新块*/ ,std::vector< NodeConnParams> & _nodelist )override;
     virtual u256 getBlockChainNumber()override;
    virtual void getCaInfo(string _hash,CaInfo & _cainfo) override;
    virtual void updateSystemContract(std::shared_ptr<Block> block) override;

private:

    

    Client* m_client;

    mutable SharedMutex  m_blocklock;     

    std::shared_ptr<Block> m_tempblock;

    mutable SharedMutex  m_lockroute;
    std::vector<SystemAction> m_routes;

    mutable SharedMutex  m_lockfilter;
    SystemFilter m_transactionfilter;
    std::map<h256, u256> m_filterchecktranscache; 

 

    mutable SharedMutex  m_locknode;
    std::vector< NodeConnParams> m_nodelist;

    mutable SharedMutex  m_lockca;
    std::map<string,CaInfo> m_calist;

    ExecutionResult call(Address const& _to, bytes const& _inputdata, bool cache = false) ; 

    Address getRoute(const string & _route)const;

    h256 filterCheckTransCacheKey(const Transaction & _t) const ;

    void updateRoute( );
    void updateNode( );
    void tempGetAllNode(int _blocknumber,std::vector< NodeConnParams> & _nodevector);
    void updateConfig( );
    void updateCa( );



    void getNodeFromContract(std::function<ExecutionResult(Address const,bytes const,bool cache )>,std::vector< NodeConnParams> & _nodelist);

    struct CallCache {
    	std::map<bytes, ExecutionResult> res;
    };

    std::map<Address, CallCache> _callCaches;
};



}
}



