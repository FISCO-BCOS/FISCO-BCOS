[中文](../README.md) / English

![](./FISCO_BCOS_Logo.svg)

[![codecov](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS/branch/master/graph/badge.svg)](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS)
[![CodeFactor](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS/badge)](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS)
[![GitHub All Releases](https://img.shields.io/github/downloads/FISCO-BCOS/FISCO-BCOS/total.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS)

FISCO BCOS (pronounced /ˈfɪskl bi:ˈkɒz/) is a domestically secure and controllable open-source blockchain platform launched in 2017 by the open-source working group of the Financial Blockchain Shenzhen Consortium (FISCO). It is committed to providing trusted infrastructure for the digital economy, unlocking the value of data elements, and empowering industrial digitalization and the Web3.0 economy. To date, more than 600 benchmark applications have been deployed in fields such as finance, government affairs, and public welfare, including the Blockchain-based Service Network (BSN), the Pearl River Delta Credit Chain, the Guangdong-Macao / Shenzhen-Hong Kong / Shenzhen-Singapore cross-border data verification platforms, and the China UnionPay blockchain trusted deposit service.

Core features:

- **Financial-grade performance**: Single-chain performance exceeds 200,000 TPS, with horizontal scaling to meet the needs of large-scale applications. Regulator nodes can be connected for real-time, penetrating supervision. All-around security safeguards are established across cryptographic algorithms, consensus protocols, P2P networking, key management, and privacy protection.
- **Domestic security and controllability**: Developed under the leadership of domestic financial institutions and technology companies, fully self-developed with a series of key technology patents. Chinese national cryptography algorithms are adopted across computing, networking, and storage, with full adaptation to domestic servers, operating systems, databases, and other software and hardware platforms.
- **Openness and open source**: The code and core technologies are fully open source and compatible with mainstream software and hardware platforms at home and abroad. Three node types — consensus node, observer node, and light node — are defined, supporting public access.

## Version Information:

- Stable Version (for production environment): v3.7.3, version details can be found
  in [FISCO-BCOS v3.7.3 Release Notes](https://github.com/FISCO-BCOS/FISCO-BCOS/releases/tag/v3.7.3).

- Latest Version (for experiencing new features): v3.17.0, version details can be found
  in [FISCO-BCOS v3.17.0 Release Notes](https://github.com/FISCO-BCOS/FISCO-BCOS/releases/tag/v3.17.0).

## System Overview
The architecture of FISCO BCOS system consists of the basic layer, core layer, service layer, user layer, and access layer on the left-hand side, providing stable and secure blockchain underlying services. The middleware layer simplifies the process of managing the blockchain system through a visual interface. On the right-hand side, there are supporting components for development, operation, and security control, meeting the needs of different roles during the application implementation process. Meanwhile, privacy protection and cross-chain-related technology components are also provided to satisfy the application requirements in different scenarios.

### key Features:

- Pipelined: A block pipeline that generates blocks continuously and compactly.
- Pluggable Consensus Mechanism: A pluggable consensus framework that is flexible and optional.
- Comprehensive Parallel Computing: Multi-group, intra-block sharding, DMC, DAG, and other parallel mechanisms that achieve powerful processing performance.
- Blockchain File System: A contract data management system that provides a "what you see is what you get" interface.
- Permission Governance Framework: A built-in permission governance framework that allows for multi-party voting to govern the blockchain.
- SDK Basic Library: Multi-language SDK that provides easier access to national encryption on all platforms.

### Component Services:
- Privacy Protection: A privacy protection solution, WeDPR, that is available for scenarios.
- Cross-Chain Collaboration: A cross-chain collaboration platform, WeCross, that supports interoperability across multiple chains.
- Blockchain Management: A visualized blockchain management platform, WeBASE.

### Development and Operations Tools:

- Chain Building Tool: A one-click chain building script that automates blockchain deployment.
- Visualization Tool: A visualization management tool that reduces operational procedures.
- Monitoring and Alerting Tool: A monitoring tool that tracks the operational status of the blockchain system in real-time and alerts users.
- Data Archiving Tool: A cold data archiving tool that supports RocksDB to release storage pressure.
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

If you need to refer to the relevant information of FISCO BCOS 2.x version, you can refer to the [FISCO BCOS 2.x Technical Documentation](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/)


## Featured application

**FISCO BCOS** has landed more than 600 benchmark applications in fields such as finance, government affairs, and public welfare, effectively supporting industrial digitalization and trusted data circulation. Typical applications include:

- Blockchain-based Service Network (BSN)
- Guangdong-Macao / Shenzhen-Hong Kong / Shenzhen-Singapore cross-border data verification platforms
- Shuxin Chain, the blockchain service infrastructure of Sichuan Province
- National marine science data sharing and circulation privacy-computing platform
- Pearl River Delta Credit Chain
- China UnionPay blockchain trusted deposit service

For more cases, please refer to the [2025 FISCO BCOS Industry Application Development Report](https://mp.weixin.qq.com/s/A2vdLtJhhyg9_BBkIwByNA) (in Chinese).

## Join our community

By the end of 2025, the FISCO BCOS open source community had gathered more than 5,000 enterprises and institutions and over 100,000 individual members in co-construction and co-governance, growing into one of the most vibrant blockchain open source communities in China.

Community honors:

- First prize of the 2018 Shenzhen FinTech Special Award
- The first domestic consortium blockchain platform adapted by the Blockchain-based Service Network (BSN) of the State Information Center
- Among the first to pass the national certification for blockchain technology products by the Beijing National Financial Technology Certification Center
- 2 of the 4 Chinese use cases in the ISO standard "Blockchain and distributed ledger technologies — Use cases" are built on FISCO BCOS
- A core technology paper was accepted by SC23, the top international academic conference on supercomputing — the first academic paper on blockchain performance optimization in the history of SC

If you have an interest in FISCO BCOS open source technology and applications, we invite you to join the community and take advantage of the support and resources available.

- [2025 MVP list](https://mp.weixin.qq.com/s/g1ZJZ8LXxgk9rexUg2a0Uw) (in Chinese)
- [FISCO BCOS certified partner list](https://mp.weixin.qq.com/s/IJ87PYxuDPpQzOoXD-XeVQ) (in Chinese)
- [2025 FISCO BCOS Industry Application Development Report](https://mp.weixin.qq.com/s/A2vdLtJhhyg9_BBkIwByNA) (in Chinese)

![](https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/images/QR_image_en.png)

## Contributing code

- Your contributions are most welcome and appreciated. Please read the [contribution instructions](https://mp.weixin.qq.com/s/_w_auH8X4SQQWO3lhfNrbQ).
- If this project is useful to you, please star us on GitHub project page!

## License

All contributions are made under the Apache License 2.0, see [LICENSE](../LICENSE) for details.
