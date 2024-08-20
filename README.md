中文 / [English](docs/README_EN.md)

![](./docs/FISCO_BCOS_Logo.svg)

[![codecov](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS/branch/master/graph/badge.svg)](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS)
[![CodeFactor](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS/badge)](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS)
[![GitHub All Releases](https://img.shields.io/github/downloads/FISCO-BCOS/FISCO-BCOS/total.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS)

FISCO BCOS（读作/ˈfɪskl bi:ˈkɒz/） 是一个稳定、高效、安全的区块链底层平台，由微众银行牵头的金链盟主导研发，其可用性经广泛应用实践检验。至今已涌现300+产业数字化标杆应用，覆盖金融、医疗、教育、文化、社会治理等领域，如珠三角征信链、区块链服务网络BSN、人民链、国家健康医疗大数据科创平台、粤澳健康码跨境互认系统等。

单链配置下，性能TPS可达10万+。全面支持国密算法、国产操作系统与国产CPU架构。包含区块流水线、可拔插共识机制、全方位并行计算、区块链文件系统、权限治理框架、分布式存储等特性。

## 版本信息
- 稳定版本（生产环境使用）：v3.7.3，版本内容可参考[《FISCO-BCOS v3.7.3版本说明》](https://github.com/FISCO-BCOS/FISCO-BCOS/releases/tag/v3.7.3)
- 最新版本（用户体验新特性）：v3.10.0，版本内容可参考 [《FISCO-BCOS v3.10.0版本说明》](https://github.com/FISCO-BCOS/FISCO-BCOS/releases/tag/v3.10.0)

## 系统概述
FISCO BCOS系统架构包括基础层、核心层、服务层、用户层和接入层提供稳定、安全的区块链底层服务。中间件层通过可视化界面，简化了用户管理区块链系统的流程。右侧配套相关开发、运维、安全控制的组件，辅助应用落地过程中不同角色的需要；同时，提供隐私保护和跨链相关的技术组件，满足不同场景的应用诉求。

![](https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/document/latest/zh_CN/_images/Technical-Architecture.png)

### 关键特性

- Pipelined：区块流水线，连续且紧凑地生成区块
- 可插拔的共识机制： 设计可插拔共识框架，灵活可选
- 全方位并行计算：多群组，块内分片，DMC，DAG等并行机制，实现强大处理性能。
- 区块链文件系统: 所见即所得的合约数据管理
- 权限治理框架：内置权限治理框架，多方投票治理区块链
- 分布式存储 TiKV：分布式事务性提交，支撑海量存储
- SDK基础库：多语言SDK，更方便的全平台国密接入

### 组件服务

- 隐私保护：场景式即使可用隐私保护解决方案WeDPR
- 跨链协作：支持多链互通的跨链协作平台WeCross
- 区块链管理：可视化的区块链管理平台WeBASE

### 开发运维工具
- 搭链工具：一键建链脚本，自动化部署区块链
- 可视化工具：提供可视化管理工具，减少操作流程
- 监控告警工具：监控区块链系统运行状态，实时告警
- 数据归档工具：冷数据归档，支持RocksDB和TiKV，释放存储压力


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

以数助实，链通产业协作，FISCO BCOS已落地400+产业数字化标杆应用，场景覆盖文化版权、司法服务、政务服务、物联网、金融、智慧社区、房产建筑、社区治理、乡村振兴等领域，如：

- 金融业：机构间对账、供应链金融、旅游金融等。
- 司法存证：仲裁链、电子借据、司法存证平台等。
- 文化版权：版权存证与交易、虎彩印刷链等。
- 社会管理：不动产登记、社区治理等。
- 乡村振兴：智慧农业养殖大数据云平台、数字化平台建设等。
- 智慧政务：城市大脑、公积金区块链平台、证书电子化项目等

FISCO
BCOS已在领域创建了诸多标杆应用示范，涵盖19类场景的252个典型应用案例，产业应用具体信息可参考[《2023 FISCO BCOS 产业应用发展报告》](https://mp.weixin.qq.com/s/hyEdSluUSG-iUZDR2PO_Ew)。

## 加入我们的社区

**FISCO BCOS开源社区**
FISCO BCOS是由深圳市金融区块链发展促进会（以下简称“金链盟”）开源工作组牵头研发的金融级、国产安全可控的区块链底层平台。作为最早开源的国产联盟链底层平台之一，FISCO
BCOS于2017年面向全球开源。

开源六周年至今，FISCO BCOS开源社区在技术创新、应用产业以及开源生态均取得了非凡成绩。

FISCO
BCOS持续攻关核心关键技术，单链性能突破10万TPS。首创DMC算法大幅度提升性能、推出三种架构形态灵活适配业务需求；全链路国产化，采用国密算法与软硬件体系，支持国产OS，适配国产芯片和服务器，支持多语言多终端国密接入。拥有覆盖底层+中间件+应用组件的丰富周边组件。

底层平台可用性已经广泛应用实践检验，支撑政务、金融、医疗、双碳、跨境数据通等关乎国计民生的重点领域落地超过400个标杆应用，在助力实体经济发展、促进公平与可持续等方面贡献力量。

社区以开源链接多方，截止2023年12月，围绕FISCO
BCOS构建的国产开源联盟链生态圈已汇聚了超过5000家机构、超10万名个人成员，以及50家认证合作伙伴、500余名核心贡献者。社区认证了63位FISCO
BCOS MVP， 发展了12个专项兴趣小组SIG，此外与上百所知名院校开展人才共育合作，培育区块链产业人才超8万人次，已发展成为最大最活跃的国产开源联盟链生态圈之一。

如您对FISCO BCOS开源技术及应用感兴趣，欢迎加入社区获得更多支持与帮助。

- [2023年度MVP](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/community/MVP_list_new.html)
- [2023年度贡献者](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/community/contributor_list_new.html)
- [FISCO BCOS 认证合作伙伴名单](https://mp.weixin.qq.com/s/A-gH2SJNQPDLgnhSGyuYDg)
- [2023 FISCO BCOS产业应用发展报告](https://mp.weixin.qq.com/s/hyEdSluUSG-iUZDR2PO_Ew)
- [社区历史文章资源](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/articles/index.html)

![](https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/images/QR_image.png)

## 贡献代码

- 我们欢迎并非常感谢您的贡献，请参阅[代码贡献流程](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/community/pr.html#)。

- 如项目对您有帮助，欢迎star支持！

## License

FISCO BCOS的开源协议为Apache License 2.0, 详情参见[LICENSE](LICENSE)。
