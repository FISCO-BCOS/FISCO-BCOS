<center> <h1>CA机构身份认证</h1> </center>

<a name="summary" id="summary"></a>
# 一、功能介绍

联盟链中区块链上所有节点身份必须可追溯，能与现实实体一一对应，做到节点行为所属机构可追溯。
联盟链中区块链上所有节点颁发CA证书，为机构备案公钥证书到区块链，节点连接时验证颁发的CA机构证书有效性判断节点是否准入，对已连接的节点行为进行追溯。
联盟链管理员生成CA根证书并把CA根证书公钥提供给所有节点使用。CA根证书为每个节点颁发CA用户证书或同一机构下所有节点使用同一CA机构证书。


# 二、使用方式

> 机构证书的准入依赖系统合约，在进行机构证书准入操作前，再次请确认：
>
> （1）系统合约已经被正确的部署。
>
> （2）所有节点的config.json的systemproxyaddress字段已经配置了相应的系统代理合约地址。
>
> （3）节点在配置了systemproxyaddress字段后，已经重启使得系统合约生效。
>
> （4）/mydata/FISCO-BCOS/systemcontractv2/下的config.js已经正确的配置了节点的RPC端口。

### 2.1 配置节点证书

节点的证书存放目录在节点文件目录的data文件夹下。包括：

- ca.crt：区块链根证书公钥，所有节点共用。
- server.crt：单个节点的证书公钥，可公开。
- server.key：单个节点的证书私钥，应保密。

证书文件应严格按照上述命名方法命名。

FISCO BCOS通过授权某节点对应的公钥server.crt，控制此节点是否能够与其它节点正常通信。

**注意：若要尝试使用[AMOP（链上链下）](amop使用说明文档.md)，请直接使用sample目录下的证书。AMOP暂不支持与新生成的证书进行连接。**

**（1）生成根证书ca.crt**

> 执行命令，可生成根证书公私钥ca.crt、ca.key。ca.key应保密，并妥善保管。供生成节点证书使用。

```shell
cd /mydata/nodedata-1/data/
openssl genrsa -out ca.key 2048
openssl req -new -x509 -days 3650 -key ca.key -out ca.crt
```

**（2）生成节点证书server.key、server.crt**

> 生成节点证书时需要根证书的公私钥ca.crt、ca.key。执行命令，生成节点证书server.key、server.crt。

> 直接编写配置文件cert.cnf。

```shell
vim /mydata/nodedata-1/data/cert.cnf
```

> 内容如下，无需做任何修改。

```text
拷贝以下内容在本地保存为cert.cnf文件
[ca]
default_ca=default_ca
[default_ca]
default_days = 365
default_md = sha256
[req] 
distinguished_name = req_distinguished_name 
req_extensions = v3_req
[req_distinguished_name] 
countryName = CN
countryName_default = CN 
stateOrProvinceName = State or Province Name (full name) 
stateOrProvinceName_default =GuangDong 
localityName = Locality Name (eg, city) 
localityName_default = ShenZhen 
organizationalUnitName = Organizational Unit Name (eg, section) 
organizationalUnitName_default = fisco
commonName =  Organizational  commonName (eg, fisco)
commonName_default = fisco
commonName_max = 64 
[ v3_req ] 
# Extensions to add to a certificate request 
basicConstraints = CA:FALSE 
keyUsage = nonRepudiation, digitalSignature, keyEncipherment
```

> 生成节点证书时需要根证书的公私钥ca.crt、ca.key。执行命令，用cert.cnf，生成节点证书server.key、server.crt。

```shell
cd /mydata/nodedata-1/data/
openssl genrsa -out server.key 2048
openssl req -new -key server.key -config cert.cnf -out server.csr
openssl x509 -req -CA ca.crt -CAkey ca.key -CAcreateserial -in server.csr -out server.crt -extensions v3_req -extfile cert.cnf
```

### 2.2 开启所有节点的SSL验证功能

在进行节点证书授权管理前，需开启区块链上每个节点的SSL验证功能。

> 此处以创世节点为例，其它节点也应采用相同的操作。

