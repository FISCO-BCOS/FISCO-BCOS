#!/bin/bash

set -e

# SHELL_FOLDER=$(cd $(dirname $0);pwd)
current_dir=$(pwd)

gmkey_path=""
output_dir="newNode"
logfile="build.log"


conf_path="conf"
gm_conf_path="gmconf"
days=36500 # 100 years
SWSSL_CMD="${HOME}"/.fisco/swssl/bin/swssl
sdk_cert=
agency_key_type="externalKey"
agency_key_index=
agency_key_path=
node_key_type="externalKey"
node_key_index=
hsm_config_array=
output_dir="newNode"

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
    cat << EOF
Usage:
    -t <agency key type>        externalKey or internalKey, default externalKey,internalKey means the agency is using hardware secure module internal key. 
    -g <gm agency cert path>    [Required] agency key path, if agency use external key
    -a <agency key index>       [Option] agency key index, if agency use internal key
    -k <key type>               externalKey or internalKey, internalKey means the node/sdk is using hardware secure module internal key. 
    -i <key index>              [Option] node/sdk's two key index, e.g. 31,32 
    -s <is sdk>                 If set -s, generate certificate for sdk
    -o <Output Dir>             Default ${output_dir}
    -X <Certificate expiration time>    Default 36500 days
    -H <HSM module config>      [Optional] if use remote HSM, and internal key, please configure the ip,port,password of HSM
    -h Help
e.g
    $0 -g nodes/gmcert/agency -o newNode_GM      #generate gm node certificate, both agency and node use external key.
    $0 -g nodes/gmcert/agency -t internalKey -a 21 -o newNode_GM -k internalKey -i 31,32 -H 192.168.10.12,10000,XXXXX          #generate gm node certificate, both agency and node use internal key.
    $0 -g nodes/gmcert/agency 21 -o newNode_GM -k internalKey -i 31,32        #generate gm node certificate, agency use external key and node use internal key.
    $0 -g nodes/gmcert/agency -t internalKey -a 21 -o newSDK_GM -H 192.168.10.12,10000,XXXXX -s      #generate gm sdk certificate, agency use internal key, sdk use external key.
EOF
exit 0
}
parse_params()
{
while getopts "t:g:a:k:i:o:H:X:hs" option;do
    case $option in
    t) agency_key_type=$OPTARG;;
    g) agency_key_path=$OPTARG
        check_and_install_swssl
    ;;
    a) agency_key_index=$OPTARG;;
    k) node_key_type=$OPTARG;;
    i) node_key_index=(${OPTARG//,/ });;
    o) output_dir=$OPTARG;;
    X) days="$OPTARG";;
    H) hsm_config_array=(${OPTARG//,/ });;
    s) sdk_cert="true";;
    h) help;;
    esac
done
}

check_and_install_swssl(){
if [ ! -f "${SWSSL_CMD}" ];then
    local swssl_link_perfix="https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/FISCO-BCOS/deps/swssl"
    if [[ "$(uname -p)" == "aarch64" ]];then
        curl -#LO "${swssl_link_perfix}/swssl-20011-linaro-6.5.0-aarch64-linux-gnu.tgz"
        LOG_INFO "Downloading tassl binary from ${swssl_link_perfix}/swssl-20011-linaro-6.5.0-aarch64-linux-gnu.tgz"
        mv swssl-20011-linaro-6.5.0-aarch64-linux-gnu.tgz swssl.tgz
    elif [[ "$(uname -p)" == "x86_64" ]];then
        curl -#LO "${swssl_link_perfix}/swssl-20011-3.10.0-1062.el7.x86_64.tgz"
        LOG_INFO "Downloading tassl binary from ${swssl_link_perfix}/swssl-20011-3.10.0-1062.el7.x86_64.tgz"
        mv swssl-20011-3.10.0-1062.el7.x86_64.tgz swssl.tgz
    else
        LOG_WARN "Unsupported platform"
        exit 1
    fi
    tar zxvf swssl.tgz && rm swssl.tgz
    chmod u+x swssl
    mkdir -p "${HOME}"/.fisco
    mv swssl "${HOME}"/.fisco/
fi
export OPENSSL_CONF="${HOME}"/.fisco/swssl/ssl/swssl.cnf
export LD_LIBRARY_PATH="${HOME}"/.fisco/swssl/lib
echo $LD_LIBRARY_PATH
if [[ -n ${hsm_config_array} ]];then
    generate_swssl_ini "${HOME}/.fisco/swssl/bin/swsds.ini"
    generate_swssl_ini "swsds.ini"
fi
}

