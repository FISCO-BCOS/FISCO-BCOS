#include <libethereum/NonceCheck.h>
#include <libdevcore/Common.h>
#include <libdevcore/easylog.h>
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

}


std::string NonceCheck::generateKey(Transaction const & _t)
{   
    Address account=_t.from();
    std::string key=toHex(account.ref());
    key += "_"+toString(_t.randomid());

    return key; 
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
        }
    }
}



void NonceCheck::updateCache(BlockChain const& _bc,bool _rebuild/*是否强制rebuild */)
{ 
  
    DEV_WRITE_GUARDED(m_lock)
    {
        try
        {
            Timer timer;
            unsigned lastnumber=_bc.number();

            
            unsigned prestartblk=m_startblk;
            unsigned preendblk=m_endblk;

            m_endblk=lastnumber;
            if( lastnumber >(unsigned)NonceCheck::maxblocksize )
                m_startblk=lastnumber-(unsigned)NonceCheck::maxblocksize;
            else
                m_startblk=0;
           
            LOG(TRACE)<<"NonceCheck::updateCache m_startblk="<<m_startblk<<",m_endblk="<<m_endblk<<",prestartblk="<<prestartblk<<",preendblk="<<preendblk<<",_rebuild="<<_rebuild;
            if( _rebuild ) 
            {
                m_cache.clear();
                preendblk=0;
            }
            else
            {
                
                 for( unsigned  i=prestartblk;i<m_startblk;i++)
                {
                    h256 blockhash=_bc.numberHash(i);

                    std::vector<bytes> bytestrans=_bc.transactions(blockhash);
                    for( unsigned j=0;j<bytestrans.size();j++)
                    {
                            Transaction	t = Transaction(bytestrans[j], CheckTransaction::None);
                            string key=this->generateKey(t);
                            auto iter=  m_cache.find( key );
                            if( iter != m_cache.end() )
                                m_cache.erase(iter);   
                    }
                }
            }
           
           
            for( unsigned  i=std::max(preendblk+1,m_startblk);i<=m_endblk;i++)
            {
                h256 blockhash=_bc.numberHash(i);

                std::vector<bytes> bytestrans=_bc.transactions(blockhash);
                for( unsigned j=0;j<bytestrans.size();j++)
                {
                        Transaction	t = Transaction(bytestrans[j], CheckTransaction::None);
                        string key=this->generateKey(t);
                        auto iter=  m_cache.find( key );
                        if( iter == m_cache.end() )
                            m_cache.insert(std::pair<std::string,bool>(key,true));   
                }
            }
                
            LOG(TRACE)<<"NonceCheck::updateCache cache size="<<m_cache.size()<<",cost"<<(timer.elapsed() * 1000);

        }
        catch (...)
        {
            // should not happen as exceptions 
            LOG(WARNING) << "o NO!!!!  NonceCheck::updateCache " << boost::current_exception_diagnostic_information();
           
        }
    }
      
    
}//fun