```shell
cd /mydata/nodedata-1/
vim config.json
```

> 将ssl字段置为1，效果如下。

```json
"ssl":"1",
```

> 修改完成后重启节点，使其生效。

```shell
./stop.sh
./start.sh
```

> 其它节点也采用相同的操作，开启SSL验证功能。

### 2.3 配置机构证书信息

将节点的证书写入系统合约，为接下来的证书准入做准备。每张证书都应该写入系统合约中。节点的证书若不写入系统合约，相应的节点将不允许通信。

#### 2.3.1 获取证书序列号

> 获取server.crt的序列号

```shell
cd /mydata/nodedata-2/data
openssl x509 -noout -in server.crt -serial
```

> 可得到证书序列号

```log
serial=8A4B2CDE94348D22
```

#### 2.3.2 编写证书准入状态文件

> 在systemcontractv2目录下编写。

```shell
/mydata/FISCO-BCOS/systemcontractv2
vim ca.json
```

> 将序列号填入hash字段。配置status，0表示不可用，1表示可用。其它字段默认即可。如下，让node2的证书可用。即status置1。

```json
{
        "hash" : "8A4B2CDE94348D22",
        "status" : 1,
        "pubkey":"",
        "orgname":"",
        "notbefore":20170223,
        "notafter":20180223,
        "whitelist":"",
        "blacklist":""
}
```

证书准入状态文件其它字段说明如下。

| **字段**    | **说明**          |
| --------- | --------------- |
| hash      | 公钥证书序列号         |
| pubkey    | 公钥证书，填空即可       |
| orgname   | 机构名称，填空即可       |
| notbefore | 证书生效时间          |
| notafter  | 证书过期时间          |
| status    | 证书状态 0：不可用 1：可用 |
| whitelist | ip白名单，填空即可      |
| blacklist | ip黑名单，填空即可      |

#### 2.3.3 将证书准入状态写入系统合约

> 执行以下命令，指定证书准入状态文件ca.json，将证书状态写入系统合约。

```shell
babel-node tool.js CAAction update ca.json
```

### 2.4 设置证书验证开关

证书验证开关能够控制是否采用证书准入机制。开启后，将根据系统合约里的证书状态（status）控制节点间是否能够通信。不在系统合约中的证书对应的节点，将不允许通信。

#### 2.4.1 开启全局开关

> 执行命令，CAVerify设置为true

```shell
babel-node tool.js ConfigAction set CAVerify true
```

> 查看开关是否生效

```shell
babel-node tool.js ConfigAction get CAVerify
```

> 输出true，表示开关已打开

```log
CAVerify=true,29
```

#### 2.4.2 关闭全局开关

开关关闭后，节点间的通信不再验证证书。

> 执行命令，CAVerify设置为false

```shell
babel-node tool.js ConfigAction set CAVerify false
```

### 2.5 修改节点证书准入状态

已经写入系统合约的证书状态，允许修改（可用/不可用）

#### 2.5.1 修改证书准入状态文件

> 修改相应证书对应的证书准入状态文件ca.json

```shell
/mydata/FISCO-BCOS/systemcontractv2
vim ca.json
```

> 配置status，0表示不可用，1表示可用。其它字段默认即可。如下，让node2的证书不可用。即status置0。

```json
{
        "hash" : "8A4B2CDE94348D22",
        "status" : 0,
        "pubkey":"",
        "orgname":"",
        "notbefore":20170223,
        "notafter":20180223,
        "whitelist":"",
        "blacklist":""
}
```

#### 2.5.2 更新证书准入状态

> 执行以下命令，指定证书准入状态文件ca.json，更新证书准入状态。

```shell
babel-node tool.js CAAction updateStatus ca.json
```

> 查看证书状态

```shell
babel-node tool.js CAAction all
```

>可看到证书状态

```log
----------CA 0---------
hash=8A4B2CDE94348D22
pubkey=
orgname=
notbefore=20170223
notafter=20180223
status=0
blocknumber=36
whitelist=
blacklist=
```

# 三、证书注意事项:

妥善保管 server.key，切勿泄露，同时应把证书，server.crt 和 server.key 备份到安全位置


