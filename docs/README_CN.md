[English](../README.md) / 中文

![](./images/FISCO_BCOS_Logo.svg)


[![codecov](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS/branch/master/graph/badge.svg)](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS) [![CodeFactor](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS/badge)](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS) [![Codacy Badge](https://api.codacy.com/project/badge/Grade/08552871ee104fe299b00bc79f8a12b9)](https://www.codacy.com/app/fisco-dev/FISCO-BCOS?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=FISCO-BCOS/FISCO-BCOS&amp;utm_campaign=Badge_Grade) [![GitHub All Releases](https://img.shields.io/github/downloads/FISCO-BCOS/FISCO-BCOS/total.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS) [![Code Lines](https://tokei.rs/b1/github/FISCO-BCOS/FISCO-BCOS?category=code)](https://github.com/FISCO-BCOS/FISCO-BCOS)

[![CircleCI](https://circleci.com/gh/FISCO-BCOS/FISCO-BCOS.svg?style=shield)](https://circleci.com/gh/FISCO-BCOS/FISCO-BCOS)  [![Build Status](https://travis-ci.org/FISCO-BCOS/FISCO-BCOS.svg)](https://travis-ci.org/FISCO-BCOS/FISCO-BCOS)

**FISCO BCOS**是由国内企业主导研发、对外开源、安全可控的企业级金融区块链底层技术平台，单链TPS可达万级，支持群组架构、跨链通信协议、可插拔的共识机制及隐私保护算法、支持国密算法、分布式存储等特性，经过多个机构、多个应用，长时间在生产环境的实际检验，具有高性能、高可用性及高安全性的特点。

- **多群组架构**

多群组架构，支持区块链节点启动多个群组，实现快速组链，满足海量服务请求。群组架构可快速地平行扩展，群组间交易处理、数据存储、区块共识相互隔离，在扩大业务规模、保障区块链系统隐私性的同时，极大简化了系统的运维复杂度。

- **分布式存储**

分布式数据存储可突破单机限制，支持横向扩展，带来容量和性能的极大提升；计算和存储分离，提高了系统健壮性；定义了标准数据访问CRUD接口，适配多种数据存储系统，同时支持Key-Value与SQL，数据更易管理，业务开发更为简便。

- **并行计算模型**

并行交易处理模型可有效利用多核处理能力，实现交易并行计算；提供预编译合约框架，支持采用C++编写合约，极大提升交易执行效率，适用于合约逻辑简单但调用频繁，或者合约逻辑固定而计算量大的场景。

- **简单易用**

提供多种编译部署方式，支持一键部署，支持Docker、多平台的快速安装部署等，并提供了丰富的便捷工具支撑快速安装部署，包括控制台、企业部署工具等，使开发人员可专注于业务开发。

<div style="text-align:center"><img src="https://media.githubusercontent.com/media/FISCO-BCOS/LargeFiles/master/images/plane.jpg"/> </div>

更多信息，请[参阅2.0版本新特性](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/what_is_new.html#id11)。

## 快速开始

阅读[快速开始](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/installation.html)，了解使用FISCO BCOS所需的必要安装和配置，帮助您快速掌握部署流程。

## 文档

阅读[**技术文档**](https://fisco-bcos-documentation.readthedocs.io/zh_CN/release-2.0/)，详细了解如何使用FISCO BCOS。

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


## 历史版本

自2017年12月正式开源后，已发布[18个版本](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/change_log/index.html)。

## 落地应用案例

FISCO BCOS已落地应用达数十个，场景覆盖政务、金融、公益、医疗、教育、交通、版权、商品溯源、供应链、招聘、农业、社交、游戏等多个领域，如：

- 金融业：机构间对账、供应链金融、旅游金融等。
- 司法服务：仲裁链、电子借据等。
- 文化版权：版权存证与交易等。
- 社会管理：不动产登记等。

此处提供一些具有代表性的[落地应用案例](https://mp.weixin.qq.com/s/vUSq80LkhF8yCfUF7AILgQ)。


## 贡献代码

- 我们欢迎并非常感谢您的贡献，请参阅[代码贡献流程](https://mp.weixin.qq.com/s/hEn2rxqnqp0dF6OKH6Ua-A
)和[编码规范](../CODING_STYLE.md)。
- 如项目对您有帮助，欢迎star支持！
- 如果发现代码存在安全漏洞，可在[这里](https://security.webank.com)上报。

## 加入社区

**FISCO BCOS开源社区**是国内活跃的开源社区，社区长期为开发者提供各类支持与帮助，已有来自各行业、各机构的数千名技术爱好者在研究和使用FISCO BCOS；如您对FISCO BCOS开源技术及应用感兴趣，欢迎加入社区获得更多支持与帮助。

![](https://media.githubusercontent.com/media/FISCO-BCOS/LargeFiles/master/images/QR_image.png)

## License

[![](https://img.shields.io/github/license/FISCO-BCOS/FISCO-BCOS.svg)](../LICENSE)

FISCO BCOS的开源协议为[GPL 3.0](https://www.gnu.org/licenses/gpl-3.0.en.html)。详情参见[LICENSE](../LICENSE)。  
