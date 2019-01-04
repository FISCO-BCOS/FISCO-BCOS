[English](../README.md) / 中文

![](./images/FISCO_BCOS_Logo.svg)

**FISCO BCOS**平台是金融区块链合作联盟（深圳）（以下简称：金链盟）开源工作组以金融业务实践为参考样本，在BCOS开源平台基础上进行模块升级与功能重塑，深度定制的安全可控、适用于金融行业且完全开源的区块链底层平台。FISCO BCOS平台基于现有的BCOS开源项目进行开发，聚焦于金融行业的分布式商业需求，从业务适当性、性能、安全、正常、技术可行性、运维与治理、成本等多个维度进行综合考虑，打造金融版本的区块链解决方案。

[![Build Status](https://travis-ci.org/FISCO-BCOS/lab-bcos.svg)](https://travis-ci.org/FISCO-BCOS/lab-bcos)  [![codecov](https://codecov.io/gh/FISCO-BCOS/lab-bcos/branch/master/graph/badge.svg)](https://codecov.io/gh/FISCO-BCOS/lab-bcos) [![CodeFactor](https://www.codefactor.io/repository/github/fisco-bcos/lab-bcos/badge)](https://www.codefactor.io/repository/github/fisco-bcos/lab-bcos) [![Codacy Badge](https://api.codacy.com/project/badge/Grade/08552871ee104fe299b00bc79f8a12b9)](https://www.codacy.com/app/fisco-dev/lab-bcos?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=FISCO-BCOS/lab-bcos&amp;utm_campaign=Badge_Grade) [![](https://img.shields.io/github/issues-pr/FISCO-BCOS/lab-bcos.svg)](https://github.com/FISCO-BCOS/lab-bcos/pulls) [![GitHub issues](https://img.shields.io/github/issues/FISCO-BCOS/lab-bcos.svg)](https://github.com/FISCO-BCOS/lab-bcos/issues) [![GitHub All Releases](https://img.shields.io/github/downloads/FISCO-BCOS/lab-bcos/total.svg)](https://github.com/FISCO-BCOS/lab-bcos) 

## 快速入门

### 搭建区块链

FISCO BCOS提供了搭链脚本 [`build_chain.sh`](tools/build_chain.sh) ，方便用户快速搭建区块链。例如，在本机（`127.0.0.1`）上生成一条有4个节点的区块链：

```bash
curl -LO https://raw.githubusercontent.com/FISCO-BCOS/lab-bcos/dev/tools/build_chain.sh
bash build_chain.sh -l "127.0.0.1:4"
```

节点生成后，启动所有节点，一条FISCO BCOS区块链即可运行起来。

```bash
cd ./nodes
bash start_all.sh
```

更多的节点部署架构，可参考 XXX.

### 使用区块链

区块链是智能合约的载体，要使用区块链，可参考：智能的部署和使用XXX。

## 源码编译部署

下载代码

```bash
git clone https://github.com/FISCO-BCOS/lab-bcos.git
```

安装依赖

```bash
# Ubuntu
sudo apt install cmake libssl-dev libleveldb-dev openssl
# CentOS
sudo yum install cmake3 gcc-c++ openssl-devel leveldb-devel openssl
```

编译部署

```bash
cd lab-bcos
$ mkdir build && cd build
$ cmake -DTESTS=ON  ..
$ make -j$(nproc)
```

## 贡献代码

要参与进来，很简单，你可以：

* 点亮我们的小星星！  
* 提交代码（Pull requests），我们有 [代码贡献流程](CONTRIBUTING.md) 和 [编码规范](CODING_STYLE.md)。
* 也可以[提问](https://github.com/FISCO-BCOS/lab-bcos/issues) 和 [提交BUG](https://github.com/FISCO-BCOS/lab-bcos/issues)。
* 或者可以在i[微信群](docs/images/WeChatQR.jpeg) 和 [Gitter](https://gitter.im/fisco-bcos/Lobby)里讨论。

## 更多参考

FISCO BCOS 提供了完整的文档：[Read the Doc](https://fisco-bcos-documentation-en.readthedocs.io/en/latest/)。

### 关键特性

* **安全**：机构证书准入，秘钥管理
* **隐私**：零知识证明，同态加密，群签名，环签名
* **性能**：多链跨链，PBFT
* **可用**：合约开发框架（web3sdk），搭链工具，监控统计
* **稳定**：在生产环境持续稳定运行，并不断迭代更新


### 落地应用案例

FISCO BCOS有很多的应用案例。此处提供一些具有代表性的[落地应用案例](http://www.fisco-bcos.org/assets/docs/FISCO%20BCOS%20-%20Featured%20Cases.pdf)。

## 社区生态

**金链盟**开源工作组，获得金链盟成员机构的广泛认可，并由专注于区块链底层技术研发的成员机构及开发者牵头开展工作。其中首批成员包括以下单位（排名不分先后）：博彦科技、华为、深证通、神州数码、四方精创、腾讯、微众银行、越秀金科。

- 微信群：[![Scan](https://img.shields.io/badge/style-Scan_QR_Code-green.svg?logo=wechat&longCache=false&style=social&label=Group)](docs/images/WeChatQR.jpeg) 


- Gitter：[![Gitter](https://img.shields.io/badge/style-on_gitter-green.svg?logo=gitter&longCache=false&style=social&label=Chat)](https://gitter.im/fisco-bcos/Lobby) 


- Twitter：[![](https://img.shields.io/twitter/url/http/shields.io.svg?style=social&label=Follow@FiscoBcos)](https://twitter.com/FiscoBcos)


- e-mail：[![](https://img.shields.io/twitter/url/http/shields.io.svg?logo=Gmail&style=social&label=service@fisco.com.cn)](mailto:service@fisco.com.cn)


## License

[![](https://img.shields.io/github/license/FISCO-BCOS/lab-bcos.svg)](LICENSE)

FISCO BCOS的开源协议为 [GPL 3.0](https://www.gnu.org/licenses/gpl-3.0.en.html)。详情参见[LICENSE](LICENSE)。  