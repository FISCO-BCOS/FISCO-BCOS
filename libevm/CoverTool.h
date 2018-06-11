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
 * @file: CoverTool.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include <libdevcore/Common.h>
#include <libevmcore/Instruction.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/Guards.h>
#include <libdevcore/easylog.h>
#include <boost/timer.hpp>
#include <libdevcore/FileSystem.h>
#include <thread>

using namespace dev::eth;
using namespace std;

namespace dev
{
namespace eth
{



using Address = h160;

struct OpcodeInfo
{
    OpcodeInfo(size_t i,Instruction o=Instruction::STOP,std::string n="INVALID_INSTRUCTION"):index(i),op(o),opname(n),hint(0){}

    size_t index;          
    Instruction op;     //opcode   
    std::string opname; 
    u64 hint;
};

struct StatItem
{
    StatItem(bytes cspace)
    {
        codespace=cspace;
        code=codespace.data();

        //Initralize       
        size_t codesize=codespace.size();

        for (size_t i = 0; i < codesize; ++i)
        {
            Instruction op = Instruction(code[i]);
            OpcodeInfo opcodeinfo(i,op,instructionInfo(op).name);

            //Correct
            if (op == Instruction::PUSHC ||
		        op == Instruction::JUMPC ||
		        op == Instruction::JUMPCI)
            {                
                opcodeinfo.op = Instruction::BAD;
                opcodeinfo.opname=instructionInfo(Instruction::BAD).name;
            }

            if ((byte)Instruction::PUSH1 <= (byte)op &&
                    (byte)op <= (byte)Instruction::PUSH32)
            {
                vectorinfo.push_back (opcodeinfo);//push back current opcode

                size_t len=(byte)op - (byte)Instruction::PUSH1 + 1;//words value in the middle

                for( size_t j=i+1;j<(i+len+1);j++)
                {
                    OpcodeInfo _opcodeinfo(j,Instruction::STOP,"<Value_INSTRUCTION>"  );
                    vectorinfo.push_back(_opcodeinfo);
                }

                i +=len;
            }
            else
            {
                vectorinfo.push_back (opcodeinfo);//push back current opcode
            }
            

            //cout<<"i="<<i<<"	"<<instructionInfo(op).name<<"\n";

        }

        lasttime=utcTime();//utcTime, has changed to ms
    }

	byte* code;
    bytes codespace;


    uint64_t    lasttime;     
    std::vector<OpcodeInfo> vectorinfo;

    void hint(size_t index,size_t codespacesize)
    {
        if(  codespace.size() > codespacesize  )
        {
            //Contract deployment, add offset
            index=index+(codespace.size()-codespacesize);
        }
        if( index < vectorinfo.size() )
        {
            vectorinfo.at(index).hint++;
            lasttime=utcTime();
        }
              
            
    }
   
};



class CoverTool 
{
private:   
    std::vector<std::thread> m_checks;
    std::map<Address, StatItem> m_statitems;    //Contract address key

    mutable SharedMutex  m_lock;	//It is not necessary :)
    bool m_aborting = false;
    std::string m_outputpath;

public:

	CoverTool() ;
	 ~CoverTool() ;

    void outputpath(std::string path)//datadir won't update because covertool is a static member in vm.
    {
        m_outputpath=path;
    }
    bool has(Address address);
    void init(Address address,bytes cspace);
    void hint(Address address,size_t index,size_t codespacesize);


   
private:

    void output();
	

};



}
}
