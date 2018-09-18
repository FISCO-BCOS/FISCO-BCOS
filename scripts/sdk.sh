#!/bin/bash

nodePath=$1

if [ -z "$nodePath" ];  then
    echo "Usage:sdk.sh nodePath "
elif [  -d "$nodePath/sdk" ]; then
    echo "$nodePath/sdk DIR exist! please clean old sdk!"
else
    cd  $nodePath
    mkdir -p sdk
    
    # 使用node.key和node.crt，生成p12证书文件，此处要输入密码，按需要输入
    openssl pkcs12 -export -name client -in node.crt -inkey node.key -out keystore.p12
    # 生成JKS keystore，此处要输入JKS keystore的密码和上一步p12的密码，按需要输入
    keytool -importkeystore -destkeystore client.keystore -srckeystore keystore.p12 -srcstoretype pkcs12 -alias client 
    
    mv client.keystore sdk
    cp ca.crt sdk/
    echo "Build $nodePath/sdk Crt suc!!!"
fi
