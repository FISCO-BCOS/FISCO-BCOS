<center> <h1>并行计算</h1> </center>

<a name="summary" id="summary"></a>
# 功能介绍
在研究和实现区块链平台和进行业务落地的过程中，我们意识到，区块链的运行速度会受多种因素影响，包括加密解密计算、交易广播和排序、共识算法多阶段提交的协作开销、虚拟机执行速度，以及包括CPU核数主频、磁盘IO、网络带宽等。由于区块链是先天的跨网络的分布式协作系统，而且强调安全性、可用性、容错性、一致性、事务性，用较复杂的算法和繁琐的多参与方协作来获得去信任化、数据不可篡改以及交易可追溯等特出的功能优势，根据分布式的CAP原理，在同等的硬件资源投入的前提下，区块链的性能往往低于中心化的系统，其表现就是并发数不高，交易时延较明显。

我们已经在多个方面对系统运行的全流程进行细致的优化，包括加密解密计算，交易处理流程，共识算法，存储优化等，使我们的区块链平台在单链架构时，运行速度达到了一个较高的性能水准，基本能满足一般的金融业务要求。

同时我们也意识到，对于用户数、交易量、存量数据较大或可能有显著增长的海量服务场景，对系统提出了更高的容量和扩展性要求，单链架构总是会遇到软件架构或硬件资源方面的瓶颈。
而区块链的系统特性决定，在区块链中增加节点，只会增强系统的容错性，增加参与者的授信背书等，而不会增加性能，这就需要通过架构上的调整来应对性能挑战，所以，我们提出了“并行计算，多链运行”的方案。



# 使用方式

### 搭建eth：

至少部署配置链，热点链，用户链1，用户链2。每条链的节点个数任意，至少为1。

搭建步骤：请参照白皮书

部署系统合约：

1. cd systemcontractv2
2. babel-node deploy.js
3. 得到系统代理合约地址后，替换到阶段的config.json的systemproxyaddress

重启每条链的所有节点，然后选择每一条链中的某个节点部署Meshchain.sol合约：

1. cd tool
2. vim config.json,修改为节点的rpc端口
2. babel-node deploy.js Meshchain
3. babel-node abi_name_service_tool.js add Meshchain


### 成功部署多条链后，开始一下步骤：

更改java proxy的applicationContext.xml(主要是链上链下的配置)，主要注意的是每个bean id的名字，nodeid，ip和端口。bean id需要跟conf下面<setNameList>id1,id2...</setNameList>,用逗号分隔，保持一致。nodeid，rpc端口需要选择跟每条链中的某个节点的nodeid保持一致。


下面是举例的配置

