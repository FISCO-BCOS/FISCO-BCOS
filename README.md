中文 / [English](docs/README_EN.md)

![](./docs/FISCO_BCOS_Logo.svg)

[![codecov](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS/branch/master/graph/badge.svg)](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS)
[![CodeFactor](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS/badge)](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS)
[![GitHub All Releases](https://img.shields.io/github/downloads/FISCO-BCOS/FISCO-BCOS/total.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS)

FISCO BCOS（读作/ˈfɪskl bi:ˈkɒz/） 是一个稳定、高效、安全的区块链底层平台，由微众银行牵头的金链盟主导研发，其可用性经广泛应用实践检验。至今已涌现300+产业数字化标杆应用，覆盖金融、医疗、教育、文化、社会治理等领域，如珠三角征信链、区块链服务网络BSN、人民链、国家健康医疗大数据科创平台、粤澳健康码跨境互认系统等。

单链配置下，性能TPS可达10万+。全面支持国密算法、国产操作系统与国产CPU架构。包含区块流水线、可拔插共识机制、全方位并行计算、区块链文件系统、权限治理框架、分布式存储等特性。

## 版本信息
- 稳定版本（生产环境使用）：v3.2.3，版本内容可参考[《FISCO-BCOS v3.2.3版本说明》](https://github.com/FISCO-BCOS/FISCO-BCOS/releases/tag/v3.2.3)
- 最新版本（用户体验新特性）：v3.7.1，版本内容可参考 [《FISCO-BCOS v3.7.1版本说明》](https://github.com/FISCO-BCOS/FISCO-BCOS/releases/tag/v3.7.1)

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

以数助实，链通产业协作，FISCO BCOS已落地300+产业数字化标杆应用，场景覆盖文化版权、司法服务、政务服务、物联网、金融、智慧社区、房产建筑、社区治理、乡村振兴等领域，如：

- 金融业：机构间对账、供应链金融、旅游金融等。
- 司法存证：仲裁链、电子借据、司法存证平台等。
- 文化版权：版权存证与交易、虎彩印刷链等。
- 社会管理：不动产登记、社区治理等。
- 乡村振兴：智慧农业养殖大数据云平台、数字化平台建设等。
- 智慧政务：城市大脑、公积金区块链平台、证书电子化项目等

FISCO BCOS已在领域创建了诸多标杆应用示范，涵盖16类场景的169个典型应用案例，产业应用具体信息可参考[《2022 FISCO BCOS产业白皮书》](https://mp.weixin.qq.com/s/hERIQbnkd_-uAMVRx7Q6WQ)。


## 加入我们的社区

**FISCO BCOS开源社区**是国内庞大且活跃的开源社区，开源以来，围绕FISCO BCOS所构建的开源社区已汇集超4000家企业及机构、9万余名个人成员共建共治共享，成功支持政务、金融、农业、公益、文娱、供应链、物联网等重点应用领域的数百个区块链应用场景落地，收集到的标杆应用超过300个，构建出庞大且活跃的开源联盟链生态圈。

如您对FISCO BCOS开源技术及应用感兴趣，欢迎加入社区获得更多支持与帮助。
- [2022年度MVP](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/community/MVP_list_new.html)
- [2022年度贡献者](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/community/contributor_list_new.html)
- [2022新增合作伙伴](https://mp.weixin.qq.com/s/ES8ZpkfywKELv0DZZrG6eQ)
- [2022 FISCO BCOS产业应用白皮书](https://mp.weixin.qq.com/s/hERIQbnkd_-uAMVRx7Q6WQ)
- [社区历史文章资源](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/articles/index.html)


![](https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/images/QR_image.png)

## 贡献代码

- 我们欢迎并非常感谢您的贡献，请参阅[代码贡献流程](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/community/pr.html#)。

- 如项目对您有帮助，欢迎star支持！

## License

FISCO BCOS的开源协议为Apache License 2.0, 详情参见[LICENSE](LICENSE)。