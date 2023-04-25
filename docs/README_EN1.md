[中文](../README.md) / English

![](./FISCO_BCOS_Logo.svg)

[![codecov](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS/branch/master/graph/badge.svg)](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS)
[![CodeFactor](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS/badge)](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS)
[![GitHub All Releases](https://img.shields.io/github/downloads/FISCO-BCOS/FISCO-BCOS/total.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS)

FISCO BCOS (pronounced /ˈfɪskl bi:ˈkɒz/) is an enterprise-level financial blockchain platform developed and open-sourced by the Financial Services Blockchain Consortium (Shenzhen) "FISCO" led by WeBank. 

In a single-chain configuration, the performance can reach more than 100k TPS. FISCO BCOS provides rich features including group architecture, parallel computing, distributed storage, pluggable consensus mechanism, privacy protection algorithms, and OSCCA-approved cryptography modules.

After a long period of practical testing in the production environment by industry partners, it has financial-grade high performance, high availability and high security. Currently, it has been adopted by over 4,000 enterprises and institutions, with more than 300 industry digital benchmark applications, covering fields such as cultural copyright, judicial services, government services, the Internet of Things, finance, smart communities, real estate construction, community governance, and rural revitalization.

# Table of Contents
- [Table of Contents](#table-of-contents)
- [Documentation](#documentation)
- [System Overview](#system-overview)
    - [Architecture](#architecture)
    - [Function](#function)
    - [Features](#features)
    - [Inheritance And Upgrade](#inheritance-and-upgrade)
- [Featured applications](#featured-applications)
- [Contributing code](#contributing-code)
- [Join our community](#join-our-community)
- [License](#license)

## Documentation

[《The FISCO BCOS Official Technical Documentation》](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/index.html) provides chain development guidelines, tool introductions, and design principle explanations. Users can quickly learn and use FISCO BCOS by reading the official technical documentation.

The documentation covers the following topics:
1. [Quick Start](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/quick_start/hardware_requirements.html)
2. [Contract Development](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/contract_develop/solidity_develop.html)
3. [SDK Tutorial](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/sdk/index.html)
4. [Chain Deployment Tutorial](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/tutorial/air/index.html)
5. [Application Development](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/develop/index.html)
6. [Blockchain Operation and Maintenance Tools](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/operation_and_maintenance/build_chain.html)
7. [Advanced Feature Usage](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/advanced_function/safety.html)
8. [Design Principles](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/design/architecture.html)

## System Overview

### Architecture

Air, Pro, Max: Can be deployed in three different architecture forms.

- **Lightweight Air version**: Has the same form as v2.0, with all functions in one blockchain node (all-in-one). This architecture is simple and can be quickly deployed in any environment. It can be used for blockchain entry, development, testing, POC verification, and other work.
- **Professional Pro version**: This architecture separates the access layer module of the blockchain node into a process, while achieving separate deployment of the access layer and core modules, and allowing the core blockchain module to expand in multiple groups. This architecture implements partition isolation and is suitable for production environments with continuous business expansion.
- **High-capacity Max version**: This architecture provides the ability to switch the primary and backup modules of the core blockchain module, and can parallelly expand computing and storage by deploying multiple machines for transaction executors and accessing distributed storage TiKV. A node in this architecture consists of a series of microservices, but it requires higher operational capabilities and is suitable for scenarios that require massive computing and storage.

### Function

- **Blockchain file system**: WYSIWYG contract data management.
- **SDK basic library**: More convenient national secret access on all platforms.
- **Transaction parallel conflict analysis tool**: Automatically generates transaction conflict variables.
- **WBC-Liquid**: Writing contracts in Rust.
- **Permission governance framework**: Multi-party voting governance blockchain.

### Features

- **Pipeline**: Block pipeline, continuously and compactly generating blocks.
- **Intra-block sharding**: Parallel execution of transactions among applications using DMC+DAG.
- **PBFT**: Immediate consensus algorithm, achieving transaction confirmation in seconds.
- **+TiKV**: Distributed transactional submission, supporting massive storage.


### Inheritance And Upgrade

- **Solidity**: Supports up to version 0.8.11.
- **CRUD**: Stores data in table structure, with more user-friendly interfaces encapsulated in this version for business development.
- **AMOP**: On-chain messenger protocol, using the P2P network of blockchain to achieve information transmission and data communication between applications accessing the blockchain.
- **Disk encryption**: Private keys and data of blockchain nodes are stored in encrypted form on physical hard drives, and cannot be decrypted even if the physical hardware is lost.
- **Cryptographic algorithm**: Built-in group ring signature and other cryptographic algorithms, supporting various secure multi-party computing scenarios.
- **Blockchain monitoring**: Real-time monitoring and data reporting of blockchain status.


## Featured applications

**FISCO BCOS** has been adopted in over hundreds of applications in areas like government affairs, finances, charity, health care, education, transport, copyright, product tracing, supply chain, recruitment, agriculture, social communication, and entertainment.

- **Finance**: inter-institutional reconciliation, supply chain finance, tourism finance, etc.
- **Judicial Deposit**: arbitration chain, digital IOUs, etc.
- **Cultural Copyright**: copyright registration and trading, etc.
- **Social Management**: real-estate registration, community governance etc.
- **Rural Revitalization**: Construction of smart agriculture and animal husbandry big data cloud platforms, digital platforms, etc.
- **Smart Governance**: City brain, provident fund blockchain platform, digital certification projects, etc.

## Contributing code

- Your contributions are most welcome and appreciated. Please read the [contribution instructions](https://mp.weixin.qq.com/s/_w_auH8X4SQQWO3lhfNrbQ).
- If this project is useful to you, please star us on GitHub project page!

## Join our community

The FISCO BCOS open source community is a thriving and expansive community in China. Since its inception, the open source community built around FISCO BCOS has united more than 4,000 enterprises and institutions, along with over 90,000 individual members, to collaborate and share resources. The community has successfully supported hundreds of blockchain application scenarios across key industries such as government affairs, finance, agriculture, public welfare, entertainment, supply chain, and the Internet of Things. With over 300 benchmark applications collected, the community has formed a large and active open source alliance chain ecosystem.

If you have an interest in FISCO BCOS open source technology and applications, we invite you to join the community and take advantage of the support and resources available.

![](https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/images/QR_image_en.png)

## License

All contributions are made under the Apache License 2.0, see [LICENSE](../LICENSE) for details.
