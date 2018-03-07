# EthCall设计文档

## 一、功能介绍

### 1、描述

​        EthCall 是一个连接Solidity和C++的通用编程接口，内置在EVM中。开发者可将Solidity中复杂的、效率低的、不可实现的逻辑，通过C++语言实现，并以EthCall接口的形式供给Solidity。Solidity即可通过EthCall接口，以函数的方式直接调用C++的逻辑。在保证Solidity语言封闭性的前提下，提供了更高的执行效率和更大的灵活性。

### 2、背景

​        目前Solidity语言存在以下两方面问题：

（1）Solidity 执行效率低

​        由Solidity语言直接实现的合约，执行的效率较低。原因是Solidity的底层指令OPCODE，在执行时需要翻译成多条C指令进行操作。对于复杂的Solidity程序，翻译成的C指令非常庞大，造成运行缓慢。而直接使用C代码直接实现相应功能，并不需要如此多的指令。 若采用C++来实现合约中逻辑相对复杂、计算相对密集的部分，将有效的提高合约执行效率。

（2）Solidity 局限性

​        Solidity语言封闭的特性，也限制了其灵活性。Solidity语言缺乏各种优秀的库，开发调试也较为困难。若Solidity语言能够直接调用各种C++的库，将大大拓展合约的功能，降低合约开发的难度。

​        **因此，需要提供一种接口，让Solidity能够调用外部的功能，在保证Solidity语言封闭性的前提下，提高执行效率，增加程序的灵活性。**

### 3、相关工作

​        为解决上述问题，目前已有两种解决方案：

（1）SHA3接口重封装

​        BCOS中采用的方式。在OPCODE的SHA3接口上封装一层，通过传入的字符串标识符判断，是普通的SHA3功能，还是拓展功能。此方式在每次调用时都需要将字符串转成参数，接口效率低。且实现不够优雅。

（2）Precompiled Contracts

​        Eth中内置的方式。在EVM中内置预编译合约，通过Solidity语言中的关键字进行调用。此方式拓展性不好，增加功能时需多次修改编译器。且调用功能时，采用消息式（而不是函数式）的调用，在调用和返回时，都需要反复打包和解包，效率低，实现复杂。

### 4、EthCall

​        EthCall为Solidity提供了一种**函数式**调用底层C++模块的方法。

**举例--同态加密**

​        在Solidity中，传递两个密文```d1```，```d2```，返回密文同态加的结果```ret```。

```Solidity
//Solidity
function paillierAdd(string d1, string d2) internal constant returns (string) 
{
	string memory ret = new string(bytes(d1).length);
	uint32 call_id = callId();
	uint r;
	assembly{
		//函数式调用，结果直接写到ret中，类似引用的方式
		r := ethcall(call_id, ret, d1, d2, 0, 0, 0, 0, 0, 0)   ///<----- ethcall
	}
	return ret;
}
```

​        在C++中，根据调用的参数，编写函数```ethcall()```，实现逻辑。

```c++
//C++
u256 ethcall(vector_ref<char> result, std::string d1, std::string d2)
{
   /*
   *	实现同态加密逻辑，结果直接写到result中
   */
    return 0;
}
```

**优势**

（1）函数式调用，支持传递引用参数。调用接口开销低，编程实现简单方便。

（2）C++代码运行复杂逻辑，执行效率高。

（3）可调用C++库，功能强大，灵活性强，降低合约开发难度。



## 二、使用方法

​         EthCall的使用，可分为两部分。在Solidity端，向EthCall的传参，并使用支持EthCall的编译器进行编译。在C++端，编写与Solidity端参数对应的接口函数，并实现需要实现的逻辑。具体描述如下。

### 1、Solidity调用EthCall

**编译器**

​         支持EthCall的编译器项目的根目录下，为fisco-solc。使用方式与一般的Solidity编译器相同。

**接口调用实现**

（1）确定一个call id，唯一标识底层C++需实现的功能。

（2）确定需要传递的参数。若是string或bytes，需定义为memory类型（外层函数的参数默认为memory类型）。

（3）调用ethcall接口，参数依次排列，call id为第一个，接着为其它参数，参数数量不足10个用0填充。

​         举例如下：

```solidity
uint32 callId = 0x66666; 
int a;
int b;
bytes memory bstr;
string memory str;
uint r;
assembly{
	r := ethcall(callId, a, b, bstr, str, 0, 0, 0, 0, 0)
}
```

​         更完整的例子可参考tool/LibEthLog.sol和tool/LibPaillier.sol。



### 2、C++处理EthCall

​         在C++中，需编写与Solidity调用相对应的函数，实现EthCall调用的处理。操作目录在 libevm/ethcall 。实现过程如下（以 EthCallDemo 为例）。

