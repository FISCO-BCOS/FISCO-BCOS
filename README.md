中文 / [English](docs/README_EN.md)

![](./docs/FISCO_BCOS_Logo.svg)

[![codecov](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS/branch/master/graph/badge.svg)](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS)
[![CodeFactor](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS/badge)](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS)
[![GitHub All Releases](https://img.shields.io/github/downloads/FISCO-BCOS/FISCO-BCOS/total.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS)

FISCO BCOS是由微众牵头的金链盟主导研发、对外开源、安全可控的企业级金融区块链底层技术平台。
单链配置下，性能TPS可达万级。提供群组架构、并行计算、分布式存储、可插拔的共识机制、隐私保护算法、支持全链路国密算法等诸多特性。
经过多个机构、多个应用，长时间在生产环境中的实践检验，具备金融级的高性能、高可用性及高安全性。

**架构**

Air 、Pro、Max：可部署为三种架构形态

- **轻便Air版**：拥有与 v2.0版本相同的形态，所有功能在一个区块链节点中（all-in-one）。该架构简单，可快速部署在任意环境中。你可以用它进行区块链入门、开发、测试、POC验证等工作。
- **专业Pro版**：该架构通过将区块链节点的接入层模块独立为进程，在实现接入层与核心模块分区部署的同时，让区块链核心功模块以多群组方式扩展。该架构实现了分区隔离，可应对将来可能的业务拓展，适合有持续业务扩展的生产环境。
- **大容量Max版**：该架构在Pro版的基础上提供链的核心模块主备切换的能力，并可通过多机部署交易执行器和接入分布式存储TiKV，实现计算与存储的平行拓展。该架构中的一个节点由一系列微服务组成，但它依赖较高的运维能力，适合需要海量计算和存储的场景。

**功能**

- 区块链文件系统：所见即所得的合约数据管理
- SDK基础库：更方便的全平台国密接入
- 交易并行冲突分析工具：自动生成交易冲突变量
- WBC-Liquid：用Rust写合约
- 权限治理框架：多方投票治理区块链

**特性**

- Pipeline：区块流水线，连续且紧凑地生成区块
- DMC：实现交易处理性能的多机拓展
- PBFT共识算法：立即一致的共识算法，实现交易秒级确认
- +TiKV：分布式事务性提交，支撑海量存储

## 技术文档

- **[FISCO BCOS 3.x技术文档](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/)**

- **[FISCO BCOS 2.x 技术文档（stable）](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/)**


## 落地应用案例

FISCO BCOS已落地应用达数百个，场景覆盖政务、金融、公益、医疗、教育、交通、版权、商品溯源、供应链、招聘、农业、社交、游戏等多个领域，如：

- 金融业：机构间对账、供应链金融、旅游金融等。
- 司法服务：仲裁链、电子借据等。
- 文化版权：版权存证与交易等。
- 社会管理：不动产登记等。

此处提供一些具有代表性的[落地应用案例](https://mp.weixin.qq.com/s/RJwRMChWt6mhJrysyBLAmA)。

## 贡献代码

- 我们欢迎并非常感谢您的贡献，请参阅[代码贡献流程](https://mp.weixin.qq.com/s/_w_auH8X4SQQWO3lhfNrbQ)。

- 如项目对您有帮助，欢迎star支持！

## 加入我们的社区

**FISCO BCOS开源社区**是国内庞大且活跃的开源社区，开源以来，围绕FISCO BCOS所构建的开源社区已汇集超3000家企业及机构、7万余名个人成员共建共治共享，成功支持政务、金融、农业、公益、文娱、供应链、物联网等重点应用领域的数百个区块链应用场景落地，收集到的标杆应用超过200个，构建出庞大且活跃的开源联盟链生态圈。

如您对FISCO BCOS开源技术及应用感兴趣，欢迎加入社区获得更多支持与帮助。
- [MVP](https://www.fisco.com.cn/news_6/494.html)
- [贡献者](https://www.fisco.com.cn/news_6/510.html)
- [合作伙伴](https://www.fisco.com.cn/news_6/492.html)


![](https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/images/QR_image.png)

## License

FISCO BCOS的开源协议为Apache License 2.0, 详情参见[LICENSE](LICENSE)。
