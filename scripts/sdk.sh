#!/bin/bash

nodePath=`pwd`/$1
CLIENTCERT_PWD=123456
KEYSTORE_PWD=123456
if [ $# -eq 1  ];  then
    nodePath=`pwd`/$1
elif [  $# -eq 2 ]; then
    nodePath=`pwd`/$1
    CLIENTCERT_PWD=$2
elif [  $# -eq 3 ]; then
    nodePath=`pwd`/$1
    CLIENTCERT_PWD=$2
    KEYSTORE_PWD=$3
else
    echo "Usage:sdk.sh nodePath [clientCert_PWD] [KEYSTORE_PWD]"
    echo "e.g:sdk.sh node-0 123456 123456"
    exit 1
fi
echo "sdk path=$nodePath/sdk"
if [  -d "$nodePath/sdk" ]; then
    echo "DIR exist! please clean all old DIR!"
    exit 1
fi

echo "Use CLIENT_CERT_PWD=${CLIENTCERT_PWD}"
echo "Use KEYSTORE_PWD=${KEYSTORE_PWD}"

cd  $nodePath
mkdir -p $nodePath/sdk

echo ${CLIENTCERT_PWD} > $nodePath/sdk/pwd.conf
# 使用node.key和node.crt，生成p12证书文件，此处要输入密码，按需要输入
openssl pkcs12 -export -name client -in $nodePath/data/node.crt -inkey $nodePath/data/node.key -out keystore.p12 -password file:sdk/pwd.conf
# 生成JKS keystore，此处要输入JKS keystore的密码和上一步p12的密码，按需要输入
keytool -importkeystore -destkeystore client.keystore -srckeystore keystore.p12 -srcstoretype pkcs12 -alias client -srcstorepass ${CLIENTCERT_PWD} -deststorepass ${KEYSTORE_PWD}

mv client.keystore sdk/
cp $nodePath/data/ca.crt sdk/
echo "=============================="
echo "Build $nodePath/sdk Crt suc!!!"