（1）在EthCallEntry.h 中的 EthCallIdList 中声明一个call id，与Solidity中的call id相对应。

```cpp
// EthCallEntry.h 中
enum EthCallIdList:callid_t
{
    LOG         = 0x10,         
    PAILLIER    = 0x20,         
    DEMO        = 0x66666       ///< demo，与Solidity中相对应
};
```

（2）新建 EthCallDemo.h。若需要 EthCallDemo.cpp，则在 libevm/CMakeLists.txt 中添加相应的编译选项（为了说明简单，本例没有cpp文件）。

（3）在 EthCallDemo.h 中实现EthCall逻辑。需要注意几点：

​         a. 引用 ```#include "EthCallEntry.h" ```

​         b. 建一个新类，继承```EthCallExecutor```模板类，模板中的参数与Solidity中的ethcall接口调用时的参数类型对应（Solidity与C++的参数类型对应关系请参考 EthCallEntry.h）。

​         c. 在构造函数中调用```registerEthcall()``` ，将call id与此要实现的类绑定在一起。

​         d. 实现```ethcall()```函数，功能逻辑在此函数中实现。函数的参数同样需与Solidity中调用的参数对应。

​         e. 实现```gasCost()```函数，编写gas消耗的逻辑。

​         完整的代码如下：

```cpp
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

class EthCallDemo : EthCallExecutor<int, int, vector_ref<byte>, string> //按顺序定义除开callId之外的类型
{  
public:
    EthCallDemo()
    {
        //使用刚刚声明的CallId将EthCallDemo注册到ethCallTable中
        LOG(DEBUG) << "ethcall bind EthCallDemo--------------------";
        this->registerEthcall(EthCallIdList::DEMO); //EthCallList::DEMO = 0x66666为ethcall的callid
    }

    u256 ethcall(int a, int b, vector_ref<byte>bstr, string str) override //必须u256，override很重要
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

    uint64_t gasCost(int a, int b, vector_ref<byte>bstr, string str) override //必须uint64_t，override很重要 
    {
        return sizeof(a) + sizeof(b) + bstr.size() + str.length(); //消耗的gas一般与处理的数据长度有关
    }
};
///EthCallDemo举例结束///
}
}
```

（4）在 EthCallEntry.cpp 引用 EthCallDemo.h ，并且在 EthCallContainer 中实例化 EthCallDemo。

```cpp
// EthCallEntry.cpp 中
#include "EthCallDemo.h"

...

class EthCallContainer
{
public:
    //在此处实例化EthCall的各种功能
    EthLog ethLog;
    Paillier ethPaillier;
    EthCallDemo ethCallDemo;   ///< demo 在此处实例化
};
```

​         更完整的实现，可参考libevm/ethcall下的 EthLog 和 Paillier。



## 三、具体设计细节

###1、增加一条OPCODE

​        Solidity语言在运行前，被 solc 编译成 OPCODE。OPCODE 在 EVM 中被执行。EthCall 接口，通过增加一条OPCODE实现，新的OPCODE名为```ETHCALL```。所有EthCall的功能都通过此OPCODE调用，功能的区分通过call id完成。在编译器solc的代码 libevmcore/Instruction.h 和 libevmcore/Instruction.cpp 中，添加```ETHCALL```。之后重新编译solc的代码（即得到fisco-solc），即可编译别包含ethcall的的Solidity程序。再在fisco-bcos的代码的相同位置添加```ETHCALL```，并编写相应逻辑，即可识别```ETHCALL```。

​        定义OPCODE ```ETHCALL``` :

```cpp
// Instruction.h
enum class Instruction: uint8_t
{
	STOP = 0x00,        ///< halts execution
	ADD,                ///< addition operation
	MUL,                ///< mulitplication operation
	...
	SHA3 = 0x20,        ///< compute SHA3-256 hash
	ETHCALL = 0x2f,     ///< call eth kernel function api   <---------新增加的OPCODE

	ADDRESS = 0x30,     ///< get address of currently executing account
	BALANCE,            ///< get balance of the given account
	ORIGIN,             ///< get execution origination address
	...
```

​        添加```ETHCALL``` 的处理逻辑：

```c++
// libevm/VM.cpp
void VM::interpretCases()
{
  ...
  		CASE_BEGIN(ADD) 
		//OPCODE ADD 处理逻辑
		CASE_END
          
        CASE_BEGIN(MUL)
		//OPCODE MUL 处理逻辑
		CASE_END
          
        CASE_BEGIN(SHA3)
		//OPCODE SHA3 处理逻辑
		CASE_END

#ifdef EVM_ETHCALL
		CASE_BEGIN(ETHCALL)		// <------ case ETHCALL
		{
			ON_OP();
			u256 ret = ethcallEntry(this, m_sp); // <------ 传入堆栈指针，供parer用
			m_sp -= 9;
			*m_sp = ret;
			++m_pc;
		}
		CASE_END
#endif
```

