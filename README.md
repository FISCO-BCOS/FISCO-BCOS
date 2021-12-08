English / [中文](docs/README_CN.md)

![](./images/FISCO_BCOS_Logo.svg)


[![codecov](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS/branch/master/graph/badge.svg)](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS)
[![CodeFactor](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS/badge)](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS)
[![GitHub All Releases](https://img.shields.io/github/downloads/FISCO-BCOS/FISCO-BCOS/total.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS)
[![Code Lines](https://tokei.rs/b1/github/FISCO-BCOS/FISCO-BCOS?category=code)](https://github.com/FISCO-BCOS/FISCO-BCOS)
 [![version](https://img.shields.io/github/tag/FISCO-BCOS/FISCO-BCOS.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS/releases/latest)

[![Github Actions](https://github.com/FISCO-BCOS/FISCO-BCOS/workflows/FISCO-BCOS%20GitHub%20Actions/badge.svg)](https://travis-ci.org/FISCO-BCOS/FISCO-BCOS)
[![CircleCI](https://circleci.com/gh/FISCO-BCOS/FISCO-BCOS.svg?style=shield)](https://circleci.com/gh/FISCO-BCOS/FISCO-BCOS)

FISCO BCOS is an enterprise-level financial blockchain underlying technology platform that is researched and developed by Chinese companies, open sourced to the outside world, and safe and controllable. FISCO BCOS 3.0 adopts a microservice architecture, but at the same time it also supports flexible splitting and combining of microservice modules to construct different forms of service models, including "lightweight Air version", "professional Pro version" and "large capacity Max version".

-**Light Air Edition**: Adopt all-in-one packaging mode, compile all modules into a binary (process), a process is a blockchain node, including all functions such as network, consensus, access, etc. Module, using local RocksDB storage. It is suitable for beginners, functional verification, POC products, etc.

- **Professional Pro version**: It includes two access layer services, RPC and Gateway, and multiple blockchain node Node services. One Node service represents a group. The storage uses local RocksDB. All Nodes share access layer services. The two services of the layer can be extended in parallel. It is suitable for production environments with controllable capacity (within T level) and can support multi-group expansion.

- **Large Capacity Max Version**: It is composed of all services of each layer, each service can be independently expanded, storage uses distributed storage TiKV, and management uses Tars-Framwork service. It is suitable for scenarios where massive transactions are on the chain and a large amount of data needs to be stored on disk.


For more information, please refer to [the system design version 3.0](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/design/index.html).

**For FISCO BCOS 2.0+ version technical documentation, please view [here](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/).**

## 快速开始

阅读[搭建第一个区块链网络](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/quick_start/air_installation.html)，了解使用FISCO BCOS所需的必要安装和配置，帮助您快速体验FISCO BCOS强大功能。

## Quick start

阅读[**技术文档**](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/)，详细了解如何使用FISCO BCOS。

- [Version and compatibility](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/change_log/index.html)

- [Installation](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/quick_start/air_installation.html)

- [System Design](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/design/index.html)

- [Java SDK](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/develop/sdk/java_sdk/index.html)

- [JSON-RPC API](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/develop/api.html)
## Landing application case

There are dozens of FISCO BCOS applications, covering government affairs, finance, public welfare, medical care, education, transportation, copyright, product traceability, supply chain, recruitment, agriculture, social interaction, games and other fields, such as:

- Financial industry: inter-institutional reconciliation, supply chain finance, tourism finance, etc.

- Judicial services: arbitration chain, electronic IOUs, etc.

- Cultural copyright: copyright deposit and trading, etc.

- Social management: real estate registration, etc.

Here are some representative [landing application cases](https://mp.weixin.qq.com/s/cUjuWf1eGMbG3AFq60CBUA).

## Contributing code

- We welcome and thank you very much for your contribution, please refer to [Code Contribution Process](https://mp.weixin.qq.com/s/_w_auH8X4SQQWO3lhfNrbQ) and [Code Specification](../CODING_STYLE.md).

- If the project is helpful to you, welcome star support!

## Join our community

**FISCO BCOS Open Source Community** is an active open source community in China. The community has long provided various types of support and assistance to institutions and individual developers. Thousands of technology enthusiasts from various industries have been researching and using FISCO BCOS. If you are interested in FISCO BCOS open source technology and applications, welcome to join the community for more support and help.

![](https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/images/QR_image.png)

## License

[![](https://img.shields.io/github/license/FISCO-BCOS/FISCO-BCOS.svg)](./LICENSE)

The open source agreement of FISCO BCOS is Apache License 2.0, see [LICENSE](./LICENSE) for details.
