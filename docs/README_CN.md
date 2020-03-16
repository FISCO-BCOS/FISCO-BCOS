[English](../README.md) / 中文

![](./images/FISCO_BCOS_Logo.svg)


[![codecov](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS/branch/master/graph/badge.svg)](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS) [![CodeFactor](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS/badge)](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS) [![Codacy Badge](https://api.codacy.com/project/badge/Grade/08552871ee104fe299b00bc79f8a12b9)](https://www.codacy.com/app/fisco-dev/FISCO-BCOS?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=FISCO-BCOS/FISCO-BCOS&amp;utm_campaign=Badge_Grade) [![GitHub All Releases](https://img.shields.io/github/downloads/FISCO-BCOS/FISCO-BCOS/total.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS) [![Code Lines](https://tokei.rs/b1/github/FISCO-BCOS/FISCO-BCOS?category=code)](https://github.com/FISCO-BCOS/FISCO-BCOS)

[![CircleCI](https://circleci.com/gh/FISCO-BCOS/FISCO-BCOS.svg?style=shield)](https://circleci.com/gh/FISCO-BCOS/FISCO-BCOS)  [![Build Status](https://travis-ci.org/FISCO-BCOS/FISCO-BCOS.svg)](https://travis-ci.org/FISCO-BCOS/FISCO-BCOS)

**FISCO BCOS**是由中国企业主导研发、对外开源、安全可控的企业级金融区块链底层技术平台。单链配置下，性能TPS可达万级。提供群组架构、跨链通信协议、可插拔的共识机制、隐私保护算法、支持国密算法、分布式存储等诸多特性。经过多个机构、多个应用，长时间在生产环境中的实践检验，具备金融级的高性能、高可用性及高安全性。

- **群组架构**

群组架构设计支持区块链节点同时启动多个群组，实现快速组链，满足海量服务请求。群组架构可以通过群组间交易处理、分布式数据存储、区块共识相互隔离等技术有效地实现平行扩展。在扩大业务规模和保障区块链系统隐私性的同时，极大简化了系统的运维复杂度。

- **分布式存储**

分布式数据存储可突破单机限制，带来容量和性能的极大提升。支持计算和存储分离，模块化的数据交换接口，提供更好的扩展性。为了简化数据管理和业务开发流程，定义了标准数据访问CRUD接口，适配多种数据存储系统（例如MySQL），同时支持业界通用Key-Value与SQL数据表结构。

- **并行计算模型**

并行交易处理模型可有效利用多核处理能力，实现交易并行计算。针对业务执行中的高频计算和复杂计算，提供了预编译合约框架，支持采用C++编写合约，极大提升交易执行效率。

- **简单易用**

提供多种编译部署方式，支持一键部署，支持Docker等所有主流平台的快速安装部署。提供了完整的工具套件来实现快速安装部署，系统监控、企业级数据治理等，解放开发人员的非业务开发时间。

<div style="text-align:center"><img src="https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/images/plane.jpg"/> </div>

更多信息，请[参阅2.0版本新特性](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/what_is_new.html#id11)。

## 快速开始

阅读[快速开始](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/installation.html)，了解使用FISCO BCOS所需的必要安装和配置，帮助您快速体验FISCO BCOS强大功能。

## 文档

阅读[**技术文档**](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/)，详细了解如何使用FISCO BCOS。

- [2.0版本新特性](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/what_is_new.html)
- [版本及兼容](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/change_log/index.html)
- [安装](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/installation.html)
- [教程](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/tutorial/index.html#)
- [使用手册](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/manual/index.html)
- [企业级部署工具](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/enterprise_tools/index.html)
- [系统设计](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/design/index.html)
- [Web3SDK](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/sdk/sdk.html)
- [JSON-RPC API](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/api.html)
- [常见问答](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/faq.html)



## 落地应用案例

FISCO BCOS已落地应用达数十个，场景覆盖政务、金融、公益、医疗、教育、交通、版权、商品溯源、供应链、招聘、农业、社交、游戏等多个领域，如：

- 金融业：机构间对账、供应链金融、旅游金融等。
- 司法服务：仲裁链、电子借据等。
- 文化版权：版权存证与交易等。
- 社会管理：不动产登记等。

此处提供一些具有代表性的[落地应用案例](https://mp.weixin.qq.com/s/cUjuWf1eGMbG3AFq60CBUA)。


## 贡献代码

- 我们欢迎并非常感谢您的贡献，请参阅[代码贡献流程](https://mp.weixin.qq.com/s/_w_auH8X4SQQWO3lhfNrbQ)和[代码规范](../CODING_STYLE.md)。
- 如项目对您有帮助，欢迎star支持！

## 加入我们的社区

**FISCO BCOS开源社区**是国内活跃的开源社区，社区长期为机构和个人开发者提供各类支持与帮助。已有来自各行业的数千名技术爱好者在研究和使用FISCO BCOS。如您对FISCO BCOS开源技术及应用感兴趣，欢迎加入社区获得更多支持与帮助。

![](https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/images/QR_image.png)

## License

[![](https://img.shields.io/github/license/FISCO-BCOS/FISCO-BCOS.svg)](../LICENSE)

FISCO BCOS的开源协议为[GPL 3.0](https://www.gnu.org/licenses/gpl-3.0.en.html)。详情参见[LICENSE](../LICENSE)。  