###2、参数Parser

​        在Solidity调用EthCall接口，参数以堆栈的形式传给底层模块。底层模块为了能够正确获取到调用参数，需要依次出栈，并将Solidity类型的数据转换成C++类型的数据。在此处实现了一个Parser来完成此功能。

​        以第一节给出的同态加法例子为例，C++中可编写如下Parser程序获取调用参数。

```c++
//Parser举例--同态加参数获取
EthCallParamParser parser;
parser.setTargetVm(vm);

callid_t callId;
string d1, d2;
vector_ref<char> result; 	//<------- 返回值定义成引用类型
parser.parse(sp, callId, result, d1, d2);  //<--------- 根据堆栈指针sp，建立参数的映射，获取参数值

```

​        在实际实现时，开发者并不需要写上面的代码，Parser对开发者来说是隐藏的，EthCall的接口实现会自动实现上述代码，并建立参数的映射关系。开发者要做的，只是根据Solidity调用ethcall的参数，编写C++的```ethcall()```函数即可。设计细节将在“自动参数映射”一节给出。

###3、Call ID

​        Call ID的实现，使得EthCall仅仅只需添加一条OPCODE，即可实现各种功能。开发者在Solidity端调用ethcall时，传入的第一个参数为Call ID，在底层，通过Call ID索引不同的模块，完成不同的功能。

```c++
// libevm/ethcall/EthCallEntry.cpp
u256 ethcallEntry(VM *vm, u256* sp)
{
    EthCallParamParser parser;
    parser.setTargetVm(vm);

    callid_t callId;
    parser.parse(sp, callId);     //<-----  先parse出Call ID
    --sp;

    ethCallTable_t::iterator it = ethCallTable.find(callId); //<------- 根据Call ID索引不同的模块
    if(ethCallTable.end() == it)
    {
        LOG(ERROR) << "EthCall id " << callId << " not found";
        BOOST_THROW_EXCEPTION(EthCallNotFound());//Throw exception if callId is not found
    }

    return it->second->run(vm, sp);   //<--------- 调用相应模块
}
```

###4、自动参数映射

​        Parser对于开发者来说，是隐藏的。开发者直接根据Solidity调用ethcall时的参数，编写C++的```ethcall()```函数。EthCall就会自动为其建立参数映射关系，自动填充参数的值。

自动参数映射，将parser隐藏，开发者不需要自己写parser，而是直接根据Solidity调用ethcall时的参数，编写C++的```ethcall()```函数。

​        例如在Solidity中：

```Solidity
r := ethcall(call_id, ret, d1, d2, 0, 0, 0, 0, 0, 0)  
```

​        C++对应实现```ethcall()```函数，参数会自动建立映射关系，填充到C++函数的```result```，```d1```，```d2```中：

```c++
u256 ethcall(vector_ref<char> result, std::string d1, std::string d2)
{
    .........
}
```

​        实现方式是通过可变参数模板实现的（更多细节请看源代码libevm/ethcall/EthCallEntry.h）。

```c++
// libevm/ethcall/EthCallEntry.h
template<typename ... T>
class EthCallExecutor : EthCallExecutorBase
{
public:
    void registerEthcall(enum EthCallIdList callId)
    {
        assert(ethCallTable.end() == ethCallTable.find(callId));

        ethCallTable[callId] = (EthCallExecutorBase*)this;
    }

    u256 run(VM *vm, u256* sp) override  //<---------- 模块运行函数 run()
    {
        EthCallParamParser parser;
        parser.setTargetVm(vm);
        std::tuple<T...> tp;//通过tuple的方式实现可变长的参数，能够对参数parse并向func传参
        parser.parseToTuple(sp, tp);  //<-------  调用parser，建立映射，填充参数值
        return runImpl(vm, tp, make_index_sequence<sizeof...(T)>());   
    }

    template<typename TpType, std::size_t ... I>
    u256 runImpl(VM *vm, TpType& tp, index_sequence<I...>)
    {
        //自动把tuple中的参数展开，传入ethcall中
        this->updateCostGas(vm, gasCost(std::get<I>(tp)...)); 
        return ethcall(std::get<I>(tp)...);  //<------ 自动把变长参数展开，调用ethcall()
    }

    virtual u256 ethcall(T...args) = 0;
    virtual uint64_t gasCost(T ...args) = 0;

};
```



