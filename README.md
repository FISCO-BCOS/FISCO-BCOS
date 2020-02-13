English / [中文](docs/README_CN.md)

![](docs/images/FISCO_BCOS_Logo.svg)

**FISCO BCOS** is a secure and reliable financial-grade open-source blockchain platform led by Chinese enterprises. Its performance has reached over 10,000 TPS with single-chain setup. The platform provides rich features including group architecture, cross-chain communication protocols, pluggable consensus mechanisms, privacy protection algorithms, OSCCA-approved (Office of State Commercial Cryptography Administration) cryptography algorithms, and distributed storage. Its performance, usability, and security have been testified by many institutional users and successful business applications in a live production environment.

[![codecov](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS/branch/master/graph/badge.svg)](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS) [![CodeFactor](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS/badge)](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS) [![Codacy Badge](https://api.codacy.com/project/badge/Grade/08552871ee104fe299b00bc79f8a12b9)](https://www.codacy.com/app/fisco-dev/FISCO-BCOS?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=FISCO-BCOS/FISCO-BCOS&amp;utm_campaign=Badge_Grade) [![GitHub All Releases](https://img.shields.io/github/downloads/FISCO-BCOS/FISCO-BCOS/total.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS) [![Code Lines](https://tokei.rs/b1/github/FISCO-BCOS/FISCO-BCOS?category=code)](https://github.com/FISCO-BCOS/FISCO-BCOS)

[![CircleCI](https://circleci.com/gh/FISCO-BCOS/FISCO-BCOS.svg?style=shield)](https://circleci.com/gh/FISCO-BCOS/FISCO-BCOS)  [![Build Status](https://travis-ci.org/FISCO-BCOS/FISCO-BCOS.svg)](https://travis-ci.org/FISCO-BCOS/FISCO-BCOS)

## Group Architecture

Our group architecture is designed to allow the simultaneous management of multiple ledger groups in the same blockchain network to facilitate speedy ledger setup and support massive service requests. This innovative architecture supports better scalability with key designs including inter-group transaction processing, distributed data storage, and intra-group consensus isolation. It aims to effectively scale up the business and simplify toilsome operational procedures while accommodating to data privacy protection requirements.

## Distributed Storage

Distributed storage framework aims to break the limitations of single-machine resource and boost system capacity and performance. It separates computation and storage units with modularized data exchange API for better flexibility. This framework simplifies data management and accelerate business development flow by defining standard CURD adaptors for common databases like MySQL, and supporting widely-used Key-Value and SQL-like data tables.

## Parallel Transaction Processing

Parallel transaction processing engine effectively exploits multi-core computation resources to parallelize transaction processing. To address key performance bottleneck caused high frequency and high complexity computation, it provides pre-compiled framework to run C++-based smart contracts, which significantly boosts the efficiency of transaction execution.

## Developer Friendly

FISCO BCOS provides various guides to compile and deploy blockchain instances, offering one-click deployment, speedy installation, and deployment on all mainstream platforms including Docker. It includes a comprehensive toolkit for speedy deployment, continuous monitoring, enterprise-level data governance, to save developers’ valuable time from toil.

![Key Features of FISCO BCOS 2.0](https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/images/plane_en.png)

For more information, please refer to new features in [version 2.0](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/what_is_new.html)

## Quick Start

Read [Quick Start](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/installation.html) to learn the installation and deployment procedures, and experience the rich features of FISCO BCOS.

## Technical documentation

- [**English**](https://fisco-bcos-documentation.readthedocs.io/en/latest/)
- [**中文**](https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/)

## Featured applications

**FISCO BCOS** has been adopted in over 10 applications in areas like government affairs, finances, charity, health care, education, transport, copyright, product tracing, supply chain, recruitment, agriculture, social communication, and entertainment.

- Finance: inter-institutional reconciliation, supply chain finance, tourism finance, etc.
- Judicial services: arbitration chain, digital IOUs, etc.
- Copyright: copyright registration and trading, etc.
- Social management: real-estate registration, etc.

Featured use cases are provided [here](http://www.fisco-bcos.org/assets/docs/FISCO%20BCOS%20-%20Featured%20Cases.pdf).

## Code contribution

- Your contributions are most welcome and appreciated. Please read the [contribution instructions](https://mp.weixin.qq.com/s/_w_auH8X4SQQWO3lhfNrbQ) and [coding code style guide](CODING_STYLE.md).
- If this project is useful to you, please star us on GitHub project page!
- If you find any security vulnerabilities, please report [them](https://security.webank.com) here!

## Join Our Community

The FISCO BCOS community is one of the most active open-source blockchain communities in China. It provides long-term technical support for both institutional and individual developers and users of FISCO BCOS. Thousands of technical enthusiasts from numerous industry sectors have joined this community, studying and using FISCO BCOS platform. If you are also interested, you are most welcome to join us for more support and fun.

![](https://raw.githubusercontent.com/FISCO-BCOS/LargeFiles/master/images/QR_image_en.png)

## License

[![](https://img.shields.io/github/license/FISCO-BCOS/FISCO-BCOS.svg)](LICENSE)

All contributions are made under the [GNU General Public License v3](https://www.gnu.org/licenses/gpl-3.0.en.html). See [LICENSE](LICENSE).
