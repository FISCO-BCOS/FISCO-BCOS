#!/bin/bash

if [ "" = "`openssl ecparam -list_curves | grep secp256k1`" ];
then
    echo "Current Openssl Don't Support secp256k1 ! Please Upgrade Openssl To  OpenSSL 1.0.2k-fips"
    exit;
fi

agency=$1
node=$2

if [ -z "$agency" ];  then
    echo "Usage:node.sh   agency_name node_name "
elif [ -z "$node" ];  then
    echo "Usage:node.sh   agency_name node_name "
elif [ ! -d "$agency" ]; then
    echo "$agency DIR Don't exist! please Check DIR!"
elif [ ! -f "$agency/agency.key" ]; then
    echo "$agency/agency.key  Don't exist! please Check DIR!"
elif [  -d "$agency/$node" ]; then
    echo "$agency/$node DIR exist! please clean all old DIR!"
else
    cd  $agency
    mkdir -p $node
    
    openssl ecparam -out node.param -name secp256k1
    openssl genpkey -paramfile node.param -out node.key
    openssl pkey -in node.key -pubout -out node.pubkey
    openssl req -new -key node.key -config cert.cnf  -out node.csr
    openssl x509 -req -days 3650 -in node.csr -CAkey agency.key -CA agency.crt -force_pubkey node.pubkey -out node.crt -CAcreateserial -extensions v3_req -extfile cert.cnf
    openssl ec -in node.key -outform DER |tail -c +8 | head -c 32 | xxd -p -c 32 | cat >node.private
    openssl ec -in node.key -text -noout | sed -n '7,11p' | sed 's/://g' | tr "\n" " " | sed 's/ //g' | awk '{print substr($0,3);}'  | cat >node.nodeid

    if [ "" != "`openssl version | grep 1.0.2k`" ];
    then
        openssl x509  -text -in node.crt | sed -n '5p' |  sed 's/://g' | tr "\n" " " | sed 's/ //g' | sed 's/[a-z]/\u&/g' | cat >node.serial
    else
        openssl x509  -text -in node.crt | sed -n '4p' | sed 's/ //g' | sed 's/.*(0x//g' | sed 's/)//g' |sed 's/[a-z]/\u&/g' | cat >node.serial
    fi

    

    cp ca.crt agency.crt $node
    mv node.key node.csr node.crt node.param node.private node.pubkey node.nodeid node.serial  $node

    cd $node
    

    nodeid=`cat node.nodeid | head`
    serial=`cat node.serial | head`
    
    cat>node.json <<EOF
 {
 "id":"$nodeid",
 "name":"$node",
 "agency":"$agency",
 "caHash":"$serial"
}
EOF

	cat>node.ca <<EOF
	{
	"serial":"$serial",
	"pubkey":"$nodeid",
	"name":"$node"
	}
EOF

    echo "Build  $node Crt suc!!!"
fi
