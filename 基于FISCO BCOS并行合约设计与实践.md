# 基于FISCO BCOS并行合约设计与实践

本文将探讨合约开发如何提高并行度的问题,开发怎么样的合约可以被并行处理。

> 作者：
>
> - 江西师范大学 王江宇
> - 深圳职业技术大学 张宇豪

## 1.什么是并行合约？

> 贴个官方网址：https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/manual/transaction_parallel.html

​		并行合约是指能够在区块链网络上以并行方式执行的智能合约。传统的智能合约通常是以串行方式执行的，也就是按照顺序逐个执行交易。这种串行执行可能导致执行速度较慢，尤其是当合约逻辑复杂或涉及大量数据处理时。

​		为了提高智能合约的执行效率，一些区块链平台引入了并行合约的概念。并行合约允许多个交易同时进行处理，这样可以通过并行计算的方式提高整体的处理速度。不同的交易可以在不同的线程或进程中并行地执行，从而减少了串行执行的延迟。

​		并行合约的实现可能涉及多个方面。首先，合约需要被设计为可以并行处理的，即任务之间没有明显的依赖关系，并且可以独立执行。其次，智能合约的底层区块链平台需要支持并行处理的机制，例如多线程或多进程执行。此外，对于并行处理的合约，还需要考虑并发安全性和资源管理等问题，以确保正确性和可靠性。

​		总结来说，通过实现并行合约，可以有效利用计算资源并提高智能合约的执行效率。这对于处理大量数据、复杂逻辑或需要高吞吐量的应用场景尤为重要。然而，并行合约的具体实现方式和支持程度可能因区块链平台而异，因此在选择使用并行合约时需要进行相应的研究和评估。



## 2.互斥的概念

"并行合约的互斥"是指在多个并行运行的智能合约之间实施互斥（Mutual Exclusion）措施，以确保它们在处理共享资源时不会产生冲突或竞争条件。

具体来说，智能合约是在区块链网络上执行的自动化代码，它可以实现各种功能，如转账、记录数据等。在某些情况下，多个合约可能需要同时操作或访问共享的资源，例如数据库、状态变量或其他数据。

并行合约的互斥的概念可以通过以下示例进行解释：

假设有两个智能合约 A 和 B，它们都需要访问共享资源 R（例如某个区域的快递派送情况）。如果没有适当的互斥机制，以下情况可能发生：

1. **竞争条件：** 合约 A 和合约 B 同时尝试修改资源 R 的内容，可能导致不一致的状态或数据损坏。
2. **死锁：** 合约 A 锁定资源 R，然后尝试锁定资源 S，而合约 B 同时锁定资源 S，然后尝试锁定资源 R。这种情况下，两个合约会陷入无限等待，无法继续执行。



在链接文档中提到：

- **互斥参数**：合约**接口**中，与合约存储变量的“读/写”操作相关的参数。例如转账的接口transfer(X, Y)，X和Y都是互斥参数。
- **互斥对象**：一笔**交易**中，根据互斥参数提取出来的、具体的互斥内容。例如转账的接口transfer(X, Y), 一笔调用此接口的交易中，具体的参数是transfer(A, B)，则这笔操作的互斥对象是[A, B]；另外一笔交易，调用的参数是transfer(A, C)，则这笔操作的互斥对象是[A, C]。

> **判断同一时刻两笔交易是否能并行执行，就是判断两笔交易的互斥对象是否有交集。相互之间交集为空的交易可并行执行。**



## 3.并行合约的必要条件

一共需要遵守如下的三个规则。

**3.1 确定接口是否可并行**

可并行的合约接口，必须满足：

- 无调用外部合约
- 无调用其它函数接口

例如如下：

