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
 * @file: NonceCheck.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include <libethereum/NonceCheck.h>
#include <libdevcore/Common.h>

using namespace dev;

NonceCheck:: ~NonceCheck() 
{
  
}

u256 NonceCheck::maxblocksize=1000;

void NonceCheck::init(BlockChain const& _bc)
{   
    m_startblk=0;
    m_endblk=0;   


    updateCache(_bc,true);

}//fun 

//这个地方的生成算法 是可变的，但感觉不用加上blocklimit? blocklimit 在交易进块之前做就可以了，和nonce校验分开
std::string NonceCheck::generateKey(Transaction const & _t)
{   
    Address account=_t.from();
    std::string key=toHex(account.ref());
    key += "_"+toString(_t.randomid());

    return key; //可读性好些，计算sha256也可以 
}

bool NonceCheck::ok(Transaction const & _transaction,bool _needinsert)
{
    DEV_WRITE_GUARDED(m_lock)
    {
        string key=this->generateKey(_transaction);
        auto iter=  m_cache.find( key );
        if( iter != m_cache.end() )
            return false;
        if( _needinsert )
        {
            m_cache.insert(std::pair<std::string,bool>(key,true));  
        }


        
    }   

    return true;
}



void NonceCheck::delCache( Transactions const & _transcations)
{
    DEV_WRITE_GUARDED(m_lock)
    {
        for( unsigned i=0;i<_transcations.size();i++)
        {
            string key=this->generateKey(_transcations[i]);
            auto iter=  m_cache.find( key );
            if( iter != m_cache.end() )
                m_cache.erase(iter);   
        }//for 
    }
}



void NonceCheck::updateCache(BlockChain const& _bc,bool _rebuild/*是否强制rebuild */)
{ 
  
    DEV_WRITE_GUARDED(m_lock)
    {
        try
        {
            Timer timer;
            unsigned lastnumber=_bc.number();//最新的块号

             //第一步，先保存上一次的
            unsigned prestartblk=m_startblk;
            unsigned preendblk=m_endblk;

            m_endblk=lastnumber;
            if( lastnumber >(unsigned)NonceCheck::maxblocksize )
                m_startblk=lastnumber-(unsigned)NonceCheck::maxblocksize;
            else
                m_startblk=0;
           
            LOG(TRACE)<<"NonceCheck::updateCache m_startblk="<<m_startblk<<",m_endblk="<<m_endblk<<",prestartblk="<<prestartblk<<",preendblk="<<preendblk<<",_rebuild="<<_rebuild;
            if( _rebuild ) // 如果是直接重建
            {
                m_cache.clear();//直接清空，重建，避免在切链的时候或者启动的时候工作量太大
                preendblk=0;//让下面的for循环取到m_startblk开始
            }
            else
            {
                //第二步 淘汰掉 窗口外的
                 for( unsigned  i=prestartblk;i<m_startblk;i++)
                {
                    h256 blockhash=_bc.numberHash(i);//先拿到块hash

                    std::vector<bytes> bytestrans=_bc.transactions(blockhash);
                    for( unsigned j=0;j<bytestrans.size();j++)
                    {
                            Transaction	t = Transaction(bytestrans[j], CheckTransaction::None);
                            string key=this->generateKey(t);
                            auto iter=  m_cache.find( key );
                            if( iter != m_cache.end() )
                                m_cache.erase(iter);   
                    }//for 
                }//for
            }
           
            //第三步 新增  注意这里的条件 避免重复执行插入
            for( unsigned  i=std::max(preendblk+1,m_startblk);i<=m_endblk;i++)
            {
                h256 blockhash=_bc.numberHash(i);//先拿到块hash

                std::vector<bytes> bytestrans=_bc.transactions(blockhash);
                for( unsigned j=0;j<bytestrans.size();j++)
                {
                        Transaction	t = Transaction(bytestrans[j], CheckTransaction::None);
                        string key=this->generateKey(t);
                        auto iter=  m_cache.find( key );
                        if( iter == m_cache.end() )
                            m_cache.insert(std::pair<std::string,bool>(key,true));   
                }//for 
            }//for
                
            LOG(TRACE)<<"NonceCheck::updateCache cache size="<<m_cache.size()<<",cost"<<(timer.elapsed() * 1000);

        }
        catch (...)
        {
            // should not happen as exceptions 
            LOG(WARNING) << "o NO!!!!  NonceCheck::updateCache " << boost::current_exception_diagnostic_information();
            //m_aborting=true;
        }
    }
      
    
}//fun



