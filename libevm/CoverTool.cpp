#include "CoverTool.h"




CoverTool::CoverTool() 
{   
  

    size_t threads=1;//一个就好了
    for (unsigned i = 0; i < threads; ++i)
    m_checks.emplace_back([=](){
       
        setThreadName("CoverTool:check" );
        this->output();
    });

    
}

CoverTool:: ~CoverTool() 
{
   // cout<<"~CoverTool"<<endl;

    m_aborting = true;

    for (auto& i: m_checks)
        i.join();
    m_checks.clear();
}


bool CoverTool::has(Address address)
{
    return m_statitems.find(address) != m_statitems.end() ? true:false;
}
void CoverTool::init(Address address,bytes cspace)
{
    //cout<<"CoverTool init"<<endl;

   
    DEV_WRITE_GUARDED(m_lock)
    {
        //cout<<"address="<<address<<",size="<<cspace.size()<<endl;

        if( this->has(address) )
            return ;        
        
        StatItem s(cspace);
        m_statitems.insert(std::pair<Address, StatItem>(address,s) );
    }
   
    
}
void CoverTool::hint(Address address,size_t index,size_t codespacesize)
{
    

    DEV_WRITE_GUARDED(m_lock)
    {
         if( this->has(address) )
        {
            //cout<<"CoverTool hint"<<",index="<<index<<endl;
            m_statitems.find(address)->second.hint(index,codespacesize);
        }
    }

   
}


void CoverTool::output()
{

    while(!m_aborting)
    {
        DEV_WRITE_GUARDED(m_lock)
        {
            try
            {
                uint64_t now=utcTime();
                std::map<Address, StatItem>::iterator iter=m_statitems.begin();
                for(; iter != m_statitems.end();iter++)
                {
                    if( now - iter->second.lasttime > 600 )//超时就输出
                    {
                        std::string  message="";//toString(iter->first)+"\n";
                        //message +="lasttime:"+toString(iter->second.lasttime)+"\n";
                        //message +="outputtime:"+toString(now)+"\n";

                        for( size_t i=0;i<iter->second.vectorinfo.size();i++)
                        {
                            message +=iter->second.vectorinfo.at(i).opname+"\t";
                            message +=toString(iter->second.vectorinfo.at(i).hint)+"\n";
                        }//for 

                        //写文件
                        if( !m_outputpath.empty() )
                            writeFile(m_outputpath+toString(iter->first), message, true);
                        
                        
                        //一天没更新就删除 
                        if( now - iter->second.lasttime > 24*3600*1000 )
                            m_statitems.erase(iter);
                    }//if
                }//for

            }
            catch (...)
            {
                // should not happen as exceptions 
                LOG(WARNING) << "Bad CoverTool:" << boost::current_exception_diagnostic_information();
                m_aborting=true;
            }
        }
        

        this_thread::sleep_for(std::chrono::milliseconds(10000));
    }//while
    
}//fun



