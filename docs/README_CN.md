[English](../README.md) / 中文

![](./images/FISCO_BCOS_Logo.svg)

**FISCO BCOS**是首个由国内企业主导研发、对外开源、安全可控的企业级金融联盟链底层平台，由金融区块链合作联盟（深圳）（简称：金链盟）成立的开源工作组协作打造，于2017年12月正式对外开源，工作组成员包括博彦科技、华为、深证通、神州数码、四方精创、腾讯、微众银行、亦笔科技和越秀金科等。

从业务适当性、性能、安全、技术可行性、运维与治理、成本等多个维度综合考虑后，**FISCO BCOS**选择联盟链技术方向，单链TPS可达万级。平台支持多种共识算法，如PBFT、Raft；提供易用的工具，开发者可在极短时间内完成联盟链部署；支持基于智能合约和多种业务模板开发应用；采用权限控制、隐私保护等策略保障业务安全和隐私。

作为一个稳定、安全、高效、易用的联盟区块链底层技术平台，**FISCO BCOS**经过了外部多家机构、多个应用长时间生产环境运行的实践检验；目前已经有数百家机构、数千名社区技术爱好者在研究和使用FISCO BCOS，落地应用已达数十个，场景覆盖政务、金融、公益、医疗、教育、交通、版权、商品溯源、供应链、招聘、农业、社交、游戏等多个领域。


[![codecov](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS/branch/master/graph/badge.svg)](https://codecov.io/gh/FISCO-BCOS/FISCO-BCOS) [![CodeFactor](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS/badge)](https://www.codefactor.io/repository/github/fisco-bcos/FISCO-BCOS) [![Codacy Badge](https://api.codacy.com/project/badge/Grade/08552871ee104fe299b00bc79f8a12b9)](https://www.codacy.com/app/fisco-dev/FISCO-BCOS?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=FISCO-BCOS/FISCO-BCOS&amp;utm_campaign=Badge_Grade) [![Code Lines](https://tokei.rs/b1/github/FISCO-BCOS/FISCO-BCOS?category=code)](https://github.com/FISCO-BCOS/FISCO-BCOS) [![GitHub All Releases](https://img.shields.io/github/downloads/FISCO-BCOS/FISCO-BCOS/total.svg)](https://github.com/FISCO-BCOS/FISCO-BCOS) 

[![CircleCI](https://circleci.com/gh/FISCO-BCOS/FISCO-BCOS.svg?style=shield)](https://circleci.com/gh/FISCO-BCOS/FISCO-BCOS)  [![Build Status](https://travis-ci.org/FISCO-BCOS/FISCO-BCOS.svg)](https://travis-ci.org/FISCO-BCOS/FISCO-BCOS)

## 文档

阅读[**文档**](https://fisco-bcos-documentation.readthedocs.io/zh_CN/release-2.0/)，详细了解如何使用FISCO BCOS。

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

## 关键特性

- **安全**：全方位安全保障、准入机制、CA认证、密钥管理等。
- **隐私**：权限管理、国密、同态加密、零知识证明、环签名、群签名等。
- **性能**：群组可扩展架构、分布式存储、预编译合约、DAG可并行合约、高效并行PBFT等，单链TPS可达万级，支持并行多链的平行扩展能力。
- **易用**：更便利地学习上手、开发、调试、部署、运营、监控、审计。
- **可靠**：经过外部多家机构、多个应用，长时间生产环境运行的实践检验。

## 兼容性说明

2.0版本与1.0版本数据和协议不兼容，合约兼容。如果要从1.3版本升级到2.0版本，需要做数据迁移。

## 落地应用案例

FISCO BCOS已落地应用达数十个，场景覆盖政务、金融、公益、医疗、教育、交通、版权、商品溯源、供应链、招聘、农业、社交、游戏等多个领域，如：

- 金融业：机构间对账、供应链金融、旅游金融等。
- 司法服务：仲裁链、电子借据等。
- 文化版权：版权存证与交易等。
- 社会管理：不动产登记等。

此处提供一些具有代表性的[落地应用案例](https://mp.weixin.qq.com/s/vUSq80LkhF8yCfUF7AILgQ)。

## 社区生态

**FISCO BCOS开源社区**目前是国内较为活跃的区块链开源社区之一，技术项目涵盖底层平台、客户端、管理台、浏览器、密钥安全、国密支持、隐私保护、业务案例等完备的软件架构体系。

目前社区活跃着数千名开发者和关注者，来自全国乃至全世界的几百家企业，各行业的研发及业务团队，大学、研究所等科研机构，共同面向社区提供形式多样的支持，包括在线社群、线下活动、教学培训、项目技术支持、开发者大赛以及产业孵化等。

## 贡献代码

要参与进来，很简单，你可以：

- Star我们！
- 提交代码(Pull requests)，我们有[代码贡献流程](https://mp.weixin.qq.com/s/hEn2rxqnqp0dF6OKH6Ua-A
)和[编码规范](../CODING_STYLE.md)。
- 也可以[提问](https://github.com/FISCO-BCOS/FISCO-BCOS/issues)和[提交BUG](https://github.com/FISCO-BCOS/FISCO-BCOS/issues)。
- 加入[微信群](https://github.com/FISCO-BCOS/FISCO-BCOS-DOC/blob/release-2.0/images/community/WeChatQR.jpg)反馈和交流。
- 欢迎反馈代码存在的安全漏洞，如果发现，请在[这里](https://security.webank.com)上报。

## 加入我们

![](https://media.githubusercontent.com/media/FISCO-BCOS/LargeFiles/master/images/QR_image.png)

## License

[![](https://img.shields.io/github/license/FISCO-BCOS/FISCO-BCOS.svg)](../LICENSE)

FISCO BCOS的开源协议为[GPL 3.0](https://www.gnu.org/licenses/gpl-3.0.en.html)。详情参见[LICENSE](../LICENSE)。  
