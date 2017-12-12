/**
 * @file: EthCallDemo.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include "EthCallEntry.h"
#include <libdevcore/easylog.h>
#include <libdevcore/Common.h>
#include <string>
#include <vector>

using namespace std;
using namespace dev;
using namespace dev::eth;

namespace dev
{
namespace eth
{

/*
*   ethcall 接口编程举例
*   例如在sol中，ethcall调用如下：
*       int a;
*       int b;
*       bytes memory bstr;
*       string memory str;
*       uint32 callId = 0x66666;
*       uint r;
*       assembly{
*           r := ethcall(callId, a, b, bstr, str, 0, 0, 0, 0, 0)
*       }
*   对应的EthCallExecutor，可以这么写：（EthCallDemo）
*/

class EthCallDemo : EthCallExecutor<int, int, vector_ref<byte>, string> //按顺序定义除开callId之外的类型
{
    /*
    *   编写步骤如下：
    *   1. 本文件引用头文件 EthCallEntry.h
    *
    *   2. EthCallEntry.cpp 中引用本头文件
    *
    *   3. 在EthCallEntry.h中的EthCallList中声明一个CallId
    *           如：EthCallIdList::DEMO = 0x66666
    *
    *   4. 在EthCallEntry.cpp的EthCallContainer中添加下面这行，实例化EthCallDemo
    *           如：static EthCallDemo ethCallDemo; 
    *   
    *   5. 仿造本Demo实现相应功能：继承EthCallExecutor类，并实现类中的构造函数、ethcall、gasCost
    */    
public:
    EthCallDemo()
    {
        //使用刚刚声明的CallId将EthCallDemo注册到ethCallTable中
        //LOG(DEBUG) << "ethcall bind EthCallDemo--------------------";
        this->registerEthcall(EthCallIdList::DEMO); //EthCallList::DEMO = 0x66666为ethcall的callid
    }

    u256 ethcall(int a, int b, vector_ref<byte>bstr, string str) override 
    {
        LOG(INFO) << "ethcall " <<  a + b << " " << bstr.toString() << str;

        //用vector_ref定义的vector可直接对sol中的array赋值
        //vector_ref的用法与vector类似，可参看libdevcore/vector_ref.h
        bstr[0] = '#';
        bstr[1] = '#';
        bstr[2] = '#';
        bstr[3] = '#';
        bstr[4] = '#';
        bstr[5] = '#';

        return 0; //返回值写入sol例子中的r变量中
    }

    uint64_t gasCost(int a, int b, vector_ref<byte>bstr, string str) override 
    {
        return sizeof(a) + sizeof(b) + bstr.size() + str.length(); //消耗的gas一般与处理的数据长度有关
    }
};

///EthCallDemo举例结束///


}
}