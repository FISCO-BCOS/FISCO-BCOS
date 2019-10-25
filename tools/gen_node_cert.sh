#!/bin/bash

set -e

# SHELL_FOLDER=$(cd $(dirname $0);pwd)
current_dir=$(pwd)
key_path=""
output_dir="newNode"
logfile="build.log"
conf_path="conf"

LOG_WARN()
{
    local content=${1}
    echo -e "\033[31m[WARN] ${content}\033[0m"
}

LOG_INFO()
{
    local content=${1}
    echo -e "\033[32m[INFO] ${content}\033[0m"
}

help() {
    echo $1
    cat << EOF
Usage:
    -c <cert path>              [Required] cert key path 
    -o <Output Dir>             Default ${output_dir}
    -h Help
e.g 
    $0 -c nodes/cert/agency -o newNode_1
EOF

exit 0
}

parse_params()
{
while getopts "c:o:h" option;do
    case $option in
    c) [ ! -z $OPTARG ] && key_path=$OPTARG
    ;;
    o) [ ! -z $OPTARG ] && output_dir=$OPTARG
    ;;
    h) help;;
    esac
done
}

print_result()
{
echo "=============================================================="
LOG_INFO "Cert Path   : $key_path"
LOG_INFO "Output Dir  : $output_dir"
echo "=============================================================="
LOG_INFO "All completed. Files in $output_dir"
}

getname() {
    local name="$1"
    if [ -z "$name" ]; then
        return 0
    fi
    [[ "$name" =~ ^.*/$ ]] && {
        name="${name%/*}"
    }
    name="${name##*/}"
    echo "$name"
}

check_name() {
    local name="$1"
    local value="$2"
    [[ "$value" =~ ^[a-zA-Z0-9._-]+$ ]] || {
        echo "$name name [$value] invalid, it should match regex: ^[a-zA-Z0-9._-]+\$"
        exit $EXIT_CODE
    }
}

file_must_exists() {
    if [ ! -f "$1" ]; then
        echo "$1 file does not exist, please check!"
        exit $EXIT_CODE
    fi
}

dir_must_exists() {
    if [ ! -d "$1" ]; then
        echo "$1 DIR does not exist, please check!"
        exit $EXIT_CODE
    fi
}

dir_must_not_exists() {
    if [ -e "$1" ]; then
        echo "$1 DIR exists, please clean old DIR!"
        exit $EXIT_CODE
    fi
}

gen_cert_secp256k1() {
    capath="$1"
    certpath="$2"
    name="$3"
    type="$4"
    openssl ecparam -out $certpath/${type}.param -name secp256k1
    openssl genpkey -paramfile $certpath/${type}.param -out $certpath/${type}.key
    openssl pkey -in $certpath/${type}.key -pubout -out $certpath/${type}.pubkey
    openssl req -new -sha256 -subj "/CN=${name}/O=fisco-bcos/OU=${type}" -key $certpath/${type}.key -config $capath/cert.cnf -out $certpath/${type}.csr
    openssl x509 -req -days 3650 -sha256 -in $certpath/${type}.csr -CAkey $capath/agency.key -CA $capath/agency.crt\
        -force_pubkey $certpath/${type}.pubkey -out $certpath/${type}.crt -CAcreateserial -extensions v3_req -extfile $capath/cert.cnf
    openssl ec -in $certpath/${type}.key -outform DER | tail -c +8 | head -c 32 | xxd -p -c 32 | cat >$certpath/${type}.private
    rm -f $certpath/${type}.csr
}

gen_node_cert() {
    if [ "" == "$(openssl ecparam -list_curves 2>&1 | grep secp256k1)" ]; then
        echo "openssl don't support secp256k1, please upgrade openssl!"
        exit -1
    fi
    agpath="$2"
    agency=$(getname "$agpath")
    ndpath="$3"
    node=$(getname "$ndpath")
    dir_must_exists "$agpath"
    file_must_exists "$agpath/agency.key"
    check_name agency "$agency"
    dir_must_not_exists "$ndpath"	
    check_name node "$node"
    mkdir -p $ndpath
    gen_cert_secp256k1 "$agpath" "$ndpath" "$node" node
    #nodeid is pubkey
    openssl ec -in $ndpath/node.key -text -noout | sed -n '7,11p' | tr -d ": \n" | awk '{print substr($0,3);}' | cat >$ndpath/node.nodeid
    cp $agpath/ca.crt $agpath/agency.crt $ndpath
}

generate_script_template()
{
    local filepath=$1
    cat << EOF > "${filepath}"
#!/bin/bash
SHELL_FOLDER=\$(cd \$(dirname \$0);pwd)

EOF
    chmod +x ${filepath}
}

generate_node_scripts()
{
    local output=$1
    generate_script_template "$output/start.sh"
    cat << EOF >> "$output/start.sh"
fisco_bcos=\${SHELL_FOLDER}/../fisco-bcos
cd \${SHELL_FOLDER}
node=\$(basename \${SHELL_FOLDER})
node_pid=\`ps aux|grep "\${fisco_bcos}"|grep -v grep|awk '{print \$2}'\`
if [ ! -z \${node_pid} ];then
    echo " \${node} is running, pid is \$node_pid."
    exit 0
else 
    nohup \${fisco_bcos} -c config.ini 2>>nohup.out &
    sleep 0.5
fi
node_pid=\`ps aux|grep "\${fisco_bcos}"|grep -v grep|awk '{print \$2}'\`
if [ ! -z \${node_pid} ];then
    echo " \${node} start successfully"
else
    echo " \${node} start failed"
    cat nohup.out
fi
EOF
    generate_script_template "$output/stop.sh"
    cat << EOF >> "$output/stop.sh"
fisco_bcos=\${SHELL_FOLDER}/../fisco-bcos
node=\$(basename \${SHELL_FOLDER})
node_pid=\`ps aux|grep "\${fisco_bcos}"|grep -v grep|awk '{print \$2}'\`
try_times=5
i=0
while [ \$i -lt \${try_times} ]
do
    if [ -z \${node_pid} ];then
        echo " \${node} isn't running."
        exit 0
    fi
    [ ! -z \${node_pid} ] && kill \${node_pid}
    sleep 0.4
    node_pid=\`ps aux|grep "\${fisco_bcos}"|grep -v grep|awk '{print \$2}'\`
    if [ -z \${node_pid} ];then
        echo " stop \${node} success."
        exit 0
    fi
    ((i=i+1))
done
EOF
}

main()
{    
    if [ ! -z "$(openssl version | grep reSSL)" ];then
        export PATH="/usr/local/opt/openssl/bin:$PATH"
    fi

    while :
    do
        gen_node_cert "" ${key_path} ${output_dir} > ${logfile} 2>&1
        cd ${output_dir}
        mkdir -p ${conf_path}/
        rm node.param node.pubkey
        mv *.* ${conf_path}/
        cd ${current_dir}
        #private key should not start with 00
        privateKey=$(openssl ec -in "${output_dir}/${conf_path}/node.key" -text 2> /dev/null| sed -n '3,5p' | sed 's/://g'| tr "\n" " "|sed 's/ //g')
        len=${#privateKey}
        head2=${privateKey:0:2}
        if [ "64" != "${len}" ] || [ "00" == "$head2" ];then
            rm -rf ${output_dir}
            continue;
        fi
        break;
    done
    # generate_node_scripts "${output_dir}"
    cat ${key_path}/agency.crt >> ${output_dir}/${conf_path}/node.crt
    cat ${key_path}/ca.crt >> ${output_dir}/${conf_path}/node.crt
    rm ${logfile}
}

parse_params $@
main
print_result
