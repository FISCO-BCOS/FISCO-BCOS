中文 / [English](docs/README_EN.md)

![](./docs/FISCO_BCOS_Logo.svg)

[![codecov](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS/branch/master/graph/badge.svg)](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS)
[![CodeFactor](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS/badge)](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS)
[![GitHub All Releases](https://img.shields.io/github/downloads/FISCO-BCOS/FISCO-BCOS/total.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS)

FISCO BCOS（读作/ˈfɪskl bi:ˈkɒz/）是金融区块链合作联盟（深圳）（简称"金链盟"）开源工作组于2017年推出的国产安全可控区块链底层开源平台，致力于为数字经济时代提供可信基础设施，释放数据要素价值，助力产业数字化及Web3.0经济发展。至今已在金融、政务、公益等领域落地标杆应用超600个，如区块链服务网络BSN、珠三角征信链、粤澳/深港/深圳-新加坡跨境数据验证平台、中国银联区块链可信存证服务等。

核心特性：

- **金融级性能**：单链性能突破20万TPS，支持横向扩展，满足大规模应用需求。支持监管节点接入，实现即时穿透式监管。在密码算法、共识协议、P2P网络、密钥管理及隐私保护方面建立全方位安全保障。
- **国产安全可控**：由国内金融机构和科技企业主导研发，全域自研并取得一系列关键技术专利。在计算、网络、存储各环节采用国密算法，全面适配国产服务器、操作系统、数据库等软硬件平台。
- **开放开源**：代码及核心技术全面开源，兼容国内外主流软硬件平台。定义共识节点、观察节点、轻节点三种节点类型，支持公众接入。

## 版本信息
- 稳定版本（生产环境使用）：v3.7.3，版本内容可参考[《FISCO-BCOS v3.7.3版本说明》](https://github.com/FISCO-BCOS/FISCO-BCOS/releases/tag/v3.7.3)
- 最新版本（用户体验新特性）：v3.17.0，版本内容可参考 [《FISCO-BCOS v3.17.0版本说明》](https://github.com/FISCO-BCOS/FISCO-BCOS/releases/tag/v3.17.0)

## 系统概述
FISCO BCOS系统架构包括基础层、核心层、服务层、用户层和接入层提供稳定、安全的区块链底层服务。中间件层通过可视化界面，简化了用户管理区块链系统的流程。右侧配套相关开发、运维、安全控制的组件，辅助应用落地过程中不同角色的需要；同时，提供隐私保护和跨链相关的技术组件，满足不同场景的应用诉求。

![](https://fisco-bcos-doc.readthedocs.io/zh-cn/latest/_images/fisco_bcos_system_architecture.png)

### 关键特性

- Pipelined：区块流水线，连续且紧凑地生成区块
- 可插拔的共识机制： 设计可插拔共识框架，灵活可选
- 全方位并行计算：多群组，块内分片，DMC，DAG等并行机制，实现强大处理性能。
- 区块链文件系统: 所见即所得的合约数据管理
- 权限治理框架：内置权限治理框架，多方投票治理区块链
- SDK基础库：多语言SDK，更方便的全平台国密接入

### 组件服务

- 隐私保护：场景式即使可用隐私保护解决方案WeDPR
- 跨链协作：支持多链互通的跨链协作平台WeCross
- 区块链管理：可视化的区块链管理平台WeBASE

### 开发运维工具
- 搭链工具：一键建链脚本，自动化部署区块链
- 可视化工具：提供可视化管理工具，减少操作流程
- 监控告警工具：监控区块链系统运行状态，实时告警
- 数据归档工具：冷数据归档，支持RocksDB，释放存储压力


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


若需查阅FISCO BCOS 2.x版本相关信息，可参考 **[《FISCO BCOS 2.x 技术文档》](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/)**


## 落地应用案例

FISCO BCOS已在金融、政务、公益等领域落地标杆应用超600个，有效助力产业数字化发展和数据可信流通。典型应用包括：

- 区块链服务网络BSN
- 粤澳/深港/深圳-新加坡跨境数据验证平台
- 四川省区块链服务基础设施蜀信链
- 国家海洋科学数据开放共享与流通隐私计算平台
- 珠三角征信链
- 中国银联区块链可信存证服务

更多案例可参考[《2025飞梭链（FISCO BCOS）产业应用发展报告》](https://mp.weixin.qq.com/s/A2vdLtJhhyg9_BBkIwByNA)。

## 加入我们的社区

截至2025年底，FISCO BCOS开源社区已汇聚超5000家企业及机构、10万余名个人成员共建共治，发展为中国最具活力的区块链技术开源社区之一。

社区荣誉：

- 2018年度深圳金融科技专项奖一等奖
- 国家信息中心区块链服务网络（BSN）首个适配的国产联盟链底层平台
- 首批通过北京国家金融科技认证中心"区块链技术产品国推认证"
- 国际标准化组织ISO《区块链与分布式账本技术 用例》中，4个中国用例有2个基于FISCO BCOS研发
- 核心技术论文入选国际超算顶级学术会议SC23，为SC会议史上第一篇区块链性能优化学术论文

欢迎对FISCO BCOS开源技术及应用感兴趣的开发者加入社区，获取更多支持与帮助。

- [2025年度MVP名单](https://mp.weixin.qq.com/s/g1ZJZ8LXxgk9rexUg2a0Uw)
- [飞梭链FISCO BCOS认证合作伙伴名单](https://mp.weixin.qq.com/s/IJ87PYxuDPpQzOoXD-XeVQ)
- [2025飞梭链（FISCO BCOS）产业应用发展报告](https://mp.weixin.qq.com/s/A2vdLtJhhyg9_BBkIwByNA)
- [社区历史文章资源](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/articles/index.html)

![](https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/images/QR_image.png)

## 贡献代码

- 我们欢迎并非常感谢您的贡献，请参阅[代码贡献流程](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/community/pr.html#)。

- 如项目对您有帮助，欢迎star支持！

## License

FISCO BCOS的开源协议为Apache License 2.0, 详情参见[LICENSE](LICENSE)。
