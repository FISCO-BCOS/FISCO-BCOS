#!/bin/bash

set -e

ca_file= #CA私钥文件
node_num=1 #节点数量
ip_file=
ip_param=
use_ip_param=
ip_array=
output_dir=./nodes/ #输出目录
port_start=30300 #起始端口
CLIENTCERT_PWD=123456
KEYSTORE_PWD=123456
statedb_type=leveldb #存储
eth_path=
make_tar=
Download=false
Download_Link=https://github.com/FISCO-BCOS/FISCO-BCOS/releases/download/v1.5.0-pre-release/fisco-bcos

help() {
	echo $1
	cat << EOF
Usage:
	-l <IP list>                [Required] "ip1:nodeNum1,ip2:nodeNum2" e.g:"192.168.0.1:2,192.168.0.2:3"
	-f <IP list file>           split by line, "ip:nodeNum"
	-e <FISCO-BCOS binary path> Default download from github
	-a <CA Key>                 Default Generate a new CA
	-o <Output Dir>             Default ./nodes/
	-p <Start Port>             Default 30300
	-c <ClientCert Passwd>      Default 123456
	-k <Keystore Passwd>        Default 123456
	-s <StateDB type>           Default leveldb. if set -s, use amop
	-t <Cert config file>       Default auto generate
	-z <Generate tar packet>    Default no
	-h Help
e.g 
    build_chain.sh -l "192.168.0.1:2,192.168.0.2:2"
EOF

exit 0
}

fail_message()
{
	echo $1
	false
}

while getopts "a:n:o:p:e:c:k:f:l:t:szh" option;do
	case $option in
	a) ca_file=$OPTARG;;
	f) ip_file=$OPTARG
	   use_ip_param="false"
	;;
	l) ip_param=$OPTARG
	   use_ip_param="true"
	;;
	o) output_dir=$OPTARG;;
	p) port_start=$OPTARG;;
	e) eth_path=$OPTARG;;
	c) CLIENTCERT_PWD=$OPTARG;;
	k) KEYSTORE_PWD=$OPTARG;;
	s) statedb_type=amop;;
	t) CertConfig=$OPTARG;;
	z) make_tar="yes";;
	h) help;;
	esac
done

