#!/bin/bash

ca_file= #CA私钥文件
node_num=1 #节点数量
ip_file=
output_dir=./output/ #输出目录
port_start=30300 #起始端口
eth_path=
make_tar=

help() {
	echo $1
	echo
	cat << EOF
参数:
	-f <IP列表文件> IP列表，必填
	-e <FISCO-BCOS程序路径> FISCO-BCOS程序路径, 必填
	-n <节点数量> 每个IP节点数量, 默认为1
	-a <CA密钥文件> CA密钥文件, 选填, 默认为自动生成
	-o <输出目录> 输出目录, 选填, 默认为./output/
	-p <起始端口> 起始端口, 选填, 默认为30300
	-z 生成压缩包
	-h 查看本帮助
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
	z) make_tar="yes";;
	h) help;;
	esac
done

[ -z $ip_file ] && help '错误: 请指定节点列表文件'
[ -z $eth_path ] && help '错误: 请指定FISCO-BCOS程序路径'

echo "FISCO-BCOS程序: $eth_path"
echo "IP列表文件:  $ip_file"
echo "CA密钥: $ca_file"
echo "每IP节点数量: $node_num"
echo "输出路径: $output_dir"
echo "端口起始: $port_start"

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

echo "生成密钥证书..."
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
echo "生成节点配置..."
#生成每个节点的配置、创世块文件和启动脚本
index=0
while read line;do
	for ((i=0;i<node_num;++i));do
		echo "生成ip:${line} id:${index}的节点配置..."
		node_dir="$output_dir/node_${line}_${index}"
	cat << EOF > "$node_dir/config${i}.conf"
	[common]
		data_path=data/
		log_config=log.conf
		ext_header=0
	[secure]
		key=data/node.key
		cert=data/node.crt
		ca_cert=data/ca.crt
		;ca_path=
	
	[statedb]
		type=leveldb
		path=data/statedb
	
	[pbft]
		block_interval=1000
		
		$nodeid_list
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
	
	nohup setsid ../fisco-bcos --config config${i}.conf --genesis genesis.json&
EOF

	cat << EOF > "$node_dir/stop.sh"
	#!/bin/bash
	
	nohup setsid ../fisco-bcos --config config${i}.conf --genesis genesis.json&
EOF
	
		chmod +x "$node_dir/start.sh"
	
		cp "$output_dir/ca.crt" "$node_dir/data/"
		cp "$eth_path" "$node_dir/eth"
		echo "cd \${PPath}/node_${line}_${index}" >> "$output_dir/start_all.sh"
		echo "nohup setsid ./fisco-bcos --config config${i}.conf --genesis genesis.json &" >> "$output_dir/start_all.sh"
		((++index))
		[ -n "$make_tar" ] && tar zcf "${node_dir}.tar.gz" "$node_dir"
	done
done < $ip_file

echo "全部完成"
