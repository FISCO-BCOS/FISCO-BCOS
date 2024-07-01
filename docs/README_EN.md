[中文](../README.md) / English

![](./FISCO_BCOS_Logo.svg)

[![codecov](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS/branch/master/graph/badge.svg)](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS)
[![CodeFactor](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS/badge)](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS)
[![GitHub All Releases](https://img.shields.io/github/downloads/FISCO-BCOS/FISCO-BCOS/total.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS)

FISCO BCOS (pronounced /ˈfɪskl bi:ˈkɒz/) is an enterprise-level financial blockchain platform developed and open-sourced by the Financial Services Blockchain Consortium (Shenzhen) "FISCO" led by WeBank. Its usability has been extensively tested through practical applications. So far, more than 300 digital benchmark applications have emerged, covering fields such as finance, healthcare, education, culture, and social governance, such as the Pearl River Delta Credit Chain, the Blockchain Service Network BSN, the People's Chain, the National Health and Medical Big Data Innovation Platform, the Guangdong-Hong Kong Health Code Cross-border Recognition System, and so on.

In a single-chain configuration, the performance TPS can reach 100k. It fully supports national encryption algorithms, domestic operating systems, and domestic CPU architecture. FISCO BCOS provides rich features includingcomprehensive parallel computing, blockchain file system, governance framework, and distributed storage.

## Version Information:

- Stable Version (for production environment): v3.2.3, version details can be found
  in [FISCO-BCOS v3.2.3 Release Notes](https://github.com/FISCO-BCOS/FISCO-BCOS/releases/tag/v3.2.3).

- Latest Version (for experiencing new features): v3.7.1, version details can be found
  in [FISCO-BCOS v3.7.1 Release Notes](https://github.com/FISCO-BCOS/FISCO-BCOS/releases/tag/v3.7.1).

## System Overview
The architecture of FISCO BCOS system consists of the basic layer, core layer, service layer, user layer, and access layer on the left-hand side, providing stable and secure blockchain underlying services. The middleware layer simplifies the process of managing the blockchain system through a visual interface. On the right-hand side, there are supporting components for development, operation, and security control, meeting the needs of different roles during the application implementation process. Meanwhile, privacy protection and cross-chain-related technology components are also provided to satisfy the application requirements in different scenarios.
![](https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/document/latest/zh_CN/_images/Technical-Architecture-en.png)

### key Features:

- Pipelined: A block pipeline that generates blocks continuously and compactly.
- Pluggable Consensus Mechanism: A pluggable consensus framework that is flexible and optional.
- Comprehensive Parallel Computing: Multi-group, intra-block sharding, DMC, DAG, and other parallel mechanisms that achieve powerful processing performance.
- Blockchain File System: A contract data management system that provides a "what you see is what you get" interface.
- Permission Governance Framework: A built-in permission governance framework that allows for multi-party voting to govern the blockchain.
- Distributed Storage TiKV: Distributed transactional submission that supports massive storage.
- SDK Basic Library: Multi-language SDK that provides easier access to national encryption on all platforms.

### Component Services:
- Privacy Protection: A privacy protection solution, WeDPR, that is available for scenarios.
- Cross-Chain Collaboration: A cross-chain collaboration platform, WeCross, that supports interoperability across multiple chains.
- Blockchain Management: A visualized blockchain management platform, WeBASE.

### Development and Operations Tools:

- Chain Building Tool: A one-click chain building script that automates blockchain deployment.
- Visualization Tool: A visualization management tool that reduces operational procedures.
- Monitoring and Alerting Tool: A monitoring tool that tracks the operational status of the blockchain system in real-time and alerts users.
- Data Archiving Tool: A cold data archiving tool that supports RocksDB and TiKV to release storage pressure.
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

**FISCO BCOS** has been adopted in over hundreds of applications in areas like government affairs, finances, charity, health care, education, transport, copyright, product tracing, supply chain, recruitment, agriculture, social communication, and entertainment.

- **Finance**: inter-institutional reconciliation, supply chain finance, tourism finance, etc.
- **Judicial Deposit**: arbitration chain, digital IOUs, etc.
- **Cultural Copyright**: copyright registration and trading, etc.
- **Social Management**: real-estate registration, community governance etc.
- **Rural Revitalization**: Construction of smart agriculture and animal husbandry big data cloud platforms, digital platforms, etc.
- **Smart Governance**: City brain, provident fund blockchain platform, digital certification projects, etc.


## Join our community

The FISCO BCOS open source community is a thriving and expansive community in China. Since its inception, the open source community built around FISCO BCOS has united more than 4,000 enterprises and institutions, along with over 90,000 individual members, to collaborate and share resources. The community has successfully supported hundreds of blockchain application scenarios across key industries such as government affairs, finance, agriculture, public welfare, entertainment, supply chain, and the Internet of Things. With over 300 benchmark applications collected, the community has formed a large and active open source alliance chain ecosystem.

If you have an interest in FISCO BCOS open source technology and applications, we invite you to join the community and take advantage of the support and resources available.

![](https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/images/QR_image_en.png)

## Contributing code

- Your contributions are most welcome and appreciated. Please read the [contribution instructions](https://mp.weixin.qq.com/s/_w_auH8X4SQQWO3lhfNrbQ).
- If this project is useful to you, please star us on GitHub project page!

## License

All contributions are made under the Apache License 2.0, see [LICENSE](../LICENSE) for details.
