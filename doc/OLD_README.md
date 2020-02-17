# 1.项目介绍

## 1.1 项目概况
FISCO BCOS平台是金融区块链合作联盟（深圳）（以下简称：金链盟）开源工作组以金融业务实践为参考样本，在BCOS开源平台基础上进行模块升级与功能重塑，深度定制的安全可控、适用于金融行业且完全开源的区块链底层平台。  

金链盟开源工作组获得金链盟成员机构的广泛认可，并由专注于区块链底层技术研发的成员机构及开发者牵头开展工作。其中首批成员包括以下单位（排名不分先后）：博彦科技、华为、深证通、神州数码、四方精创、腾讯、微众银行、越秀金科。   

FISCO BCOS平台基于现有的BCOS开源项目进行开发，聚焦于金融行业的分布式商业需求，从业务适当性、性能、安全、正常、技术可行性、运维与治理、成本等多个维度进行综合考虑，打造金融版本的区块链解决方案。

基于FISCO BCOS的金融区块链底层平台，可以快速构建“区块链+金融"应用场景，对金融行业大有裨益：  
- 对银行机构，可以降低清结算成本、提高中后台运营效率、提升流程自动化程度；  
- 对非银金融机构，可以提升权益登记、信息存证的权威性、削减交易对手方风险、解决数据追踪与信息防伪问题、降低审核审计的操作成本等；
- 对金融监管机构，为监管机构提供了一致且易于审计的数据，通过对机构间区块链的数据分析，能够比传统审计流程更快更精确地监管金融业务，并极大加强反洗钱力度；
- 在跨境金融场景中，有助于实现跨境金融机构间的账本共享，降低合作银行间对账与清结算成本及争议摩擦成本，从而提高跨境业务处理速度及效率。

