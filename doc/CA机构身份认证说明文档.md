<center> <h1>CA机构身份认证</h1> </center>

<a name="summary" id="summary"></a>
# 功能介绍

联盟链中区块链上所有节点身份必须可追溯，能与现实实体一一对应，做到节点行为所属机构可追溯。
联盟链中区块链上所有节点颁发CA证书，为机构备案公钥证书到区块链，节点连接时验证颁发的CA机构证书有效性判断节点是否准入，对已连接的节点行为进行追溯。
联盟链管理员生成CA根证书并把CA根证书公钥提供给所有节点使用。CA根证书为每个节点颁发CA用户证书或同一机构下所有节点使用同一CA机构证书。


# 使用方式

### 打开SSL通信开关：

1.datadir下编辑config.json配置文件把ssl设置为1："ssl":"1"。
2.重启修改后的区块链节点。
注意：只要联盟链中一个节点开启了ssl功能其他节点都需要设置ssl为1才能正常相连。

### 打开机构身份认证开关

1.打开开关
```shell
cd systemcontractv2
babel-node tool.js ConfigAction set CAVerify true
```
2.验证开关是否打开成功
```shell
babel-node tool.js ConfigAction get CAVerify
```
出现CAVerify=true则代表开关打开成功


### 机构证书序列号上链：

1.获取机构证书序列号

datadir下执行命令，获取证书序列号。
```shell
openssl x509 -noout -in server.crt -serial
```

2.证书序列号写入ca.json文件
```json
{
       "hash":"xxx",//获取的证书序列号
       "pubkey":"",//默认填空即可
       "orgname":"",//根据需要选填
       "notbefore":20170223,//根据需要选填
       "notafter":20180223,//根据需要选填
       "status":1,//0:该机构不准入 1：机构准入
       "whitelist":"",//默认填空即可
       "blacklist":""//默认填空即可
}
```
3.机构证书信息上链
```shell
cd systemcontractv2
babel-node tool.js CAAction update ca.json
babel-node tool.js CAAction all #查看写入的机构信息是否与ca.json相符。
```
4.更新机构证书状态

设置ca.json中的status设置为0表示使用该证书的节点不准入：
```shell
babel-node tool.js CAAction updateStatus ca.json #可以重复录入多个机构证书信息。
babel-node tool.js CAAction all #查看写入的机构信息是否与ca.json相符。
```
重启拒接连接的节点，该节点不能正常连接。
注意:一般该操作由联盟链管理人员去控制节点准入

### 证书注意事项:

妥善保管 server.key，切勿泄露，同时应把证书，server.crt 和 server.key 备份到安全位置


