
#pragma once

#include <libdevcore/Common.h>
#include <libdevcore/Guards.h>

#include <libdevcore/CommonIO.h>
#include <boost/timer.hpp>
#include <thread>
#include <libethereum/Transaction.h>
#include <libethereum/Block.h>
#include <libethereum/BlockChain.h>


using namespace std;
using namespace dev::eth;

namespace dev
{
namespace eth
{


class NonceCheck 
{
private:   
  
  
    std::unordered_map< std::string,bool  > m_cache;    
    unsigned m_startblk; 
    unsigned m_endblk;  
    mutable SharedMutex  m_lock;    
   std::string generateKey(Transaction const & _t); 
   
public:
    static u256    maxblocksize;

    
    NonceCheck(){}
	 ~NonceCheck() ;

	void init(BlockChain const& _bc);	
    bool ok(Transaction const & _transaction,bool _needinsert=false/*如果不存在是否插入*/);
    void updateCache(BlockChain const& _bc,bool _rebuild=false);
    void delCache( Transactions const & _transcations);

};



}
}