print_result()
{
echo "=============================================================="
LOG_INFO "Agency Cert Path   : $agency_key_path"
[ ! -z "${guomi_mode}" ] && LOG_INFO "GM Cert Path: $gmkey_path"
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

exit_with_clean()
{
    local content=${1}
    echo -e "\033[31m[ERROR] ${content}\033[0m"
    if [ -d "${output_dir}" ];then
        rm -rf "${output_dir}"
    fi
    exit 1
}

file_must_exists() {
    if [ ! -f "$1" ]; then
        exit_with_clean "$1 file does not exist, please check!"
    fi
}

dir_must_exists() {
    if [ ! -d "$1" ]; then
        exit_with_clean "$1 DIR does not exist, please check!"
    fi
}

dir_must_not_exists() {
    if [ -e "$1" ]; then
        LOG_WARN "$1 DIR exists, please clean old DIR!"
        exit 1
    fi
}


gen_node_cert_with_extensions_gm() {
    capath="$1"
    certpath="$2"
    name="$3"
    type="$4"
    extensions="$5"
    keyType="$6"
    keyIndex="$7"
    # add key index support
    if [ "${keyType}" == "internalKey" ];then
        generate_swssl_ini "swsds.ini"
        $SWSSL_CMD req -engine sdf -batch -sm3 -new -subj "/CN=$name/O=fisco-bcos/OU=${type}" -key "sm2_${keyIndex}" -keyform engine -config "$capath/gmcert.cnf" -out "$certpath/gm${type}.csr" 2> /dev/null
        rm swsds.ini
    else
        $SWSSL_CMD genpkey -paramfile "$capath/gmsm2.param" -out "$certpath/gm${type}.key" 2> /dev/null
        $SWSSL_CMD req -new -subj "/CN=$name/O=fisco-bcos/OU=${type}" -key "$certpath/gm${type}.key" -config "$capath/gmcert.cnf" -out "$certpath/gm${type}.csr" 2> /dev/null
    fi
    if [ -n "${no_agency}" ];then
        echo "not use $(basename $capath) to sign $(basename $certpath) ${type}" >>"${logfile}"
        $SWSSL_CMD x509 -sm3 -req -CA "$capath/../gmca.crt" -CAkey "$capath/../gmca.key" -days "${days}" -CAcreateserial -in "$certpath/gm${type}.csr" -out "$certpath/gm${type}.crt" -extfile "$capath/gmcert.cnf" -extensions "$extensions" 2> /dev/null
    else
        $SWSSL_CMD x509 -sm3 -req -CA "$capath/gmagency.crt" -CAkey "$capath/gmagency.key" -days "${days}" -CAcreateserial -in "$certpath/gm${type}.csr" -out "$certpath/gm${type}.crt" -extfile "$capath/gmcert.cnf" -extensions "$extensions" 2> /dev/null
    fi
    rm -f $certpath/gm${type}.csr
}

gen_node_cert_with_index_and_extensions_gm(){
    capath="$1"
    final=${capath: -1}
    if [ "${final}" == "/" ];then
        capath=${capath%?}
    fi
    agKeyId="$2"
    certpath="$3"
    name="$4"
    type="$5"
    extensions="$6"
    keyType="$7"
    keyIndex="$8"
    # add key index support
    if [ "${keyType}" == "internalKey" ];then
        generate_swssl_ini "swsds.ini"
        touch ${HOME}/.rnd
        $SWSSL_CMD req -engine sdf -batch -sm3 -new -subj "/CN=$name/O=fisco-bcos/OU=${type}" -key "sm2_${keyIndex}" -keyform engine -config "$capath/gmcert.cnf" -out "$certpath/gm${type}.csr"  2> /dev/null
        rm swsds.ini
    else
        $SWSSL_CMD genpkey -paramfile "$capath/gmsm2.param" -out "$certpath/gm${type}.key" 2> /dev/null
        $SWSSL_CMD req -new -subj "/CN=$name/O=fisco-bcos/OU=${type}" -key "$certpath/gm${type}.key" -config "$capath/gmcert.cnf" -out "$certpath/gm${type}.csr" 2> /dev/null
    fi
    if [ -n "${no_agency}" ];then
        echo "not use $(basename $capath) to sign $(basename $certpath) ${type}" >>"${logfile}"
        $SWSSL_CMD x509 -sm3 -req -CA "$capath/../gmca.crt" -CAkey "$capath/../gmca.key" -days "${days}" -CAcreateserial -in "$certpath/gm${type}.csr" -out "$certpath/gm${type}.crt" -extfile "$capath/gmcert.cnf" -extensions "$extensions" 2> /dev/null
    else
        generate_swssl_ini "swsds.ini"
        #$SWSSL_CMD x509 -sm3 -req -CA "$capath/gmagency.crt" -CAkey "$capath/gmagency.key" -days "${days}" -CAcreateserial -in "$certpath/gm${type}.csr" -out "$certpath/gm${type}.crt" -extfile "$capath/gmcert.cnf" -extensions "$extensions" 2> /dev/null
        $SWSSL_CMD x509 -engine sdf -req -CA "$capath/gmagency.crt" -CAkey "sm2_${agKeyId}" -CAkeyform engine -days "${days}" -CAcreateserial -in "$certpath/gm${type}.csr" -out "$certpath/gm${type}.crt" -extfile "$capath/gmcert.cnf" -extensions "$extensions" 2> /dev/null
        rm swsds.ini
    fi
    rm -f $certpath/gm${type}.csr
}

gen_node_cert_gm() {
    agpath="${1}"
    agency=$(basename "$agpath")
    ndpath="${2}"
    node=$(basename "$ndpath")
    dir_must_exists "$agpath"
    #file_must_exists "$agpath/gmagency.key"
    check_name agency "$agency"
    mkdir -p $ndpath
    dir_must_exists "$ndpath"
    check_name node "$node"
    mkdir -p $ndpath

    final=${agpath: -1}
    if [ "${final}" == "/" ];then
        agpath=${agpath%?}
    fi
    
    if [ -n "${no_agency}" ] || [ "${agency_key_type}" != "internalKey" ];then
        gen_node_cert_with_extensions_gm "$agpath" "$ndpath" "$node" node v3_req "${node_key_type}" "${node_key_index[0]}"
        gen_node_cert_with_extensions_gm "$agpath" "$ndpath" "$node" ennode v3enc_req "${node_key_type}" "${node_key_index[1]}"
    else
        gen_node_cert_with_index_and_extensions_gm "$agpath" "${agency_key_index}" "$ndpath" "$node" node v3_req "${node_key_type}" "${node_key_index[0]}"
        gen_node_cert_with_index_and_extensions_gm "$agpath" "${agency_key_index}" "$ndpath" "$node" ennode v3enc_req "${node_key_type}" "${node_key_index[1]}"
    fi
    if [ "${node_key_type}" = "internalKey" ];then
        generate_swssl_ini "swsds.ini"
        # $SWSSL_CMD engine sdf -t -post "GET_SM2_PUB:${sign_key_array[${nodeIndex}]}"  2>&1 
        $SWSSL_CMD engine sdf -t -post "GET_SM2_PUB:${node_key_index[0]}"  2>&1 | sed '9,$d'|  sed '/|/d'| sed 's/$//;s/ *//g;/^$/d' | sed ':a;N;s/\n//g;ta' | sed 's/000100000000000000000000000000000000000000000000000000000000000000000000//g' | sed 's/0000000000000000000000000000000000000000000000000000000000000000//g'| cat > "$ndpath/gmnode.nodeid"
        rm swsds.ini
    else
        $SWSSL_CMD ec -in "$ndpath/gmnode.key" -text -noout 2> /dev/null | sed -n '7,11p' | sed 's/://g' | tr "\n" " " | sed 's/ //g' | awk '{print substr($0,3);}'  | cat > "$ndpath/gmnode.nodeid"
    fi
    #if [ -z "${no_agency}" ];then cat "${agpath}/gmagency.crt" >> "$ndpath/gmnode.crt";fi
    #cat "${agpath}/../gmca.crt" >> "$ndpath/gmnode.crt"
    #nodeid is pubkey
    #serial
    if [ "" != "$($SWSSL_CMD version 2> /dev/null | grep 1.1.1i)" ];
    then
        $SWSSL_CMD x509 -text -in "$ndpath/gmnode.crt" 2> /dev/null | sed -n '5p' |  sed 's/://g' | tr "\n" " " | sed 's/ //g' | sed 's/[a-z]/\u&/g' | cat > "$ndpath/gmnode.serial"
    else
        $SWSSL_CMD x509 -text -in "$ndpath/gmnode.crt" 2> /dev/null | sed -n '4p' |  sed 's/ //g' | sed 's/.*(0x//g' | sed 's/)//g' |sed 's/[a-z]/\u&/g' | cat > "$ndpath/gmnode.serial"
    fi
    cp "$agpath/gmca.crt" "$ndpath"
    mkdir -p "${ndpath}/${gm_conf_path}/"
    mv "${ndpath}"/*.* "${ndpath}/${gm_conf_path}/"
    cat "$agpath/gmagency.crt" >> "${ndpath}/${gm_conf_path}/gmca.crt"
}

generate_swssl_ini()
{
    local output=${1}
    local ip=${hsm_config_array[0]}
    local port=${hsm_config_array[1]}
    local passwd=${hsm_config_array[2]}
    cat << EOF > "${output}"
[ErrorLog]
level=3
logfile=swsds.log
maxsize=10
[HSM1]
ip=${ip} 
port=${port} 
passwd=${passwd}
[Timeout]
connect=30
service=60
[ConnectionPool]
poolsize=2
EOF
    printf "  [%d] p2p:%-5d  channel:%-5d  jsonrpc:%-5d\n" "${node_index}" $(( offset + port_array[0] )) $(( offset + port_array[1] )) $(( offset + port_array[2] )) >>"${logfile}"
}

generate_node_scripts()
{
    local output=$1
    local docker_tag="v${compatibility_version}"
    generate_script_template "$output/start.sh"
    local ps_cmd="\$(ps aux|grep \${fisco_bcos}|grep -v grep|awk '{print \$2}')"
    local start_cmd="OPENSSL_CONF=./swssl.cnf nohup \${fisco_bcos} -c config.ini >>nohup.out 2>&1 &"
    local stop_cmd="kill \${node_pid}"
    local pid="pid"
    local log_cmd="tail -n20  nohup.out"
    local check_success="\$(${log_cmd} | grep running)"
    local fisco_bin_path="../${bcos_bin_name}"
    if [ -n "${deploy_mode}" ];then fisco_bin_path="${bcos_bin_name}"; fi
    if [ -n "${docker_mode}" ];then
        ps_cmd="\$(docker ps |grep \${SHELL_FOLDER//\//} | grep -v grep|awk '{print \$1}')"
        start_cmd="docker run -d --rm --name \${SHELL_FOLDER//\//} -v \${SHELL_FOLDER}:/data --network=host -w=/data fiscoorg/fiscobcos:${docker_tag} -c config.ini"
        stop_cmd="docker kill \${node_pid} 2>/dev/null"
        pid="container id"
        log_cmd="tail -n20 \$(docker inspect --format='{{.LogPath}}' \${SHELL_FOLDER//\//})"
        check_success="success"
    fi
    cat << EOF >> "$output/start.sh"
fisco_bcos=\${SHELL_FOLDER}/${fisco_bin_path}
cd \${SHELL_FOLDER}
node=\$(basename \${SHELL_FOLDER})
node_pid=${ps_cmd}
if [ -n "\${node_pid}" ];then
    echo " \${node} is running, ${pid} is \$node_pid."
    exit 0
else
    ${start_cmd}
    sleep 1.5
fi
try_times=4
i=0
while [ \$i -lt \${try_times} ]
do
    node_pid=${ps_cmd}
    success_flag=${check_success}
    if [[ -n "\${node_pid}" && -n "\${success_flag}" ]];then
        echo -e "\033[32m \${node} start successfully\033[0m"
        exit 0
    fi
    sleep 0.5
    ((i=i+1))
done
echo -e "\033[31m  Exceed waiting time. Please try again to start \${node} \033[0m"
${log_cmd}
exit 1
EOF
    generate_script_template "$output/stop.sh"
    cat << EOF >> "$output/stop.sh"
fisco_bcos=\${SHELL_FOLDER}/${fisco_bin_path}
node=\$(basename \${SHELL_FOLDER})
node_pid=${ps_cmd}
try_times=20
i=0
if [ -z \${node_pid} ];then
    echo " \${node} isn't running."
    exit 0
fi
[ -n "\${node_pid}" ] && ${stop_cmd} > /dev/null
while [ \$i -lt \${try_times} ]
do
    sleep 0.6
    node_pid=${ps_cmd}
    if [ -z \${node_pid} ];then
        echo -e "\033[32m stop \${node} success.\033[0m"
        exit 0
    fi
    ((i=i+1))
done
echo "  Exceed maximum number of retries. Please try again to stop \${node}"
exit 1
EOF
    generate_script_template "$output/scripts/load_new_groups.sh"
    cat << EOF >> "$output/scripts/load_new_groups.sh"
cd \${SHELL_FOLDER}/../
NODE_FOLDER=\$(pwd)
fisco_bcos=\${NODE_FOLDER}/${fisco_bin_path}
node=\$(basename \${NODE_FOLDER})
node_pid=${ps_cmd}
if [ -n "\${node_pid}" ];then
    echo "\${node} is trying to load new groups. Check log for more information."
    DATA_FOLDER=\${NODE_FOLDER}/data
    for dir in \$(ls \${DATA_FOLDER})
    do
        if [[ -d "\${DATA_FOLDER}/\${dir}" ]] && [[ -n "\$(echo \${dir} | grep -E "^group\d+$")" ]]; then
            STATUS_FILE=\${DATA_FOLDER}/\${dir}/.group_status
            if [ ! -f "\${STATUS_FILE}" ]; then
                echo "STOPPED" > \${STATUS_FILE}
            fi
        fi
    done
    touch config.ini.append_group
    exit 0
else
    echo "\${node} is not running, use start.sh to start all group directlly."
fi
EOF
    generate_script_template "$output/scripts/reload_whitelist.sh"
    cat << EOF >> "$output/scripts/reload_whitelist.sh"
check_cal_line()
{
    line=\$1;
    if [[ \$line =~ cal.[0-9]*=[0-9A-Fa-f]{128,128}\$ ]]; then
        echo "true";
    else
        echo "false";
    fi
}

check_cal_lines()
{
    # print Illegal line
    config_file=\$1
    error="false"
    for line in \$(grep -v "^[ ]*[;]" \$config_file | grep "cal\."); do
        if [[ "true" != \$(check_cal_line \$line) ]]; then
            LOG_ERROR "Illigal whitelist line: \$line"
            error="true"
        fi
    done

    if [[ "true" == \$error ]]; then
        LOG_ERROR "[certificate_whitelist] reload error for illigal lines"
        exit 1
    fi
}

check_duplicate_key()
{
    config_file=\$1;
    dup_key=\$(grep -v '^[ ]*[;]' \$config_file |grep "cal\."|awk -F"=" '{print \$1}'|awk '{print \$1}' |sort |uniq -d)

    if [[ "" != \$dup_key ]]; then
        LOG_ERROR "[certificate_whitelist] has duplicate keys:"
        LOG_ERROR "\$dup_key"
        exit 1
    fi
}

check_whitelist()
{
    config_file=\$1
    check_cal_lines \$config_file
    check_duplicate_key \$config_file
}

check_whitelist \${SHELL_FOLDER}/../config.ini

cd \${SHELL_FOLDER}/../
NODE_FOLDER=\$(pwd)
fisco_bcos=\${NODE_FOLDER}/${fisco_bin_path}
node=\$(basename \${NODE_FOLDER})
node_pid=${ps_cmd}
if [ -n "\${node_pid}" ];then
    echo "\${node} is trying to reset certificate whitelist. Check log for more information."
    touch config.ini.reset_certificate_whitelist
    exit 0
else
    echo "\${node} is not running, use start.sh to start and enable whitelist directlly."
fi
EOF

generate_script_template "$output/scripts/monitor.sh"
    cat << EOF >> "$output/scripts/monitor.sh"

# * * * * * bash monitor.sh -d /data/nodes/127.0.0.1/ > monitor.log 2>&1
cd \$SHELL_FOLDER

alarm() {
        alert_ip=\$(/sbin/ifconfig eth0 | grep inet | awk '{print \$2}')
        time=\$(date "+%Y-%m-%d %H:%M:%S")
        echo "[\${time}] \$1"
}

# restart the node
restart() {
        local nodedir=\$1
        bash \$nodedir/stop.sh
        sleep 5
        bash \$nodedir/start.sh
}

info() {
        time=\$(date "+%Y-%m-%d %H:%M:%S")
        echo "[\$time] \$1"
}

# check if nodeX is work well
function check_node_work_properly() {
        nodedir=\$1
        # node name
        node=\$(basename \$nodedir)
        # fisco-bcos path
        fiscopath=\${nodedir}/
        config=\$1/config.ini
        # rpc url
        config_ip="127.0.0.1"
        config_port=\$(cat \$config | grep 'jsonrpc_listen_port' | egrep -o "[0-9]+")

        # check if process id exist
        pid=\$(ps aux | grep "\$fiscopath" | egrep "fisco-bcos" | grep -v "grep" | awk -F " " '{print \$2}')
        [ -z "\${pid}" ] && {
                alarm " ERROR! \$config_ip:\$config_port \$node does not exist"
                restart \$nodedir
                return 1
        }

        # get group_id list
        groups=\$(ls \${nodedir}/conf/group*genesis | grep -o "group.*.genesis" | grep -o "group.*.genesis" | cut -d \. -f 2)
        for group in \${groups}; do
                # get blocknumber
                heightresult=\$(curl -s "http://\$config_ip:\$config_port" -X POST --data "{\"jsonrpc\":\"2.0\",\"method\":\"getBlockNumber\",\"params\":[\${group}],\"id\":67}")
                echo \$heightresult
                height=\$(echo \$heightresult | awk -F'"' '{if(\$2=="id" && \$4=="jsonrpc" && \$8=="result") {print \$10}}')
                [[ -z "\$height" ]] && {
                        alarm " ERROR! Cannot connect to \$config_ip:\$config_port \$node:\$group "
                        continue
                }

                height_file="\$nodedir/\$node_\$group.height"
                prev_height=0
                [ -f \$height_file ] && prev_height=\$(cat \$height_file)
                heightvalue=\$(printf "%d\n" "\$height")
                prev_heightvalue=\$(printf "%d\n" "\$prev_height")

                viewresult=\$(curl -s "http://\$config_ip:\$config_port" -X POST --data "{\"jsonrpc\":\"2.0\",\"method\":\"getPbftView\",\"params\":[\$group],\"id\":68}")
                echo \$viewresult
                view=\$(echo \$viewresult | awk -F'"' '{if(\$2=="id" && \$4=="jsonrpc" && \$8=="result") {print \$10}}')
                [[ -z "\$view" ]] && {
                        alarm " ERROR! Cannot connect to \$config_ip:\$config_port \$node:\$group "
                        continue
                }

                view_file="\$nodedir/\$node_\$group.view"
                prev_view=0
                [ -f \$view_file ] && prev_view=\$(cat \$view_file)
                viewvalue=\$(printf "%d\n" "\$view")
                prev_viewvalue=\$(printf "%d\n" "\$prev_view")

                # check if blocknumber of pbft view already change, if all of them is the same with before, the node may not work well.
                [ \$heightvalue -eq \$prev_heightvalue ] && [ \$viewvalue -eq \$prev_viewvalue ] && {
                        alarm " ERROR! \$config_ip:\$config_port \$node:\$group is not working properly: height \$height and view \$view no change"
                        continue
                }

                echo \$height >\$height_file
                echo \$view >\$view_file
                info " OK! \$config_ip:\$config_port \$node:\$group is working properly: height \$height view \$view"

        done

        return 0
}

# check all node of this server, if all node work well.
function check_all_node_work_properly() {
        local work_dir=\$1
        for configfile in \$(ls \${work_dir}/node*/config.ini); do
                check_node_work_properly \$(dirname \$configfile)
        done
}