```
<bean id="routeChain" class="cn.webank.channel.client.Service">
	<property name="threadPool">
		<bean class="org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor">
			<property name="corePoolSize" value="50" />
			<property name="maxPoolSize" value="100" />
			<property name="queueCapacity" value="500" />
			<property name="keepAliveSeconds" value="60" />
			<property name="rejectedExecutionHandler">
				<bean class="java.util.concurrent.ThreadPoolExecutor.AbortPolicy" />
			</property>
		</bean>
	</property>
	<property name="orgID" value="WB" />
	<property name="allChannelConnections">
		<map>
		<entry key="WB">
			<bean class="cn.webank.channel.handler.ChannelConnections">
				<property name="connectionsStr">
					<list>
						<value>50f720ddd5d2f7feefb22bf62011a73878f80180d56bd7f4deab58173847c442b35256634950497a0208dacd3ff6ed1a2a31e97976d8dddbd64dc124a6e8a284@127.0.0.1:30339</value>
					</list>
				</property>
			</bean>
		</entry>
		</map>
	</property>
</bean>

<bean id="hotChain" class="cn.webank.channel.client.Service">
	<property name="threadPool">
		<bean class="org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor">
			<property name="corePoolSize" value="50" />
			<property name="maxPoolSize" value="100" />
			<property name="queueCapacity" value="500" />
			<property name="keepAliveSeconds" value="60" />
			<property name="rejectedExecutionHandler">
				<bean class="java.util.concurrent.ThreadPoolExecutor.AbortPolicy" />
			</property>
		</bean>
	</property>
	<property name="orgID" value="WB" />
	<property name="allChannelConnections">
		<map>
		<entry key="WB">
			<bean class="cn.webank.channel.handler.ChannelConnections">
				<property name="connectionsStr">
					<list>
						<value>23142aac805fa2ef9bee4a7b9489445e2706628c1b93a2a678162c187bed19028e1ce6da1f455d5d49014e6075c3a98ae143b793081eb42aee66ca5773e6f4a5@127.0.0.1:30340</value>
					</list>
				</property>
			</bean>
		</entry>
		</map>
	</property>
</bean>

<bean id="set1Chain" class="cn.webank.channel.client.Service">
	<property name="threadPool">
		<bean class="org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor">
			<property name="corePoolSize" value="50" />
			<property name="maxPoolSize" value="100" />
			<property name="queueCapacity" value="500" />
			<property name="keepAliveSeconds" value="60" />
			<property name="rejectedExecutionHandler">
				<bean class="java.util.concurrent.ThreadPoolExecutor.AbortPolicy" />
			</property>
		</bean>
	</property>
	<property name="orgID" value="WB" />
	<property name="allChannelConnections">
		<map>
		<entry key="WB">
			<bean class="cn.webank.channel.handler.ChannelConnections">
				<property name="connectionsStr">
					<list>
						<value>fc292d42da074834fb37b850704b45f4a2a4efe15d50ca36c6ac9ddd63c9ce029cf9ac38e72917e162a8fa7449ec8df43a1df616702b0197e6bd5d9c0a1ae8d2@127.0.0.1:30338</value>
					</list>
				</property>
			</bean>
		</entry>
		</map>
	</property>
</bean>

<bean id="set2Chain" class="cn.webank.channel.client.Service">
	<property name="threadPool">
		<bean class="org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor">
			<property name="corePoolSize" value="50" />
			<property name="maxPoolSize" value="100" />
			<property name="queueCapacity" value="500" />
			<property name="keepAliveSeconds" value="60" />
			<property name="rejectedExecutionHandler">
				<bean class="java.util.concurrent.ThreadPoolExecutor.AbortPolicy" />
			</property>
		</bean>
	</property>
	<property name="orgID" value="WB" />
	<property name="allChannelConnections">
		<map>
		<entry key="WB">
			<bean class="cn.webank.channel.handler.ChannelConnections">
				<property name="connectionsStr">
					<list>
						<value>7fbf7094ad555f698a377293f93317c65274412bdb9b2da11d319c205742b6ede57d24ee2f1816ceea458cf76bf91afcc75e68b11dc136036f16ab0c35fa8263@127.0.0.1:30337</value>
					</list>
				</property>
			</bean>
		</entry>
		</map>
	</property>
</bean>

</beans>

```

### 然后更改配置文件config.xml

```
<?xml version="1.0" encoding="UTF-8" ?>
<config>
    <privateKey>bcec428d5205abe0f0cc8a734083908d9eb8563e31f943d760786edf42ad67dd</privateKey> <!--用作发送交易做签名的私钥-->
    <serviceId></serviceId> <!-- 微众RMB的ID,HTTP方式可忽略-->
    <scenario></scenario> <!-- 微众RMB的scenario,Http方式忽略 -->
    <routeAddress>0x44f9b7d2629581d706dfa5f7e12799c0edfda73d</routeAddress> <!-- 路由合约，这个需要执行下面部署路由合约的时候会返回的地址-->
    <setNameList>set1Chain,set2Chain</setNameList> <!--用户链的列表-->
    <hotChainName>hotChain</hotChainName> <!--热点链的名字-->
    <routeChainName>routeChain</routeChainName> <!--路由链链的名字-->
    <enableTimeTask>1</enableTimeTask> <!--是否开启定时任务-->
    <timeTaskIntervalSecond>60</timeTaskIntervalSecond> <!--定时任务间隔，秒为单位-->
</config>

```

