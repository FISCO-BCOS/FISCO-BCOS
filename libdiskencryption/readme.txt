*********************************数据落盘加密存储*******************************
----------------------------config.json新增字段说明----------------------------
#cryptomod: 0:不加密  1:本地加密 2:keycenter加密（根据加密模式选填）



----------------------------cryptomod.json字段说明-----------------------------
#cryptomod：0：不加密 1：本地加密 3：keycenter加密（必填）      
#rlpcreatepath: network.rlp network.rlp.pub文件生成路径（必填）
#datakeycreatepath：datakey文件生成路径，启用加密模式1,2时dakakey生成路径 如其它填空（根据加密模式选填）
#keycenterurl:加密模式2时 keycenter访问地址（https://127.0.0.1:8888）如其它填空（根据加密模式选填）
#superkey:加密模式1时配置的密码  如其它填空（根据加密模式选填）

#密钥生成-----当不使用加密时：eth --gennetworkrlp cryptomod.json指令生成network.rlp 和 network.rlp.pub文件
#密钥生成-----当使用加密1,2时 ：eth --gennetworkrlp cryptomod.json指令生成networ.rlp加密文件，datakey加密文件，network.rlp.pub文件
#节点启动-----当使用加密模式 1,2  cryptomod.json与datakey文件需与network.rlp文件放在同一目录下，不使用加密文件不处理