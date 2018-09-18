#!/bin/bash

ca_file= #CA私钥文件
node_num=1 #节点数量
ip_file=
output_dir=./output/ #输出目录
port_start=30300 #起始端口
statedb_type=leveldb #存储
eth_path=
make_tar=

help() {
	echo $1
	echo
	cat << EOF
Usage:
	-f <IP list file> [Required]
	-e <FISCO-BCOS program path> [Required]
	-n <Nodes per IP> Default 1
	-a <CA Key>       Default Generate a new CA
	-o <Output Dir>   Default ./output/
	-p <Start Port>   Default 30300
	-s <StateDB type> Default leveldb. if set -s, use amop
	-z Generate tar packet
	-h Help
EOF

exit
}

while getopts "a:n:o:p:e:f:hz" option;do
	case $option in
	a) ca_file=$OPTARG;;
	n) node_num=$OPTARG;;
	f) ip_file=$OPTARG;;
	o) output_dir=$OPTARG;;
	p) port_start=$OPTARG;;
	e) eth_path=$OPTARG;;
	s) statedb_type="amop";;
	z) make_tar="yes";;
	h) help;;
	esac
done

[ -z $ip_file ] && help 'ERROR: Please specify the IP list file.'
[ -z $eth_path ] && help 'ERROR: Please specify the fisco-bcos path.'

echo "FISCO-BCOS Path: $eth_path"
echo "IP List File:    $ip_file"
echo "CA Key:          $ca_file"
echo "Nodes per IP:    $node_num"
echo "Output Dir:      $output_dir"
echo "Start Port:      $port_start"

[ -d "$output_dir" ] || mkdir -p "$output_dir"

#准备CA密钥
if [ ! -e "$ca_file" ]; then
	openssl ecparam -out $output_dir/ca.param -name secp256k1 #准备密钥参数
	openssl genpkey -paramfile $output_dir/ca.param -out $output_dir/ca.key #生成secp256k1算法的CA密钥
	openssl req -new -x509 -days 3650 -key $output_dir/ca.key -out $output_dir/ca.crt -batch #生成CA证书, 此处需要输入一些CA信息, 可按需要输入, 或回车跳过
	ca_file="$output_dir/ca.key"
else
	openssl req -new -x509 -days 3650 -key "$ca_file" -out "$output_dir/ca.crt" -batch #生成CA证书, 此处需要输入一些CA信息, 可按需要输入, 或回车跳过
fi

#准备密钥参数
openssl ecparam -out "$output_dir/node.param" -name secp256k1

#准备证书配置
cat  << EOF > "$output_dir/cerf.cnf"
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
EOF

echo "Generating node key certificate..."
nodeid_list=""
ip_list=""
#生成每个节点的密钥和ip列表
index=0
while read line;do
	for ((i=0;i<node_num;++i));do
		node_dir="$output_dir/node_${line}_${index}/"
		mkdir -p "$node_dir/data"
		
		openssl genpkey -paramfile "$output_dir/node.param" -out "$node_dir/data/node.key"
		openssl req -new -key "$node_dir/data/node.key" -config "$output_dir/cerf.cnf" -out "$node_dir/data/node.csr" -batch
		openssl x509 -req -in "$node_dir/data/node.csr" -CAkey "$ca_file" -CA "$output_dir/ca.crt" -out "$node_dir/data/node.crt" -CAcreateserial -extensions v3_req -extfile "$output_dir/cerf.cnf" &> /dev/null
		
		#openssl pkcs12 -export -name client -in "$node_dir/data/node.crt" -inkey "$node_dir/data/node.key" -out "$node_dir/keystore.p12"
		#keytool -importkeystore -destkeystore "$node_dir/client.keystore" -srckeystore "$node_dir/keystore.p12" -srcstoretype pkcs12 -alias client
		
		nodeid=$(openssl ec -in "$node_dir/data/node.key" -text 2> /dev/null | perl -ne '$. > 6 and $. < 12 and ~s/[\n:\s]//g and print' | perl -ne 'print substr($_, 2)."\n"')
		nodeid_list=$"${nodeid_list}miner.${index}=$nodeid
		"
		ip_list=$"${ip_list}node.${index}="${line}:$(( port_start + index * 4 ))"
		"
		((++index))
	done
