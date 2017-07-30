
#pragma once

#include <libdevcore/Common.h>
#include <libevmcore/Instruction.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/Guards.h>
#include <libdevcore/Log.h>
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

    size_t index;          //下标
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

        //初始化       
        size_t codesize=codespace.size();

        for (size_t i = 0; i < codesize; ++i)
        {
            Instruction op = Instruction(code[i]);
            OpcodeInfo opcodeinfo(i,op,instructionInfo(op).name);

            //下面修正
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
                vectorinfo.push_back (opcodeinfo);//先将当前的插入

                size_t len=(byte)op - (byte)Instruction::PUSH1 + 1;//中间都是 字节value

                for( size_t j=i+1;j<(i+len+1);j++)
                {
                    OpcodeInfo _opcodeinfo(j,Instruction::STOP,"<Value_INSTRUCTION>"  );
                    vectorinfo.push_back(_opcodeinfo);
                }

                i +=len;
            }
            else
            {
                vectorinfo.push_back (opcodeinfo);//先将当前的插入
            }
            

            //cout<<"i="<<i<<"	"<<instructionInfo(op).name<<endl;

        }

        lasttime=utcTime();//utcTime的实现 已经改成毫秒了
    }

	byte* code;
    bytes codespace;


    uint64_t    lasttime;  //最后更新时间    
    std::vector<OpcodeInfo> vectorinfo;

    void hint(size_t index,size_t codespacesize)
    {
        if(  codespace.size() > codespacesize  )
        {
            //说明进来的是普通交易，不是部署合约，所以要加上偏移量
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
    std::map<Address, StatItem> m_statitems;    //合约地址为key

    mutable SharedMutex  m_lock;	//虽然没啥必要 
    bool m_aborting = false;
    std::string m_outputpath;//输出路径

public:

	CoverTool() ;
	 ~CoverTool() ;

    void outputpath(std::string path)//这么做，是因为covertool在vm中是静态类成员，初始化时datadir并没有更新
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
