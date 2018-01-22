#!/bin/sh
certtype=$1
if [[ "$certtype" == "ca" ]];then
echo ">>>>>>>>>>>>>>>>>>>>>make ca cert begin<<<<<<<<<<<<<<<<<<<<<<<<<"
openssl genrsa -out ca.key 2048
openssl req -new -x509 -days 3650 -key ca.key -out ca.crt
echo ">>>>>>>>>>>>>>>>>>>>>make ca cert end<<<<<<<<<<<<<<<<<<<<<<<<<<<"
elif [[ "$certtype" == "server" ]];then
echo ">>>>>>>>>>>>>>>>>>>>>make server cert begin<<<<<<<<<<<<<<<<<<<<<<<<<"
cat >cert.cnf <<EOT
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
organizationalUnitName_default = webank
commonName =  Organizational  commonName (eg, webank)
commonName_default = webank
commonName_max = 64
[ v3_req ]
# Extensions to add to a certificate request
basicConstraints = CA:FALSE
keyUsage = nonRepudiation, digitalSignature, keyEncipherment
EOT
cakey=$2
cacrt=$3

if [ ! -f "$cakey" ]; then
echo "ca.key 文件不存在"
elif [ ! -f "$cacrt" ]; then
echo "ca.crt 文件不存在"
else
openssl genrsa -out server.key 2048
openssl req -new -key server.key -config cert.cnf -out server.csr
openssl x509 -req -CA $cacrt -CAkey $cakey -CAcreateserial -in server.csr -out server.crt -extensions v3_req -extfile cert.cnf
echo ">>>>>>>>>>>>>>>>>>>>make server cert end<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<"
fi
else
echo "参数错误! 生成根证书:genkey.sh ca  |  生成用户证书:genkey.sh server ./ca.key ./ca.crt"
fi
