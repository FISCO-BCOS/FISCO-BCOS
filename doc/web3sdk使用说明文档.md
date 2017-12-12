# web3sdk使用指南

## 介绍
	web3sdk是用来访问bcos节点的java API。
	主要由两部分组成：
	1、AMOP（链上链下）系统旨在为联盟链提供一个安全高效的消息信道。
	2、web3j(Web3 Java Ethereum Ðapp API),这个部分来源于https://github.com/web3j/web3j，并针对bcos的做了相应改动。主要改动点为：1、交易结构。2、为web3增加了AMOP消息信道。
	本文档主要说明如何使用AMOP消息信道调用web3j的API。AMOP（链上链下）的使用可以参考AMOP使用指南。web3j的使用可以参考https://github.com/web3j/web3j和存证sample。

## 运行环境
	1、需要首先搭建BCOS区块链环境（参考[BCOS使用wiki](https://github.com/bcosorg/bcos/wiki)）
	2、JDK1.8。
	
## 配置
以下为SDK的配置案例
SDK配置（Spring Bean）：
``` 

	<?xml version="1.0" encoding="UTF-8" ?>
	<beans xmlns="http://www.springframework.org/schema/beans"
		xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:p="http://www.springframework.org/schema/p"
		xmlns:tx="http://www.springframework.org/schema/tx" xmlns:aop="http://www.springframework.org/schema/aop"
		xmlns:context="http://www.springframework.org/schema/context"
		xsi:schemaLocation="http://www.springframework.org/schema/beans   
	    http://www.springframework.org/schema/beans/spring-beans-2.5.xsd  
	         http://www.springframework.org/schema/tx   
	    http://www.springframework.org/schema/tx/spring-tx-2.5.xsd  
	         http://www.springframework.org/schema/aop   
	    http://www.springframework.org/schema/aop/spring-aop-2.5.xsd">
	    
	<!-- AMOP消息处理线程池配置，根据实际需要配置 -->
	<bean id="pool" class="org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor">
		<property name="corePoolSize" value="50" />
		<property name="maxPoolSize" value="100" />
		<property name="queueCapacity" value="500" />
		<property name="keepAliveSeconds" value="60" />
		<property name="rejectedExecutionHandler">
			<bean class="java.util.concurrent.ThreadPoolExecutor.AbortPolicy" />
		</property>
	</bean>
	
	<!-- 区块链节点信息配置 -->
	<bean id="channelService" class="cn.webank.channel.client.Service">
		<property name="orgID" value="WB" /> <!-- 配置本机构名称 -->
			<property name="allChannelConnections">
				<map>
					<entry key="WB"> <!-- 配置本机构的区块链节点列表（如有DMZ，则为区块链前置）-->
						<bean class="cn.webank.channel.handler.ChannelConnections">
							<property name="connectionsStr">
								<list>
									<value>NodeA@127.0.0.1:30333</value><!-- 格式：节点名@IP地址:端口，节点名可以为任意名称 -->
								</list>
							</property>
						</bean>
					</entry>
				</map>
			</property>
		</bean>
	</bean>
```

## SDK使用

1、代码案例：

```

	package cn.webank.channel.test;
	import cn.webank.web3j.abi.datatypes.generated.Uint256;
	import cn.webank.web3j.crypto.Credentials;
	import cn.webank.web3j.crypto.ECKeyPair;
	import cn.webank.web3j.crypto.Keys;
	import cn.webank.web3j.protocol.core.methods.response.EthBlockNumber;
	import cn.webank.web3j.protocol.core.methods.response.TransactionReceipt;
	import org.slf4j.Logger;
	import org.slf4j.LoggerFactory;
	import org.springframework.context.ApplicationContext;
	import org.springframework.context.support.ClassPathXmlApplicationContext;
	import cn.webank.channel.client.Service;
	import cn.webank.web3j.protocol.Web3j;
	import cn.webank.web3j.protocol.channel.ChannelEthereumService;

	import java.math.BigInteger;

	public class Ethereum {
		static Logger logger = LoggerFactory.getLogger(Ethereum.class);
		
		public static void main(String[] args) throws Exception {
			
			//初始化Service
			ApplicationContext context = new ClassPathXmlApplicationContext("classpath:applicationContext.xml");
			Service service = context.getBean(Service.class);
			service.run();

			Thread.sleep(3000);
			System.out.println("开始测试...");
			System.out.println("===================================================================");
			
			logger.info("初始化AOMP的ChannelEthereumService");
			ChannelEthereumService channelEthereumService = new ChannelEthereumService();
			channelEthereumService.setChannelService(service);
			
			//使用AMOP消息信道初始化web3j
			Web3j web3 = Web3j.build(channelEthereumService);

			logger.info("调用web3的getBlockNumber接口");
			EthBlockNumber ethBlockNumber = web3.ethBlockNumber().sendAsync().get();
			logger.info("获取ethBlockNumber:{}", ethBlockNumber);

			//初始化交易签名私钥
			ECKeyPair keyPair = Keys.createEcKeyPair();
			Credentials credentials = Credentials.create(keyPair);

			//初始化交易参数
			java.math.BigInteger gasPrice = new BigInteger("30000000");
			java.math.BigInteger gasLimit = new BigInteger("30000000");
			java.math.BigInteger initialWeiValue = new BigInteger("0");

			//部署合约
			Ok ok = Ok.deploy(web3,credentials,gasPrice,gasLimit,initialWeiValue).get();
			System.out.println("Ok getContractAddress " + ok.getContractAddress());
			
			//调用合约接口
			java.math.BigInteger Num = new BigInteger("999");
			Uint256 num = new Uint256(Num);
			TransactionReceipt receipt = ok.trans(num).get();
			System.out.println("receipt transactionHash" + receipt.getTransactionHash());

			//查询合约数据
			num = ok.get().get();
			System.out.println("ok.get() " + num.getValue());
		}
	}

```

## 合约编译及java Wrap代码生成
* 智能合约语法及细节参考 <a href="https://solidity.readthedocs.io/en/develop/solidity-in-depth.html">solidity官方文档</a>。
* 安装FISCO-BCOS的solidity编译器
    * 直接使用FISCO-BCOS的合约编译器，将根目录下的fisco-solc放到系统/usr/bin目录下
* SDK执行gradle run 之后生成工具包，将自己的合约复制进contracts文件夹中（建议删除文件夹中其他无关的合约）
* 工具包中bin文件夹下为合约编译的执行脚本，contracts为合约存放文件夹，apps为sdk jar包，lib为sdk依赖jar包，output（不需要新建，脚本会生成）为编译后输出的abi、bin及java文件目录。
* 在bin文件夹下compile.sh为编译合约的脚本，执行命令sh compile.sh [参数1：java包名]执行成功后将在output目录生成所有合约对应的abi,bin,java文件，其文件名类似：合约名字.[abi|bin|java]。compile.sh脚本执行步骤实际分为两步，1.首先将sol源文件编译成abi和bin文件，依赖fisco-solc工具；2.将bin和abi文件编译java Wrap代码，依赖web3sdk