function help() {
        echo "Usage:"
        echo "Optional:"
        echo "    -d  <path>          work dir(default: \\$SHELL_FOLDER/../). "
        echo "    -h                  Help."
        echo "Example:"
        echo "    bash monitor.sh -d /data/nodes/127.0.0.1 "
        exit 0
}

work_dir=\${SHELL_FOLDER}/../../

while getopts "d:r:c:s:h" option; do
        case \$option in
        d) work_dir=\$OPTARG ;;
        h) help ;;
        esac
done

[ ! -d \${work_dir} ] && {
        LOG_ERROR "work_dir(\$work_dir) not exist "
        exit 0
}

check_all_node_work_properly \${work_dir}

EOF
generate_script_template "$output/scripts/reload_sdk_allowlist.sh"
    cat << EOF >> "$output/scripts/reload_sdk_allowlist.sh"
cd \${SHELL_FOLDER}/../
NODE_FOLDER=\$(pwd)
fisco_bcos=\${NODE_FOLDER}/${fisco_bin_path}
node=\$(basename \${NODE_FOLDER})
node_pid=${ps_cmd}
if [ -n "\${node_pid}" ];then
    LOG_INFO "\${node} is trying to reload sdk allowlist. Check log for more information."
    touch config.ini.reset_allowlist
    exit 0
