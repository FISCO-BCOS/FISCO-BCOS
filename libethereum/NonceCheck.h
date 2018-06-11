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
 * @file: NonceCheck.h
 * @author: toxotguo
 * 
 * @date: 2017
 */


#pragma once

#include <libdevcore/Common.h>
#include <libdevcore/Guards.h>
#include <libdevcore/easylog.h>
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
	
    bool ok(Transaction const & _transaction,bool _needinsert=false);
   

    void updateCache(BlockChain const& _bc,bool _rebuild=false);

    void delCache( Transactions const & _transcations);

};



}
}