done < $ip_file

echo "#!/bin/bash" > "$output_dir/start_all.sh"
echo "PPath=\`pwd\`" >> "$output_dir/start_all.sh"
echo "Generating node configuration..."
#生成每个节点的配置、创世块文件和启动脚本
index=0
while read line;do
	for ((i=0;i<node_num;++i));do
		echo "Generating IP:${line} ID:${index} config files..."
		node_dir="$output_dir/node_${line}_${index}"
	cat << EOF > "$node_dir/config${i}.conf"
[rpc]
	listen_ip=0.0.0.0
	listen_port=$(( port_start + 1 + index * 4 ))
	http_listen_port=$(( port_start + 2 + index * 4 ))
	console_port=$(( port_start + 3 + index * 4 ))

[p2p]
	listen_ip=0.0.0.0
	listen_port=$(( port_start + index * 4 ))
	idle_connections=100
	reconnect_interval=60

	$ip_list
[pbft]
	block_interval=1000

	$nodeid_list
[common]
	;\${DATAPATH} == data_path
	data_path=data/
	log_config=log.conf
	ext_header=0
[secure]
	key=\${DATAPATH}/node.key
	cert=\${DATAPATH}/node.crt
	ca_cert=\${DATAPATH}/ca.crt
	ca_path=
[statedb]
	;type : leveldb/amop
	type=${statedb_type}
	path=\${DATAPATH}/statedb
	retryInterval=1
	maxRetry=0
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
	PERFORMANCE_TRACKING    =   true
	MAX_LOG_FILE_SIZE       =   209715200 ## 200MB - Comment starts with two hashes (##)
	LOG_FLUSH_THRESHOLD     =   100  ## Flush after every 100 logs

* TRACE:
	ENABLED                 =   true
	FILENAME                =   "log/trace_log_%datetime{%Y%M%d%H}.log"

* DEBUG:
	ENABLED                 =   true
	FILENAME                =   "log/debug_log_%datetime{%Y%M%d%H}.log"

* FATAL:
	ENABLED                 =   true
	FILENAME                =   "log/fatal_log_%datetime{%Y%M%d%H}.log"

* ERROR:
	ENABLED                 =   true
	FILENAME                =   "log/error_log_%datetime{%Y%M%d%H}.log"

* WARNING:
		ENABLED                 =   true
		FILENAME                =   "log/warn_log_%datetime{%Y%M%d%H}.log"

* INFO:
	ENABLED                 =   true
	FILENAME                =   "log/info_log_%datetime{%Y%M%d%H}.log"

* VERBOSE:
	ENABLED                 =   true
	FILENAME                =   "log/verbose_log_%datetime{%Y%M%d%H}.log"
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
nohup setsid ./fisco-bcos --config config${i}.conf --genesis genesis.json&
EOF

	cat << EOF > "$node_dir/stop.sh"
#!/bin/bash
weth_pid=\`ps aux|grep "config config${i}.conf"|grep -v grep|awk '{print \$2}'\`
kill -9 \${weth_pid}
EOF
	
		chmod +x "$node_dir/start.sh"
	
		cp "$output_dir/ca.crt" "$node_dir/data/"
		cp "$eth_path" "$node_dir/fisco-bcos"
		echo "cd \${PPath}/node_${line}_${index} && ./start.sh" >> "$output_dir/start_all.sh"
		((++index))
		[ -n "$make_tar" ] && tar zcf "${node_dir}.tar.gz" "$node_dir"
	done
done < $ip_file

echo "All completed."