[ -z $use_ip_param ] && help 'ERROR: Please set -l or -f option.'
if [ "${use_ip_param}" = "true" ];then
    ip_array=(${ip_param//,/ })
elif [ "${use_ip_param}" = "false" ];then
	n=0
	while read line;do
		ip_array[n]=$line
		((++n))
	done < $ip_file
else 
    help 
fi

if [ -z ${eth_path} ];then
    eth_path=${output_dir}/fisco-bcos
	Download=true
fi

[ -d "$output_dir" ] || mkdir -p "$output_dir"

if [ "${Download}" = "true" ];then
	echo "Downloading fisco-bcos binary..." 
	curl -Lo ${eth_path} ${Download_Link}
  chmod a+x ${eth_path}
fi

#准备CA密钥
if [ ! -e "$ca_file" ]; then
	echo "Generating CA key..."
	openssl ecparam -out $output_dir/ca.param -name secp256k1 || fail_message "openssl error!"#准备密钥参数
	while :
	do
		openssl genpkey -paramfile $output_dir/ca.param -out $output_dir/ca.key #生成secp256k1算法的CA密钥
		privateKey=`openssl ec -in "$output_dir/ca.key" -text 2> /dev/null| sed -n '3,5p' | sed 's/://g'| tr "\n" " "|sed 's/ //g'`
		len=${#privateKey}
		head2=${privateKey:0:2}
		if [ "64" == "${len}" ] && [ "00" != "$head2" ];then
			break;
		fi
	done
	openssl req -new -x509 -days 3650 -key $output_dir/ca.key -out $output_dir/ca.crt -batch #生成CA证书, 此处需要输入一些CA信息, 可按需要输入, 或回车跳过
	ca_file="$output_dir/ca.key"
else
	#生成CA证书, 此处需要输入一些CA信息, 可按需要输入, 或回车跳过
	openssl req -new -x509 -days 3650 -key "$ca_file" -out "$output_dir/ca.crt" -batch || fail_message "openssl error!" 
fi

#准备密钥参数
openssl ecparam -out "$output_dir/node.param" -name secp256k1

#准备证书配置
if [ -z ${CertConfig} ] || [ ! -e ${CertConfig} ];then
CertConfig="$output_dir/cert.cnf"
cat  << EOF > "$CertConfig"
	[ca]
	default_ca=default_ca
	[default_ca]
	default_days = 3650
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
EOF
fi
echo "Generating node key ..."
nodeid_list=""
ip_list=""
#生成每个节点的密钥和ip列表
index=0
for line in ${ip_array[*]};do
	ip=${line%:*}
	num=${line#*:}
	[ "$num" = "$ip" -o -z "${num}" ] && num=${node_num}
	for ((i=0;i<num;++i));do
		# echo "Generating IP:${ip} ID:${index} key files..."
		node_dir="$output_dir/node_${ip}_${index}"
		[ -d "$node_dir" ] && echo "$node_dir exist! Please delete!" && exit 1
		mkdir -p $node_dir/data/
		mkdir -p $node_dir/sdk/
		while :
		do
			openssl genpkey -paramfile "$output_dir/node.param" -out "$node_dir/data/node.key"
			privateKey=`openssl ec -in "$node_dir/data/node.key" -text 2> /dev/null| sed -n '3,5p' | sed 's/://g'| tr "\n" " "|sed 's/ //g'`
			len=${#privateKey}
			head2=${privateKey:0:2}
			if [ "64" == "${len}" ] && [ "00" != "$head2" ];then
				break;
			fi
		done
		openssl req -new -key "$node_dir/data/node.key" -config "${CertConfig}" -out "$node_dir/data/node.csr" -batch
		openssl x509 -req -in "$node_dir/data/node.csr" -CAkey "$ca_file" -CA "$output_dir/ca.crt" -out "$node_dir/data/node.crt" -CAcreateserial -extensions v3_req -extfile "${CertConfig}" &> /dev/null
    	openssl ec -in "$node_dir/data/node.key" -text 2> /dev/null | perl -ne '$. > 6 and $. < 12 and ~s/[\n:\s]//g and print' | perl -ne 'print substr($_, 2)."\n"' > "$node_dir/data/node.nodeid"

		echo ${CLIENTCERT_PWD} > $node_dir/sdk/pwd.conf
		openssl pkcs12 -export -name client -in "$node_dir/data/node.crt" -inkey "$node_dir/data/node.key" -out "$node_dir/data/keystore.p12" -password file:$node_dir/sdk/pwd.conf
		keytool -importkeystore -destkeystore "$node_dir/sdk/client.keystore" -srckeystore "$node_dir/data/keystore.p12" -srcstoretype pkcs12 -alias client -srcstorepass ${CLIENTCERT_PWD} -deststorepass ${KEYSTORE_PWD} >> /dev/null 2>&1 || fail_message "java keytool error!" 
        rm $node_dir/sdk/pwd.conf
		nodeid=$(openssl ec -in "$node_dir/data/node.key" -text 2> /dev/null | perl -ne '$. > 6 and $. < 12 and ~s/[\n:\s]//g and print' | perl -ne 'print substr($_, 2)."\n"')
		nodeid_list=$"${nodeid_list}miner.${index}=$nodeid
	"
		ip_list=$"${ip_list}node.${index}="${ip}:$(( port_start + index * 4 ))"
	"
		((++index))
	done
done 
rm "$output_dir/node.param"
echo "#!/bin/bash" > "$output_dir/start_all.sh"
echo "PPath=\`pwd\`" >> "$output_dir/start_all.sh"
echo "Generating node configuration..."
#生成每个节点的配置、创世块文件和启动脚本
index=0
for line in ${ip_array[*]};do
	ip=${line%:*}
	num=${line#*:}
	[ "$num" = "$ip" -o -z "${num}" ] && num=${node_num}
	for ((i=0;i<num;++i));do
		echo "Generating IP:${ip} ID:${index} files..."
		node_dir="$output_dir/node_${ip}_${index}"
	cat << EOF > "$node_dir/config.conf"
[rpc]
	listen_ip=127.0.0.1
	listen_port=$(( port_start + 1 + index * 4 ))
	http_listen_port=$(( port_start + 2 + index * 4 ))
	console_port=$(( port_start + 3 + index * 4 ))

[p2p]
	listen_ip=0.0.0.0
	listen_port=$(( port_start + index * 4 ))
	$ip_list
[pbft]
	$nodeid_list
[common]
	;\${DATAPATH} == data_path
	data_path=data/
	log_config=log.conf
[secure]
	key=\${DATAPATH}/node.key
	cert=\${DATAPATH}/node.crt
	ca_cert=\${DATAPATH}/ca.crt
[statedb]
	;type : leveldb/amop
	type=${statedb_type}
	path=\${DATAPATH}/statedb
	topic=DB
EOF

	cat << EOF > "$node_dir/log.conf"
* GLOBAL:
	ENABLED                 =   true
	TO_FILE                 =   true
	TO_STANDARD_OUTPUT      =   false
	FORMAT                  =   "%level|%datetime{%Y-%M-%d %H:%m:%s:%g}|%file:%line|%msg"
	FILENAME                =   "log/log_%datetime{%Y%M%d%H}.log"
	MILLISECONDS_WIDTH      =   3
	PERFORMANCE_TRACKING    =   false
	MAX_LOG_FILE_SIZE       =   209715200 ## 200MB - Comment starts with two hashes (##)
	LOG_FLUSH_THRESHOLD     =   100  ## Flush after every 100 logs

* FATAL:  
    ENABLED                 =   false
    TO_FILE                 =   false
* ERROR:  
    ENABLED                 =   true
    TO_FILE                 =   false
* WARNING: 
     ENABLED                =   true
     TO_FILE                =   false
* INFO: 
    ENABLED                 =   true
    TO_FILE                 =   false 
* DEBUG:  
    ENABLED                 =   false
    TO_FILE                 =   false
* TRACE:  
    ENABLED                 =   false
    TO_FILE                 =   false
* VERBOSE:  
    ENABLED                 =   false
    TO_FILE                 =   false
EOF

	cat << EOF > "$node_dir/genesis.json"
{
	"nonce": "0x0",
	"difficulty": "0x0",
	"mixhash": "0x0",
	"coinbase": "0x0",
	"timestamp": "0x0",
	"parentHash": "0x0",
	"extraData": "0x0",
	"gasLimit": "0x0",
	"god":"0x0",
	"alloc": {},
	"initMinerNodes":[]
}
EOF

	cat << EOF > "$node_dir/start.sh"
#!/bin/bash
SHELL_FOLDER=\$(cd "\$(dirname "\$0")";pwd)
fisco_bcos=\${SHELL_FOLDER}/fisco-bcos
cd \${SHELL_FOLDER}
nohup setsid \$fisco_bcos --config config.conf --genesis genesis.json&
EOF

	cat << EOF > "$node_dir/stop.sh"
#!/bin/bash
SHELL_FOLDER=\$(cd "\$(dirname "\$0")";pwd)
fisco_bcos=\${SHELL_FOLDER}/fisco-bcos
weth_pid=\`ps aux|grep "\${fisco_bcos}"|grep -v grep|awk '{print \$2}'\`
kill \${weth_pid}
EOF

		chmod +x "$node_dir/start.sh"
		chmod +x "$node_dir/stop.sh"
		cp "$output_dir/ca.crt" "$node_dir/data/"
		cp "$output_dir/ca.crt" "$node_dir/sdk/"
		cp "$eth_path" "$node_dir/fisco-bcos"
		echo "cd \${PPath}/node_${ip}_${index} && bash start.sh" >> "$output_dir/start_all.sh"
		((++index))
		[ -n "$make_tar" ] && tar zcf "${node_dir}.tar.gz" "$node_dir"
	done
	chmod +x "$output_dir/start_all.sh"
done 

echo "========================================"
echo "FISCO-BCOS Path : $eth_path"
[ ! -z $ip_file ] && echo "IP List File    : $ip_file"
[ ! -z $ip_param ] && echo "IP List Param   : $ip_param"
echo "Start Port      : $port_start"
echo "Output Dir      : $output_dir"
echo "CA Key Path     : $ca_file"
echo "========================================"
echo "All completed. All files in $output_dir"