FISCO BCOS的开源协议为GPL3.0，详情参见[LICENSE](https://github.com/FISCO-BCOS/FISCO-BCOS/blob/master-1.3/LICENSE)。  


## 1.2 技术白皮书

参见[金融区块链底层平台FISCO BCOS白皮书——金融区块链基础设施与实践样本v1.0](https://github.com/FISCO-BCOS/whitepaper)([PDF下载](https://github.com/FISCO-BCOS/whitepaper/raw/master/FISCO%20BCOS%20Whitepaper.pdf))。


# 2.安装部署

## 2.1 支持平台

目前运行的操作系统平台如下：

- Linux


## 2.2 安装说明

如果您想快速体验FISCO BCOS，我们提供了[一键快速安装部署](sample/)工具。[(FISCO BCOS One-Click Installation Manual)](sample/README_EN.md)

如果您希望使用Docker进行安装部署，可以参阅[使用Docker安装部署FISCO BCOS指南](docker/)。[(FISCO BCOS Docker Installation Manual)](docker/README_EN.md)

如果您想编译源码并亲自动手配置、搭建FISCO BCOS，请参考[FISCO BCOS使用说明书](manual/README.md)。[(FISCO-BCOS Manual)](manual/README_EN.md)

如果你想快速搭建生产上的可用环境, 请参考[FISCO-BCOS物料包工具](https://github.com/FISCO-BCOS/fisco-package-build-tool)。

## 2.3 实践指引

[FISCO BCOS实践指引](https://github.com/FISCO-BCOS/Wiki/tree/master/FISCO%20BCOS%E5%AE%9E%E8%B7%B5%E6%8C%87%E5%BC%95)，建议优先阅读。




# 3.特性介绍
* [FISCO BCOS特性介绍](https://github.com/FISCO-BCOS/Wiki/blob/master/FISCO%20BCOS%E7%89%B9%E6%80%A7%E4%BB%8B%E7%BB%8D.pdf)

**基础功能**

* [系统合约介绍](https://github.com/FISCO-BCOS/Wiki/tree/master/FISCO-BCOS%E7%B3%BB%E7%BB%9F%E5%90%88%E7%BA%A6%E4%BB%8B%E7%BB%8D)
* [系统参数说明文档](https://github.com/FISCO-BCOS/Wiki/tree/master/%E7%B3%BB%E7%BB%9F%E5%8F%82%E6%95%B0%E8%AF%B4%E6%98%8E%E6%96%87%E6%A1%A3)
* [CNS合约命名服务](https://github.com/FISCO-BCOS/Wiki/tree/master/Contract_Name%20Service%E6%9C%8D%E5%8A%A1)

**性能**

* [共识算法优化](https://github.com/FISCO-BCOS/Wiki/tree/master/%E5%BA%94%E7%94%A8%E4%BA%8E%E5%8C%BA%E5%9D%97%E9%93%BE%E7%9A%84%E5%A4%9A%E8%8A%82%E7%82%B9%E5%B9%B6%E8%A1%8C%E6%8B%9C%E5%8D%A0%E5%BA%AD%E5%AE%B9%E9%94%99%E5%85%B1%E8%AF%86%E7%AE%97%E6%B3%95)
* [并行计算介绍](https://github.com/FISCO-BCOS/Wiki/tree/master/FISCO-BCOS%E5%B9%B6%E8%A1%8C%E8%AE%A1%E7%AE%97%E4%BB%8B%E7%BB%8D)

**易用性**

* [易用性提升](https://github.com/FISCO-BCOS/Wiki/tree/master/%E6%B5%85%E8%B0%88FISCO-BCOS%E7%9A%84%E6%98%93%E7%94%A8%E6%80%A7)
* [链上信使协议AMOP](https://github.com/FISCO-BCOS/Wiki/tree/master/AMOP%E4%BD%BF%E7%94%A8%E6%8C%87%E5%8D%97)
* [弹性联盟链共识框架](弹性联盟链共识框架说明文档.md)

**安全**

* [FISCO BCOS权限模型](https://github.com/FISCO-BCOS/Wiki/tree/master/FISCO%20BCOS%E6%9D%83%E9%99%90%E6%A8%A1%E5%9E%8B)
* [群签名和环签名链上验证](启用_关闭群签名环签名ethcall.md)
* [可监管的零知识证明说明](可监管的零知识证明说明.md)
* [FISCO-BCOS开启国密特性](%E5%9B%BD%E5%AF%86%E6%93%8D%E4%BD%9C%E6%96%87%E6%A1%A3.md)

# 4.应用案例
* [FISCO BCOS应用实践](https://github.com/FISCO-BCOS/Wiki/blob/master/FISCO%20BCOS%E5%BA%94%E7%94%A8%E5%AE%9E%E8%B7%B5.pdf) 
* [存证案例说明](https://github.com/FISCO-BCOS/Wiki/tree/master/%E5%AD%98%E8%AF%81sample%E8%AF%B4%E6%98%8E) [案例源码](https://github.com/FISCO-BCOS/evidenceSample)
* [微粒贷机构间对账平台](https://github.com/FISCO-BCOS/Wiki/blob/master/%E3%80%90FISCO%20BCOS%E5%BA%94%E7%94%A8%E6%A1%88%E4%BE%8B%E3%80%91%E5%BE%AE%E7%B2%92%E8%B4%B7%E6%9C%BA%E6%9E%84%E9%97%B4%E5%AF%B9%E8%B4%A6%E5%B9%B3%E5%8F%B0/README.md) 
* [“仲裁链”：基于区块链的存证实践](https://github.com/FISCO-BCOS/Wiki/blob/master/%E3%80%90FISCO%20BCOS%E6%A1%88%E4%BE%8B%E4%BB%8B%E7%BB%8D%E3%80%91%E2%80%9C%E4%BB%B2%E8%A3%81%E9%93%BE%E2%80%9D%EF%BC%9A%E5%9F%BA%E4%BA%8E%E5%8C%BA%E5%9D%97%E9%93%BE%E7%9A%84%E5%AD%98%E8%AF%81%E5%AE%9E%E8%B7%B5/README.md) 
* [群签名&&环签名客户端示例demo](https://github.com/FISCO-BCOS/sig-service-client)
* [群签名&&环签名RPC服务](https://github.com/FISCO-BCOS/sig-service)
* [一对一匿名可监管转账](https://github.com/FISCO-BCOS/zkg-tx1to1)

# 5.其他

## 5.1 区块链知识&行业动态
详情参见[区块链知识&行业动态](https://github.com/FISCO-BCOS/Wiki/blob/master/README.md#%E5%8C%BA%E5%9D%97%E9%93%BE%E7%9F%A5%E8%AF%86%E8%A1%8C%E4%B8%9A%E5%8A%A8%E6%80%81)

## 5.2 常见问题

FISCO BCOS常见问题，可参见[常见问题](https://github.com/FISCO-BCOS/Wiki/tree/master/FISCO%20BCOS%E5%B8%B8%E8%A7%81%E9%97%AE%E9%A2%98/README.md)。

也欢迎爱好者相互讨论、一起交流，请进入[提问专区](https://github.com/FISCO-BCOS/FISCO-BCOS/issues)。

# 6.联系我们
邮箱：service@fisco.com.cn

微信群：添加群管理员微信号fiscobcosfan，拉您入FISCO BCOS官方技术交流群。

群管理员微信二维码：

![](./FISCO-BCOS.jpeg)

诚邀广大安全专家共同关注区块链安全。如果有安全风险，欢迎各位踊跃提交漏洞。





