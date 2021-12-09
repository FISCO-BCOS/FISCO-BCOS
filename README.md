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

## Quick start

Read [Build the first blockchain network](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/quick_start/air_installation.html) to understand the necessary installation and configuration required to use FISCO BCOS , To help you quickly experience the powerful functions of FISCO BCOS.

## Documentation

Read [**Technical Documentation**](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/) to learn more about how to use FISCO BCOS.

- [Version and compatibility](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/change_log/index.html)

- [Installation](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/quick_start/air_installation.html)

- [System Design](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/design/index.html)

- [Java SDK](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/develop/sdk/java_sdk/index.html)

- [JSON-RPC API](https://fisco-bcos-doc.readthedocs.io/zh_CN/latest/docs/develop/api.html)
## Featured applications

**FISCO BCOS** has been adopted in over 10 applications in areas like government affairs, finances, charity, health care, education, transport, copyright, product tracing, supply chain, recruitment, agriculture, social communication, and entertainment.

- Finance: inter-institutional reconciliation, supply chain finance, tourism finance, etc.
- Judicial services: arbitration chain, digital IOUs, etc.
- Copyright: copyright registration and trading, etc.
- Social management: real-estate registration, etc.

Featured use cases are provided [here](http://www.fisco-bcos.org/assets/docs/FISCO%20BCOS%20-%20Featured%20Cases.pdf).

## Contributing code

- Your contributions are most welcome and appreciated. Please read the [contribution instructions](https://mp.weixin.qq.com/s/_w_auH8X4SQQWO3lhfNrbQ) and [coding code style guide](CODING_STYLE.md).
- If this project is useful to you, please star us on GitHub project page!

## Join our community

The FISCO BCOS community is one of the most active open-source blockchain communities in China. It provides long-term technical support for both institutional and individual developers and users of FISCO BCOS. Thousands of technical enthusiasts from numerous industry sectors have joined this community, studying and using FISCO BCOS platform. If you are also interested, you are most welcome to join us for more support and fun.

![](https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/images/QR_image_en.png)


## License

[![](https://img.shields.io/github/license/FISCO-BCOS/FISCO-BCOS.svg)](./LICENSE)

All contributions are made under the Apache License 2.0, see [LICENSE](./LICENSE) for details.