1. serviceId：rmb id
2. routeAddress：路由链的地址，下面会说明如何获取
3. setNameList：对应的所有的子链名字，参照applicationContext.xml里面的所有子链配置的id
4. hotChainName: 对应热点链的名字，参照applicationContext.xml里面的热点链配置的id
5. routeChainName：对应路由链的名字，参照applicationContext.xml里面的路由链配置的id
6. enableTimeTask：是否开启relay task，0：不开启 1：开启
7. timeTaskIntervalSecond：定期任务间隔


### 再使用工具部署路由合约:

java -cp conf/:apps/*:lib/* cn.webank.proxy.tool.DeployContract deploy nodes.json


nodes.json 格式:

```
[
    {
        "set_name":"set1Service",
        "set_warn_num":8,
        "set_max_num":10,
        "set_node_list":[
            {
                "ip":"127.0.0.1",
                "p2p_port":30801,
                "rpc_port":8301,
                "node_id":"60652990f12303020b2e51be4effe056cd1ae3433d2749eddfe66294641990bb650220f486c7f26e200c910f4b4416ad43cd18b35908a5f3e83984de9c68458c",
                "type":1
            },
           {
                "ip":"127.0.0.1",
                "p2p_port":30802,
                "rpc_port":8302,
                "node_id":"b46aefcaec2e6fd621ea064ca77645c9655acff738f62c5f6cbfc290207298e59bdb3b5af16a1479522f072fef44e8c04543cdd4f84da10bf897d8b0dd43bdb3",
                "type":1
            },
	  {
                "ip":"127.0.0.1",
                "p2p_port":30803,
                "rpc_port":8303,
                "node_id":"154ab5eac409f841faf35c2e7e8c2434efb20335ee396c32723f0d7c3b9bedd56db373cafe397c6e23c72ce50b5552959c7f6cb4c73343a582d2509d679e86d5",
                "type":1
            }
	]
    },
    {
        "set_name":"set2Service",
        "set_warn_num":8,
        "set_max_num":10,
        "set_node_list":[
            {
                "ip":"127.0.0.1",
                "p2p_port":30801,
                "rpc_port":8301,
                "node_id":"1999877c0b966bdc5feb239c5563a1f8c52234f805f3265085baa66e3152302fe0cc3da9a10753c0d8284a6374dafc7fb04232acc5e467d6fa7f986c1a6d4a39",
                "type":1
            },
            {
                "ip":"127.0.0.1",
                "p2p_port":30802,
                "rpc_port":8302,
                "node_id":"1d9a844c9e22e3b38519d88707fcf56ae425285b5837a6fe5e1c1f66d21ca9eb15ea2dd743ca8b57c83bc2d3b51f295056bffe32d242b74869fa308f38e4cc0a",
                "type":1
            },
            {
                "ip":"127.0.0.1",
                "p2p_port":30803,
                "rpc_port":8303,
                "node_id":"40d0f3893184fb9f296b626dfdfb854ec5169103445dc9731faaa1de14fbbe8ec35bdad9720131dc70d568e2cff6a8cdb03fe39dddf361db8656efcebe4bf85d",
                "type":1
            },
            {
                "ip":"127.0.0.1",
                "p2p_port":30804,
                "rpc_port":8304,
                "node_id":"0c7cdbff612b0bb273c3f03a91e73fd3683470982b491de00cecae61c41b100535113036e3a7059bfe684a8ddf2a587413ba2d263e2e2d21a9114e849b4a8f62",
                "type":1
            }
        ]
    }
]
```

期间会有相关的日志'deployContract'输出到终端,来确认是否部署成功。

如果看到'register route contract success.address:'，后面跟着就是路由合约的地址，请填写到config.xml的routeAddress字段

路由合约的默认分配规则，一个set的uid个数最大为10.意味着1-10的uid会在set1，11-20会在set2...诸如类推。

rmb的报文或者http报文信息里面会有uid参数，用作路由。

### 再使用工具注册热点商户和虚拟商户

```
java -cp conf/:apps/*:lib/* cn.webank.proxy.tool.DeployContract registerMerchant 1 merchantName
```

1. registerMerchant是接口名字
2. 第一个参数是商户id
3. 第二个参数是商户名字

注册成功后，会有'registerMerchant ** onResponse data:0',必须为0才可以确保成功。其他则为失败

### 更多工具使用说明

```
java -cp conf/:apps/*:lib/* cn.webank.proxy.tool.DeployContract
```

注意:由于初始化的原因，需要等待若干秒

然后会有相关的工具使用说明:

```
usage:[deploy nodes.json
      [queryMerchantId $chainName, $requestStr
      [registerMerchant $merchantId, $merchantName
      [queryMerchantAssets $chainName, $requestStr
      [querySetUsers $setIdx(0代表set1, 1代表set2,类推)]
```

1. queryMerchantId 查询所有已存在的商户id，参数chainName为nodes.json里面的set_name，requestStr为 '{"contract":"Meshchain","func":"getAllMerchantIds","version":"","params":[]}'
2. queryMerchantAssets 查询商户资产，参数chainName为nodes.json里面的set_name，requestStr为'{"contract":"Meshchain","func":"getMerchantAssets","version":"","params":["$merchantId"]}'
3. deploy 发布路由合约，nodes.json见上述
4. querySetUsers 查询某个set的所有用户id，setIdx是一个set数组下标，0代表set1, 1代表set2等等

### 最后启动server监听:

http server:

```
nohup java -cp conf/:apps/*:lib/* -Dserver=http cn.webank.proxy.main.Start &
```


如果监听的是http server,就可以curl发送post http请求，如上述的充值和消费接口

充值协议:

```
{"method":"userDeposit","uid":"1","version":"","contractName":"Meshchain","params":["1000"]}
```

消费协议:

```
{"method":"consume","uid":"1","version":"","contractName":"Meshchain","params":["1",200]}
```

response响应:

```
{
	"code":0,
	"data":“”，
	“message”:""
}
```

code的说明:

```
0:成功
10000:热点账户已存在
10001:用户已存在
10002:用户状态不正常
10003:用户不存在
10004:热点账户不存在
10005:热点账户状态不正常
10006:用户余额不足
10007:冻结余额不合法
10008:热点账户余额为0
10009:没有可释放的金额
10010:非热点账户
10011:非影子户
10012:trie proof验证失败
10013:影子户不存在
10014:影子户状态不正常
10015:影子户已存在
10016:公钥列表不存在
10017:验证签名失败
10018:金额非法
```
# 验证和关键日志

### 商户的资产查询是否变化
若用户消费了接口，则可以通过命令来查询

```
java -cp conf/:apps/*:lib/* cn.webank.proxy.tool.DeployContract queryMerchantAssets  chainName '{"contract":"Meshchain","func":"getMerchantAssets","version":"","params":["merchantId"]}'
```

1. chainName哪一条链
2. merchantId商户ID

### 查询跨链的转账是否成功
1. 首先得确保relay task已经开启
2. 其次子链上面的商户需要有资产，否则会有特别的返回码，返回码请参考Meshchain合约的错误码
3. proxy.log,参考log4j2.xml的配置，查询关键字grep 'RelayTask start',代表子链往热点链开始转账
4. 查询总资产是否平衡，即满足转账前链A的资产,链B的资产...等于转账后的子链A',B'...和热点链H的总和。A + B + ... = A' + B' +... + H (可以通过工具查询queryMerchantAssets)



