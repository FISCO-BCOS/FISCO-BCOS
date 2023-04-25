中文 / [English](docs/README_EN1.md)

![](./docs/FISCO_BCOS_Logo.svg)

[![codecov](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS/branch/master/graph/badge.svg)](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS)
[![CodeFactor](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS/badge)](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS)
[![GitHub All Releases](https://img.shields.io/github/downloads/FISCO-BCOS/FISCO-BCOS/total.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS)

FISCO BCOS（读作/ˈfɪskl  bi:ˈkɒz/）是由微众牵头的金链盟主导研发、对外开源、安全可控的企业级金融区块链底层技术平台。
单链配置下，性能TPS可达10万+。提供群组架构、并行计算、分布式存储、可插拔的共识机制、隐私保护算法、支持全链路国密算法诸多特性。
FISCO BCOS是一个稳定、高效、安全的区块链底层平台，其可用性经广泛应用实践检验。至今已超过4000家企业及机构、300+产业数字化标杆应用，覆盖文化版权、司法服务、政务服务、物联网、金融、智慧社区、房产建筑、社区治理、乡村振兴等领域。

![](./docs/Technical-Architecture.png)

## 技术文档
[《FISCO BCOS官方技术文档》](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/index.html)提供建链开发指引、工具介绍以及设计原理解析，用户可通过阅读官方技术文档快速了解、使用FISCO BCOS。
1. [快速开始](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/quick_start/hardware_requirements.html)
2. [合约开发](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/contract_develop/solidity_develop.html)
3. [SDK教程](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/sdk/index.html)
4. [搭链教程](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/tutorial/air/index.html)
5. [应用开发](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/develop/index.html)
6. [区块链运维工具](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/operation_and_maintenance/build_chain.html)
7. [高阶功能使用](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/advanced_function/safety.html)
8. [设计原理](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/design/architecture.html)


## 系统概述

### 架构

Air 、Pro、Max：可部署为三种架构形态

- **轻便Air版**：该架构拥有与 v2.0版本相同的形态，所有功能在一个区块链节点中（all-in-one）。该架构简单，可快速部署在任意环境中。你可以用它进行区块链入门、开发、测试、POC验证等工作。
- **专业Pro版**：该架构通过将区块链节点的接入层模块独立为进程，在实现接入层与核心模块分区部署的同时，让区块链核心功模块以多群组方式扩展。该架构实现了分区隔离，可应对将来可能的业务拓展，适合有持续业务扩展的生产环境。
- **大容量Max版**：该架构在Pro版的基础上提供链的核心模块主备切换的能力，并可通过多机部署交易执行器和接入分布式存储TiKV，实现计算与存储的平行拓展。该架构中的一个节点由一系列微服务组成，但它依赖较高的运维能力，适合需要海量计算和存储的场景。

### 功能

- 区块链文件系统：所见即所得的合约数据管理
- SDK基础库：更方便的全平台国密接入
- 交易并行冲突分析工具：自动生成交易冲突变量
- WBC-Liquid：用Rust写合约
- 权限治理框架：多方投票治理区块链

### 特性

- Pipeline：区块流水线，连续且紧凑地生成区块
- 块内分片：DMC+DAG的方式实现应用间交易执行并行化
- PBFT共识算法：立即一致的共识算法，实现交易秒级确认
- +TiKV：分布式事务性提交，支撑海量存储

### 继承与升级

- Solidity：支持至0.8.11版本
- CRUD：采用表结构存储数据，本版本中封装了更易用的接口，对业务开发更友好
- AMOP：链上信使协议，借助区块链的P2P网络实现信息传输，实现接入区块链的应用间数据通信
- 落盘加密：区块链节点的私钥和数据加密存储于物理硬盘中，物理硬件丢失也无法解密
- 密码算法：内置群环签名等密码算法，可支持各种安全多方计算场景
- 区块链监控：实现区块链状态的实时监控与数据上报

## 落地应用案例

FISCO BCOS已落地300+产业数字化标杆应用，场景覆盖文化版权、司法服务、政务服务、物联网、金融、智慧社区、房产建筑、社区治理、乡村振兴等领域，如：

- 金融业：机构间对账、供应链金融、旅游金融等。
- 司法存证：仲裁链、电子借据、司法存证平台等。
- 文化版权：版权存证与交易、虎彩印刷链等。
- 社会管理：不动产登记、社区治理等。
- 乡村振兴：智慧农业养殖大数据云平台、数字化平台建设等。
- 智慧政务：城市大脑、公积金区块链平台、证书电子化项目等
  
FISCO BCOS已在领域创建了诸多标杆应用示范，涵盖16类场景的169个典型应用案例，产业应用具体信息可参考[《2022 FISCO BCOS产业白皮书》](https://mp.weixin.qq.com/s/hERIQbnkd_-uAMVRx7Q6WQ)。

## 贡献代码

- 我们欢迎并非常感谢您的贡献，请参阅[代码贡献流程](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/community/pr.html#)。

- 如项目对您有帮助，欢迎star支持！

## 加入我们的社区

**FISCO BCOS开源社区**是国内庞大且活跃的开源社区，开源以来，围绕FISCO BCOS所构建的开源社区已汇集超4000家企业及机构、9万余名个人成员共建共治共享，成功支持政务、金融、农业、公益、文娱、供应链、物联网等重点应用领域的数百个区块链应用场景落地，收集到的标杆应用超过300个，构建出庞大且活跃的开源联盟链生态圈。

如您对FISCO BCOS开源技术及应用感兴趣，欢迎加入社区获得更多支持与帮助。
- [2022年度MVP](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/community/MVP_list_new.html)
- [2022年度贡献者](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/community/contributor_list_new.html)
- [2022新增合作伙伴](https://mp.weixin.qq.com/s/ES8ZpkfywKELv0DZZrG6eQ)
- [2022 FISCO BCOS产业应用白皮书](https://mp.weixin.qq.com/s/hERIQbnkd_-uAMVRx7Q6WQ)
- [社区历史文章资源](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/articles/index.html)


![](https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/images/QR_image.png)

## License

FISCO BCOS的开源协议为Apache License 2.0, 详情参见[LICENSE](LICENSE)。
