[English](../README.md) / 中文

![](./images/FISCO_BCOS_Logo.svg)


[![codecov](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS/branch/master/graph/badge.svg)](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS)
[![CodeFactor](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS/badge)](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS)
[![GitHub All Releases](https://img.shields.io/github/downloads/FISCO-BCOS/FISCO-BCOS/total.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS)
[![Code Lines](https://tokei.rs/b1/github/FISCO-BCOS/FISCO-BCOS?category=code)](https://github.com/FISCO-BCOS/FISCO-BCOS)
 [![version](https://img.shields.io/github/tag/FISCO-BCOS/FISCO-BCOS.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS/releases/latest)

[![Github Actions](https://github.com/FISCO-BCOS/FISCO-BCOS/workflows/FISCO-BCOS%20GitHub%20Actions/badge.svg)](https://travis-ci.org/FISCO-BCOS/FISCO-BCOS)
[![CircleCI](https://circleci.com/gh/FISCO-BCOS/FISCO-BCOS.svg?style=shield)](https://circleci.com/gh/FISCO-BCOS/FISCO-BCOS)

**FISCO BCOS**是由中国企业主导研发、对外开源、安全可控的企业级金融区块链底层技术平台。FISCO BCOS 3.0采用微服务架构，但同时也支持灵活拆分组合微服务模块，从而构建不同形态的服务模式，包括“轻便Air版本”、“专业Pro版”和“大容量Max版”。

- **轻便Air版**：采用all-in-one的封装模式，将所有模块编译成一个二进制（进程），一个进程即为一个区块链节点，包括网络、共识、接入等所有功能模块，采用本地RocksDB存储。它适用于初学者入门、功能验证、POC产品等。

- **专业Pro版**：包括RPC、Gateway两个接入层的服务和多个区块链节点Node服务组成，其中一个Node服务代表一个群组，存储采用本地RocksDB，所有Node共用接入层服务，接入层的两个服务可平行扩展。它适用于容量可控（T级以内）的生产环境，能够支持多群组扩展。

- **大容量Max版**：由各个层的所有服务构成，每个服务都可独立扩展，存储采用分布式存储TiKV，管理采用Tars-Framwork服务。它适用于海量交易上链，需要支持大量数据落盘存储的场景。


更多信息，请[参阅3.0版本系统设计](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/design/index.html)。

**FISCO BCOS 2.0+版本技术文档请查看[这里](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/)。**

## 快速开始

阅读[搭建第一个区块链网络](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/quick_start/air_installation.html)，了解使用FISCO BCOS所需的必要安装和配置，帮助您快速体验FISCO BCOS强大功能。

## 文档

阅读[**技术文档**](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/)，详细了解如何使用FISCO BCOS。

- [版本及兼容](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/change_log/index.html)
- [安装](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/quick_start/air_installation.html)
- [系统设计](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/design/index.html)
- [Java SDK](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/develop/sdk/java_sdk/index.html)
- [JSON-RPC API](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/develop/api.html)

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

FISCO BCOS的开源协议为Apache License 2.0, 详情参见[LICENSE](../LICENSE)。
