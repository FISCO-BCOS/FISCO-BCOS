#!/bin/bash
certtype=$1
if [ "$certtype" == "ca" ];then
echo ">>>>>>>>>>>>>>>>>>>>>make ca cert begin<<<<<<<<<<<<<<<<<<<<<<<<<"
certdate=$2

if [ ! -n "$certdate" ]; then
echo "参数错误!" 
echo "生成根证书:genkey.sh ca date   ep:genkey.sh ca 3650(ca证书有效期为3650天)"
echo "生成用户证书:genkey.sh server ./ca.key ./ca.crt date   ep:genkey.sh server ./ca.key ./ca.crt 365(server证书有效期为365天)" 
echo "生成p12证书文件:genkey.sh p12 ./server.key ./server.crt ./ca.crt"
else
openssl genrsa -out ca.key 2048
openssl req -new -x509 -days $certdate -key ca.key -out ca.crt
fi

echo ">>>>>>>>>>>>>>>>>>>>>make ca cert end<<<<<<<<<<<<<<<<<<<<<<<<<<<"
elif [ "$certtype" == "server" ];then
echo ">>>>>>>>>>>>>>>>>>>>>make server cert begin<<<<<<<<<<<<<<<<<<<<<<<<<"
cat >cert.cnf <<EOT
[ca]
default_ca=default_ca
[default_ca]
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
EOT
cakey=$2
cacrt=$3
certdate=$4
if [ ! -f "$cakey" ]; then
echo "ca.key 文件不存在"
elif [ ! -f "$cacrt" ]; then
echo "ca.crt 文件不存在"
elif [ ! -n "$certdate" ]; then
echo "未设置证书有效期"
else
openssl genrsa -out server.key 2048
openssl req -new -key server.key -config cert.cnf -out server.csr
openssl x509 -req -days $certdate -CA $cacrt -CAkey $cakey -CAcreateserial -in server.csr -out server.crt -extensions v3_req -extfile cert.cnf
echo ">>>>>>>>>>>>>>>>>>>>make server cert end<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<"
fi
elif [ "$certtype" == "p12" ]; then
serverkey=$2
servercrt=$3
cacrt=$4
echo ">>>>>>>>>>>>>>>>>>>>make server p12 begin<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<"
if [ ! -f "$serverkey" ]; then
echo "server.key 文件不存在"
elif [ ! -f "$servercrt" ]; then
echo "server.crt 文件不存在"
elif [ ! -f "$cacrt" ]; then
echo "ca.crt 文件不存在"
else
openssl pkcs12 -export -in $servercrt -inkey $serverkey -certfile $cacrt -out server.p12
echo ">>>>>>>>>>>>>>>>>>>>make server p12 end<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<"
fi
else
echo "参数错误!" 
echo "生成根证书:genkey.sh ca date   ep:genkey.sh ca 3650(ca证书有效期为3650天)"
echo "生成用户证书:genkey.sh server ./ca.key ./ca.crt date   ep:genkey.sh server ./ca.key ./ca.crt 365(server证书有效期为365天)" 
echo "生成p12证书文件:genkey.sh p12 ./server.key ./server.crt ./ca.crt"
fi
