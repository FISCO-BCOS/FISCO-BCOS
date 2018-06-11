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
 * @file: CoverTool.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include "CoverTool.h"
#include <libdevcore/easylog.h>

CoverTool::CoverTool() 
{   
   // cout<<"CoverTool init"<<"\n";
   //LOG(TRACE)<<"CoverTool init";

    size_t threads=1;// 1 is enough
    for (unsigned i = 0; i < threads; ++i)
    m_checks.emplace_back([=](){
       
        pthread_setThreadName("CoverTool:check" );
        this->output();
    });

    
}

CoverTool:: ~CoverTool() 
{
   // cout<<"~CoverTool"<<"\n";

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
    //cout<<"CoverTool init"<<"\n";

   
    DEV_WRITE_GUARDED(m_lock)
    {
        //cout<<"address="<<address<<",size="<<cspace.size()<<"\n";

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
            //cout<<"CoverTool hint"<<",index="<<index<<"\n";
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
                    if( now - iter->second.lasttime > 600 )//exit when timeout
                    {
                        std::string  message="";//toString(iter->first)+"\n";
                        //message +="lasttime:"+toString(iter->second.lasttime)+"\n";
                        //message +="outputtime:"+toString(now)+"\n";

                        for( size_t i=0;i<iter->second.vectorinfo.size();i++)
                        {
                            message +=iter->second.vectorinfo.at(i).opname+"\t";
                            message +=toString(iter->second.vectorinfo.at(i).hint)+"\n";
                        }//for 

                        //write to file
                        if( !m_outputpath.empty() )
                            writeFile(m_outputpath+toString(iter->first), message, true);
                        
                        
                        //delete per day
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



