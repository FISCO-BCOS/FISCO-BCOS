# 加法同态接口使用说明
加法同态接口，可在FISCO-BCOS的节点上进行加密的加法操作，而不泄露参与加法的具体数字。

包括：

1、[链外工具库](https://github.com/FISCO-BCOS/FISCO-BCOS/tree/master/tool/java/Paillier)：链外工具，可以完成对需要运算的数据完成加解密操作。

2、[合约库](https://github.com/FISCO-BCOS/FISCO-BCOS/blob/master/tool/LibPaillier.sol) ：链上接口，调用此接口在链上进行加法同态运算。

3、[底层同态算法支持](https://github.com/FISCO-BCOS/FISCO-BCOS/blob/master/libevm/ethcall/Paillier.cpp) ：基于EthCall实现，在FISCO-BCOS底层提供高效的加法同态运算。

## 1、链外工具库说明

### 1.1. 同态密钥管理对象（PaillierKeyPair）

- 接口名称：generateKeyPair
- 接口功能说明：生成同态加密的密钥对
- 参数说明

| 输入参数     | 参数类型     | 参数说明                       |
| :------- | :------- | :------------------------- |
| 无        |          |                            |
| **输出参数** | **参数类型** | **参数说明**                   |
| 返回值      | KeyPair  | 生成的密钥对 （其他 ：成功    null：失败） |

### 1.2. 同态算法对象（PaillierCipher）

- 接口名称：encryption
- 接口功能说明：对数据进行同态加密
- 参数说明

| 输入参数      | 参数类型       | 参数说明                       |
| :-------- | :--------- | :------------------------- |
| m         | BigInteger | 待加密的操作数                    |
| publickey | PublicKey  | 加密公钥，可以使用同态密钥管理对象获取        |
| **输出参数**  | **参数类型**   | **参数说明**                   |
| 返回值       | String     | 加密后的输出 （其他 ：成功    null：失败） |

- 接口名称：decryption
- 接口功能说明：对加密数据进行解密还原操作数
- 参数说明

| 输入参数     | 参数类型     | 参数说明     |
| :------- | :------- | :------- |
| 无        |          |          |
| **输出参数** | **参数类型** | **参数说明** |
| 返回值      |          |          |

- 接口名称：encryption
- 接口功能说明：对数据进行同态加密
- 参数说明

| 输入参数       | 参数类型       | 参数说明                        |
| :--------- | :--------- | :-------------------------- |
| ciphertext | String     | 加密的密文数据                     |
| privateKey | PrivateKey | 加密公钥对应的私钥，可以使用同态密钥管理对象获取    |
| **输出参数**   | **参数类型**   | **参数说明**                    |
| 返回值        | BigInteger | 加密后的操作数 （其他 ：成功    null：失败） |

- 接口名称：ciphertextAdd
- 接口功能说明：加法同态实现
- 参数说明

| 输入参数        | 参数类型     | 参数说明                          |
| :---------- | :------- | :---------------------------- |
| Ciphertext1 | String   | 同态加密后的操作数1                    |
| Ciphertext2 | String   | 同态加密后的操作数2                    |
| **输出参数**    | **参数类型** | **参数说明**                      |
| 返回值         | String   | 加法同态后的结果数 （其他 ：成功    null：失败） |

### 1.3. 使用样例

```
// generate the key pair for encrypt and decrypt
KeyPair keypair = PaillierKeyPair.generateKeyPair();
RSAPublicKey pubKey = (RSAPublicKey)keypair.getPublic();
RSAPrivateKey priKey = (RSAPrivateKey)keypair.getPrivate();

// encrypt the first number with public key
BigInteger i1 = BigInteger.valueOf(1000000);
String c1 = PaillierCipher.encryption(i1, pubKey);

// encrypt the second number with same public key
BigInteger i2 = BigInteger.valueOf(2012012012);
String c2 = PaillierCipher.encryption(i2, pubKey);

// paillier add with numbers
String c3 = PaillierCipher.ciphertextAdd(c1,c2);

// decrypt the result
BigInteger o3 = PaillierCipher.decryption(c3, priKey);
```


## 2、 合约库说明

### 2.1. 同态加法库（[LibPaillier.sol](https://github.com/FISCO-BCOS/FISCO-BCOS/blob/master/tool/LibPaillier.sol)）

- 接口名称：paillierAdd
- 接口功能说明：加法同态的合约库接口
- 参数说明

| 输入参数     | 参数类型     | 参数说明                |
| :------- | :------- | :------------------ |
| d1       | string   | 加密后的第一个加法操作数        |
| d2       | string   | 加密后的第二个加法操作数        |
| **输出参数** | **参数类型** | **参数说明**            |
| 返回值      | string   | 成功：同态加法的结果    失败：空串 |

*注：该合约的实现调用了EthCall，更多细节请参考[EthCall说明文档](EthCall说明文档.md)*

### 2.2. 使用样例（[TestPaillier.sol](https://github.com/FISCO-BCOS/FISCO-BCOS/blob/master/tool/TestPaillier.sol)）

```
pragma solidity ^0.4.2;

import "LibPaillier.sol";

contract TestPaillier {

    function add(string d1, string d2) public constant returns (string) {
		return LibPaillier.paillierAdd(d1, d2);
    }
	
	function put() public constant returns (string) {
		return "Hellp";
    }
}
```
