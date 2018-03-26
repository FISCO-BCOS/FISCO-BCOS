# EthCall 说明文档

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

### 3、优势

​        EthCall为Solidity提供了一种**函数式**调用底层C++模块的方法。存在以下优势：

（1）函数式调用，支持传递引用参数。调用接口开销低，编程实现简单方便。

（2）C++代码运行复杂逻辑，执行效率高。

（3）可调用C++库，功能强大，灵活性强，降低合约开发难度。



## 二、使用方法

​         EthCall的使用，可分为两部分。在Solidity端，向EthCall的传参，并使用支持EthCall的编译器进行编译。在C++端，编写与Solidity端参数对应的接口函数，并实现需要实现的逻辑。具体描述如下。

### 1、Solidity调用EthCall

**编译器**

​         支持EthCall的编译器在项目的根目录下，为fisco-solc。使用方式与一般的Solidity编译器相同。

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



## 三、日志、异常处理方法

### 1. 程序运行时，log报错：EthCall id XX not found

​         找不到相应的call id，请检查```EthCallContainer```中是否有你的类的实例化定义。



### 2. Solidity编译不通过

​         请确认：

​	（1）是否用的 fisco-solc 编译器进行编译。

​	（2）ethcall调用时是否用0补齐10个参数。



### 3. C++程序编译不通过

​         请确认：

​	（1）EthCallEntry.cpp 是否include了你的.h文件。你的.h文件是否include了 EthCallEntry.h

​	（2）参数对应是否正确：```EthCallExecutor```类模板参数，```ethcall()```参数，```gasCost()```参数。对应参数时，无需对应call id的类型。

​	（3）C++端的ethcall在定义时是否加了override关键字，返回值类型必须为```u256。``` 。

​	（4）目前不支持array(vector) 类型的调用。



### 4. C++的ethcall接口无法正确获取Solidity调用的值

​         请确认：

​	（1）参数对应是否正确：```EthCallExecutor```类模板参数，```ethcall()```参数，```gasCost()```参数。无需对应call id的类型。

​	（2）C++端的ethcall在定义时是否加了override关键字。

​	（3）string，bytes是否定义成memory类型。

​	（4）目前不支持array(vector) 类型的调用。