![截图 2023-08-17 19-33-19](https://blog-1304715799.cos.ap-nanjing.myqcloud.com/imgs/截图 2023-08-17 19-33-19.png)



**3.2 确定互斥参数**

在编写接口前，先确定接口的互斥参数，接口的互斥即是对全局变量的互斥，互斥参数的确定规则为：

- 接口访问了全局mapping，mapping的key是互斥参数
- 接口访问了全局数组，数组的下标是互斥参数
- 接口访问了简单类型的全局变量，所有简单类型的全局变量共用一个互斥参数，用不同的变量名作为互斥对象

还是使用上面一样的：

![截图 2023-08-17 19-33-19](https://blog-1304715799.cos.ap-nanjing.myqcloud.com/imgs/截图 2023-08-17 19-33-19.png)



**3.3 确定参数类型和顺序**

确定互斥参数后，根据规则确定参数类型和顺序，规则为：

- 接口参数仅限：**string、address、uint256、int256**（未来会支持更多类型）
- 互斥参数必须全部出现在接口参数中
- 所有互斥参数排列在接口参数的最前



## 4.FISCO并行计算的框架

### 4.1 ParallelContract合约

```solidity
pragma solidity ^0.4.25;

// 预编译合约 ParallelConfigPrecompiled
contract ParallelConfigPrecompiled {
    // 注册并行函数
    // 参数1：并行函数地址
    // 参数2：并行函数名称
    // 参数3：致命区间值，用于限制并行计算区间大小
    function registerParallelFunctionInternal(address, string, uint256) public returns (int);
    
    // 注销并行函数
    // 参数1：并行函数地址
    // 参数2：并行函数名称
    function unregisterParallelFunctionInternal(address, string) public returns (int);    
}
 
// 并行合约基类
contract ParallelContract {
    // 实例化并使用 ParallelConfigPrecompiled 合约
    ParallelConfigPrecompiled precompiled = ParallelConfigPrecompiled(0x1006);
    
    // 注册并行函数，注册后可以在并行计算中使用
    // 参数1：并行函数名称
    // 参数2：致命区间值，用于限制并行计算区间大小
    function registerParallelFunction(string functionName, uint256 criticalSize) public 
    {
        precompiled.registerParallelFunctionInternal(address(this), functionName, criticalSize);
    }
    
    // 注销并行函数，在停用并行计算前建议注销所有已注册的并行函数
    // 参数1：并行函数名称
    function unregisterParallelFunction(string functionName) public
    {
        precompiled.unregisterParallelFunctionInternal(address(this), functionName);
    }
    
    // 启用并行计算功能
    function enableParallel() public;
    
    // 关闭并行计算功能
    function disableParallel() public;
}
```



### 4.2 解析合约代码

> 上面的整个合约包含如下内容：

ParallelConfigPrecompiled 预编译合约：

这部分合约定义了名为 `ParallelConfigPrecompiled` 的合约，它用于在区块链上预先配置并行计算的相关设置。

1. `registerParallelFunctionInternal` 函数：该函数用于注册并行函数，它接收三个参数：并行函数的地址、函数名称以及一个称为 "criticalSize" 的参数。该参数用于限制并行计算区间的大小。
2. `unregisterParallelFunctionInternal` 函数：这个函数用于注销已注册的并行函数，同样接受函数地址和名称作为参数。



ParallelContract 并行合约基类：

这部分合约定义了一个名为 `ParallelContract` 的合约，它作为并行计算的基类，可以被其他合约继承并使用。

1. `ParallelConfigPrecompiled` 实例化：在该合约内部实例化了 `ParallelConfigPrecompiled` 合约，并使用了地址 `0x1006` 来初始化。
2. `registerParallelFunction` 函数：这个函数是用于在继承的合约中注册并行函数的。它接收函数名称和 "criticalSize" 参数，并通过调用预编译合约的 `registerParallelFunctionInternal` 函数来注册函数。这允许继承合约在并行计算中使用已注册的函数。
3. `unregisterParallelFunction` 函数：用于在继承的合约中注销已注册的并行函数。它接收函数名称，然后通过调用预编译合约的 `unregisterParallelFunctionInternal` 函数来注销函数。
4. `enableParallel` 函数和 `disableParallel` 函数：这两个函数用于启用和关闭并行计算功能。



## 5.并行合约模拟业务设计

### 5.1 业务设计思路

![截图 2023-08-17 18-36-01](https://blog-1304715799.cos.ap-nanjing.myqcloud.com/imgs/截图 2023-08-17 18-36-01.png)



### 5.2 详细业务说明

该业务主要涉及快递派送、签收流程以及售后操作。快递员负责派送快递，签收方可以对签收后的物流进行售后操作。我们这里不涉及一个快递员针对所有的用户进行派送，显示业务可以，这里只做模拟演示。

**角色定义：**

- `快递员`：负责派送快递到指定区域。
- `签收方`：负责在指定区域接收并签收快递。

**流程设计：**

- `派送流程`： 快递员根据区域划分将快递派送到对应的签收方处。
- `签收流程`： 签收方在收到快递后，可以进行签收操作。签收后，系统记录签收时间及状态。
- `售后流程`： 签收方可以对签收后的物流进行售后操作，包括提出问题、退货、换货等。售后请求会被记录，并通知相关部门处理。

**并行合约设计部分：**

- `派送业务`：A快递员拿取快件给B用户派送快递，B快递员拿取快件给C用户派送快递，此时A快递员不可能给C用户派送快递。可以实现多快递员同时拿取快件进行不同地方的派件。
- `售后业务`：A用户和B用户如果都签收之后，A用户可以向该物流进行售后，B用户也可以对物流进行售后，但是对应的商家和物流可能不一样，所以也可以同时进行操作。



## 6.具体合约实现与测试

环境搭建：

- FISCO BCOS 2.9.2 
- WeBASE-Front 1.5.5 

![截图 2023-08-17 20-02-04](https://blog-1304715799.cos.ap-nanjing.myqcloud.com/imgs/截图 2023-08-17 20-02-04.png)



### 6.1 并行合约

```solidity
pragma solidity ^0.4.25;

// 预编译合约 ParallelConfigPrecompiled
contract ParallelConfigPrecompiled {
    // 注册并行函数
    // 参数1：并行函数地址
    // 参数2：并行函数名称
    // 参数3：致命区间值，用于限制并行计算区间大小
    function registerParallelFunctionInternal(address, string, uint256) public returns (int);
    
    // 注销并行函数
    // 参数1：并行函数地址
    // 参数2：并行函数名称
    function unregisterParallelFunctionInternal(address, string) public returns (int);    
}
 
// 并行合约基类
contract ParallelContract {
    // 实例化并使用 ParallelConfigPrecompiled 合约
    ParallelConfigPrecompiled precompiled = ParallelConfigPrecompiled(0x1006);
    
    // 注册并行函数，注册后可以在并行计算中使用
    // 参数1：并行函数名称
    // 参数2：致命区间值，用于限制并行计算区间大小
    function registerParallelFunction(string functionName, uint256 criticalSize) public 
    {
        precompiled.registerParallelFunctionInternal(address(this), functionName, criticalSize);
    }
    
    // 注销并行函数，在停用并行计算前建议注销所有已注册的并行函数
    // 参数1：并行函数名称
    function unregisterParallelFunction(string functionName) public
    {
        precompiled.unregisterParallelFunctionInternal(address(this), functionName);
    }
    
    // 启用并行计算功能
    function enableParallel() public;
    
    // 关闭并行计算功能
    function disableParallel() public;
}
```



### 6.2 StorageData合约

**数据结构定义**

**User 结构**：描述了一个快递员的信息。包括：

- `u_address`：用户的地址。
- `role`：用户的角色。

**SignRecord 结构**：描述了一个签收记录的信息。包括：

- `record_id`：记录的唯一标识。
- `c_address`：客户地址。
- `u_address`：快递员地址。
- `sign_time`：签收时间。

**状态变量**

1. `RecordCount`：记录了签收记录的数量。
2. `SignRecordList`：一个动态数组，存储所有签收记录的标识。
3. `UserIsSignStatus`：映射，记录了用户是否已签收的状态。
4. `UserIsClaimStatus`：映射，记录了用户是否已提出售后申请的状态。
5. `UserMap`：映射，将用户地址与其对应的 `User` 结构关联起。
6. `RecordMap`：映射，将记录标识与其对应的 `SignRecord` 结构关联起来。

**事件定义**

1. `Registered` 事件：在用户注册时触发，记录用户的地址和角色信息。
2. `Signed` 事件：在用户签收操作时触发，记录用户的地址和签收状态。
3. `Refusal` 事件：在用户拒收操作时触发，记录用户的地址和拒收时间。

```solidity
pragma solidity ^0.4.25;

// 数据结构合约
contract StorageData {
    // 快递员
    struct User{
        address u_address;  // 用户的地址
        uint    role;       // 角色
    }
    
    // 签收记录
    struct SignRecord{
        uint record_id;
        address c_address;
        address u_address;
        uint256 sign_time;
    }
    
    // 签收记录ID
    uint256 public RecordCount;
    // 签收的记录数组
    uint256[] public SignRecordList;
    
    
    // 用户签收的状态
    mapping(address => bool) public UserIsSignStatus;
    // 用户售后申请
    mapping(address => bool) public UserIsClaimStatus;
    // 用户注册
    mapping(address => User) public UserMap;
    // 生成记录
    mapping(uint256 => SignRecord) public RecordMap;
    
    
    // 用户注册的事件
    event Registered(address indexed _userAddress,uint indexed _role);
    
    
    // 用户签收的事件
    event Signed(address indexed _userAddress,uint256 indexed _isSign);
    
    
    // 用户拒收事件
    event Refusal(address indexed _userAddress,uint indexed _time);
    
}
```



### 6.3 Delivery合约

1. `IsCourier` 修饰器：该修饰器要求被修饰的函数只能由快递员调用。
2. `userRegister` 函数：用于用户注册，接受一个参数 `_userAddress`，将用户地址与角色信息进行关联，设置为普通用户（角色为1）。
3. `courierRegister` 函数：用于快递员注册，接受一个参数 `_courierAddress`，将快递员地址与角色信息进行关联，设置为快递员（角色为2）。
4. `courierToUserSign` 函数：用于快递员签收操作，接受 `_from` 快递员地址、`_to` 用户地址和 `_isSign` 是否签收的标识参数。函数内部生成签收记录并存储到 `RecordMap` 中，同时更新签收状态、触发相应事件。
5. `afterSales` 函数：用于用户进行售后操作，接受 `_from` 用户地址和 `_isClaim` 是否申请售后的标识参数。根据 `_isClaim` 更新用户的售后申请状态。
6. `selectSignRecordList` 函数：查询所有的签收记录，返回一个包含所有签收记录的数组。
7. `enableParallel` 函数：重写了基类的启用并行计算功能，注册了两个并行函数：`courierToUserSign` 和 `afterSales`。
8. `disableParallel` 函数：重写了基类的禁用并行计算功能，注销了之前注册的并行函数。

> 这里courierToUserSign函数的前两个参数分别是address，为互斥参数。afterSales函数的前一个参数是互斥参数。

```solidity
pragma solidity ^0.4.25;
pragma experimental ABIEncoderV2;

// 派送合约
import "./StorageData.sol";
import "./ParallelContract.sol";
contract Delivery  is StorageData,ParallelContract {
    
    // 判断当前是否为快递员
    modifier IsCourier(address courierAddress) {
        require(UserMap[courierAddress].role == 2,"当前用户不是快递员");
        _;
    }
    
    // 用户的注册业务
    function userRegister(address _userAddress) public {
        UserMap[_userAddress].u_address = _userAddress;
        UserMap[_userAddress].role = 1;
    }
    
    // 快递员注册业务
    function courierRegister(address _courierAddress) public {
        UserMap[_courierAddress].u_address = _courierAddress;
        UserMap[_courierAddress].role = 2;
    }
    
    // 签收生成签收记录
    function courierToUserSign(address _from,address _to,uint256 _isSign) public IsCourier(_from) {
        // 生成签收完成的订单记录
        RecordCount++;
        SignRecord storage _signRecord = RecordMap[RecordCount];
        _signRecord.record_id = RecordCount;
        _signRecord.c_address = _from;
        _signRecord.u_address = _to;
        _signRecord.sign_time = block.timestamp;
        
        SignRecordList.push(RecordCount);
        
        if (_isSign == 0){
            // 设置该用户已签收
            UserIsSignStatus[_to] = true;
        }else {
            // 设置用户拒绝签收
            UserIsSignStatus[_to] = false;
            emit Refusal(_to,block.timestamp);
        }
        emit Signed(_to,_isSign);
    }
    
    // 对物流进行售后
    function afterSales(address _from,uint256 _isClaim) public {
        require(UserIsClaimStatus[_from] == false,"当前用户已经申请售后");
        if (_isClaim == 0){
            UserIsClaimStatus[_from] = true;
        }else {
            UserIsClaimStatus[_from] = false;
        }
    }
    
    // 查询所有的签收记录
    function selectSignRecordList() public returns(SignRecord[] memory) {
        SignRecord[] memory recordList = new SignRecord[](SignRecordList.length);
        for (uint i = 0; i < SignRecordList.length; i++){
            recordList[i] = RecordMap[SignRecordList[i]];
        }
        return recordList;
    }
    
    // 声明函数 enableParallel，用于启用并行计算功能
    function enableParallel() public {
        registerParallelFunction("courierToUserSign(address,address,uint256)",2);
        registerParallelFunction("afterSales(address,uint256)",1);
    }
    
    // 声明函数 disableParallel，用于禁用并行计算功能
    function disableParallel() public {
        unregisterParallelFunction("courierToUserSign(address,address,bool)");
        unregisterParallelFunction("afterSales(address,uint256)");
    }
}
```



> 如下代码作为解析：

- **courierToUserSign**签收业务
  - 这里入参分别为两个用户地址，一个是快递员一个是用户。然后签收状态码，对前两个参数需要做互斥的判断，在业务中我涉及了生成签收的记录，然后修改了用户的签收状态，这里我分别使用了maping的设计，虽然对存储的消耗会稍微大一点，但是保证了每一个key不一样。
  - 通过状态码的判断，对用户的签收状态使用mapping的方式更新。
- **afterSales**售后业务
  - 这里的售后使用的是mapping的方式更新，保证了每一个key都是不一样的， 具体的业务可以去根据该mapping对应的value去判断，再去进行详细的业务操作。

```solidity
   // 签收生成签收记录
    function courierToUserSign(address _from,address _to,uint256 _isSign) public IsCourier(_from) {
        // 生成签收完成的订单记录
        RecordCount++;
        SignRecord storage _signRecord = RecordMap[RecordCount];
        _signRecord.record_id = RecordCount;
        _signRecord.c_address = _from;
        _signRecord.u_address = _to;
        _signRecord.sign_time = block.timestamp;
        
        SignRecordList.push(RecordCount);
        
        if (_isSign == 0){
            // 设置该用户已签收
            UserIsSignStatus[_to] = true;
        }else {
            // 设置用户拒绝签收
            UserIsSignStatus[_to] = false;
            emit Refusal(_to,block.timestamp);
        }
        emit Signed(_to,_isSign);
    }
    
    // 对物流进行售后
    function afterSales(address _from,uint256 _isClaim) public {
        require(UserIsClaimStatus[_from] == false,"当前用户已经申请售后");
        if (_isClaim == 0){
            UserIsClaimStatus[_from] = true;
        }else {
            UserIsClaimStatus[_from] = false;
        }
    }
```



### 6.4 部署调用

分别创建四个用户，2个快递员，2个用户。

![截图 2023-08-17 20-29-45](https://blog-1304715799.cos.ap-nanjing.myqcloud.com/imgs/截图 2023-08-17 20-29-45.png)

在Delivery合约中我添加了初始化构造函数，默认注册了2个快递员和2个用户。

```solidity
    constructor() public {
        userRegister(0x871c15e058f81906e9b7faea37b0023a3a13fdc6);
        userRegister(0x081ae64bb3e2d480b6dfd5b994451420ee58830e);
        
        courierRegister(0x442234617ffb5e26892a8e329f42cf23dd9a0ff9);
        courierRegister(0xc3990fe75c7b2354b80fa0a90bb3cd7544101168);
    }
    
```

部署合约之后，调用enableParallel函数，开启两个函数允许并行执行。

![截图 2023-08-17 20-40-10](https://blog-1304715799.cos.ap-nanjing.myqcloud.com/imgs/截图 2023-08-17 20-40-10.png)

调用courierToUserSign函数，courier1给user1用户签收。然后courier2给user2用户签收。签收状态码是0。

![截图 2023-08-17 20-33-51](https://blog-1304715799.cos.ap-nanjing.myqcloud.com/imgs/截图 2023-08-17 20-33-51.png)

调用回执如下：

![截图 2023-08-17 20-35-56](https://blog-1304715799.cos.ap-nanjing.myqcloud.com/imgs/截图 2023-08-17 20-35-56.png)

根据如上操作然后继续完成courier2对user2用户的签收。

接下来调用selectSignRecordList函数，查看所有的签收记录情况。

![image-20230817203731156](https://blog-1304715799.cos.ap-nanjing.myqcloud.com/imgs/image-20230817203731156.png)

如下是回执信息：

![截图 2023-08-17 20-38-20](https://blog-1304715799.cos.ap-nanjing.myqcloud.com/imgs/截图 2023-08-17 20-38-20.png)



> 到这里肯定会有同学说： 你这才调用了2次啊？还是单次调用这样，并没有实现并行合约啊，并没有体现并行合约执行的效果。
>
> 答疑：
>
> - 其实这里需要多个客户端同时执行该函数，同时签收
> - 我们需要达到较高的TPS的情况下才能看到具体的效果



### 6.5 Java SDK 模拟测试

- 这里使用的是Springboot编写的一个API接口http://localhost:8080/delivery/sign进行测试
- 简单的使用了一下线程池模拟测试了一下并行合约的执行效率，我们需要调用enableParallel函数开启并行执行。

> 也可以结合Java-SDK-Demo进行测试。

```java
@Slf4j
@RestController
@RequestMapping("/delivery")
public class DeliveryController {

    @Autowired
    private DeliveryService deliveryService;

    @GetMapping("/sign")
    public void courierToUserSign(){
        // 开启允许并行执行
        try
        {
            deliveryService.enableParallel();
        } catch (Exception e) {
            throw new RuntimeException(e);
        }

        // 直接批量注册用户和快递员
        ArrayList<String> userList = initUserAddress();
        ArrayList<String> courierList = initUserAddress();
        InitUserAndCourier(userList, courierList);


        ExecutorService threadPool = Executors.newFixedThreadPool(100); // 设置线程池大小
        int numTasks = courierList.size(); // 任务数量
        int numThreads = 20; // 并发线程数
        int tasksPerThread = numTasks / numThreads; // 每个线程需要执行的任务数量

        // 多个线程同时执行 模拟多快递员进行多用户的签收
        for (int i = 0; i < numThreads; i++) {
            int startIdx = i * tasksPerThread;
            int endIdx = (i == numThreads - 1) ? numTasks : startIdx + tasksPerThread;

            threadPool.execute(() -> {
                for (int j = startIdx; j < endIdx; j++) {
                    DeliveryCourierToUserSignInputBO signInputBO = new DeliveryCourierToUserSignInputBO();
                    signInputBO.set_isSign(BigInteger.valueOf(0));
                    signInputBO.set_from(courierList.get(j));
                    signInputBO.set_to(userList.get(j));
                    try {
                        deliveryService.courierToUserSign(signInputBO);
                    } catch (Exception e) {
                        throw new RuntimeException(e);
                    }
                }
            });
        }
        threadPool.shutdown();
    }

    // 初始化用户和快递员
    private void InitUserAndCourier(ArrayList<String> userList, ArrayList<String> courierList) {
        // 注册用户
        for (String s : userList) {
            DeliveryUserRegisterInputBO userRegisterInputBO = new DeliveryUserRegisterInputBO();
            userRegisterInputBO.set_userAddress(s);
            try {
                deliveryService.userRegister(userRegisterInputBO);
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        }
        // 注册快递员
        for (String s : courierList) {
            DeliveryCourierRegisterInputBO courierRegisterInputBO = new DeliveryCourierRegisterInputBO();
            courierRegisterInputBO.set_courierAddress(s);
            try {
                deliveryService.courierRegister(courierRegisterInputBO);
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        }
    }

    // 初始化用户地址
    private ArrayList<String> initUserAddress() {
        ArrayList<String> userAddressList = new ArrayList<>();
        for (int i = 0; i < 20; i++) {
            CryptoSuite cryptoSuite = new CryptoSuite(CryptoType.ECDSA_TYPE);
            CryptoKeyPair keyPair = cryptoSuite.createKeyPair();
            userAddressList.add(keyPair.getAddress());
        }
        return userAddressList;
    }
}
```