else
    echo "\${node} is not running, use start.sh to start all group directlly."
fi
EOF
}




main(){
    if [ ! -z "$(openssl version | grep reSSL)" ];then
        export PATH="/usr/local/opt/openssl/bin:$PATH"
    fi
    mkdir ${output_dir}
    while :
    do
        gen_node_cert_gm "${agency_key_path}" "${output_dir}" 
        if [ ${node_key_type} == "externalKey" ];then
            privateKey=$($SWSSL_CMD ec -in "${output_dir}/${gm_conf_path}/gmnode.key" -text 2> /dev/null| sed -n '3,5p' | sed 's/://g'| tr "\n" " "|sed 's/ //g')
            echo ${privateKey}
            len=${#privateKey}
            head2=${privateKey:0:2}
            # private key should not start with 00
            if [ "64" != "${len}" ] || [ "00" == "$head2" ];then
                rm -rf ${node_dir}
                echo "continue gm because of length=${len} head=$head2" >>"${logfile}"
                continue;
            fi 
        fi
        break;
    done
    mv ${output_dir}/${gm_conf_path} ${output_dir}/${conf_path}
    # generate_node_scripts "${output_dir}"
    if [[ -n "${sdk_cert}" ]]; then
        if [ ${node_key_type} == "externalKey" ];then
            mv "${output_dir}/${conf_path}/gmnode.key" "${output_dir}/${conf_path}/gmsdk.key"
            mv "${output_dir}/${conf_path}/gmennode.key" "${output_dir}/${conf_path}/gmensdk.key"
        fi
        mv "${output_dir}/${conf_path}/gmnode.crt" "${output_dir}/${conf_path}/gmsdk.crt"
        mv "${output_dir}/${conf_path}/gmennode.crt" "${output_dir}/${conf_path}/gmensdk.crt"
        mv "${output_dir}/${conf_path}/gmnode.nodeid" "${output_dir}/${conf_path}/gmsdk.publickey"
        mv "${output_dir}/${conf_path}" "${output_dir}/gm"
    fi
    if [ -f "${logfile}" ];then rm "${logfile}";fi
}

parse_params $@
main
print_result
