#!/bin/bash
console_branch="master"
fisco_bcos_service_path="../build/fisco-bcos-tars-service/"
build_chain_path="BcosBuilder/pro/build_chain.py"
current_path=`pwd` # tools
node_list="group0_node_40402 group0_node_40412 group0_node_40422 group0_node_40432"
output_dir="pro_nodes"
auth_admin_account=""
LOG_ERROR() {
    local content=${1}
    echo -e "\033[31m ${content}\033[0m"
}

LOG_INFO() {
    local content=${1}
    echo -e "\033[32m ${content}\033[0m"
}

LOG_WARN() {
    local content=${1}
    echo -e "\033[31m[ERROR] ${content}\033[0m"
}

stop_node()
{
    cd ${current_path}

    LOG_INFO "exit_node >>>>>>> stop all pro nodes <<<<<<<<<<<"
    if [ -z "$(bash ${output_dir}/127.0.0.1/stop_all.sh |grep 'Exceed waiting time')" ];then
      LOG_INFO "Stop success"
    else
      LOG_ERROR "Could not stop the node"
      exit 1
    fi
}

exit_node()
{
    cd ${current_path}
    local all_service="rpc_20200 rpc_20201 gateway_30300 gateway_30301 ${node_list}"

    for node in ${all_service}
    do
        LOG_ERROR "exit_node ============= print error|warn info for ${node} ============="
        cat ${output_dir}/127.0.0.1/${node}/log/* |grep -iE 'error|warn|cons|connectedSize|heart'
        LOG_ERROR "exit_node ============= print error|warn info for ${node} finish ============="
        LOG_ERROR "exit_node ########### print nohup info for ${node} ###########"
        cat ${output_dir}/127.0.0.1/${node}/nohup.out
        LOG_ERROR "exit_node ########### print nohup info for ${node} finish ###########"
    done
    stop_node
    LOG_ERROR "exit_node ######### exit for ${1}"
    exit 1
}

wait_and_start()
{
    LOG_INFO "Try to start all"
    if [ -z "$(bash start_all.sh | grep 'Exceed waiting time')" ]; then
        LOG_INFO "Start all success"
    else
        bash stop_all.sh
        LOG_WARN "Another testing is running. Waiting 20s and re-try to start all."
        sleep 20
        wait_and_start
    fi
}

generate_auth_account()
{
  local account_script="get_account.sh"
  local use_sm="${1}"
  if [ "${use_sm}" == "true" ]; then
      account_script="get_gm_account.sh"
  fi

  if [ ! -f ${account_script} ]; then
        local get_account_link="https://raw.githubusercontent.com/FISCO-BCOS/console/master/tools/${account_script}"
        LOG_INFO "Downloading ${account_script} from ${get_account_link}..."
        curl -#LO "${get_account_link}"
  fi
  auth_admin_account=$(bash ${account_script} | grep Address | sed -r "s/\x1B\[([0-9]{1,2}(;[0-9]{1,2})?)?[m|K]//g" | awk '{print $5}')
  LOG_INFO "Admin account: ${auth_admin_account}"
}

init()
{
    local use_sm="${1}"
    cd ${current_path}
    local sed_cmd="sed -i"
    if [ "$(uname)" == "Darwin" ];then
        sed_cmd="sed -i .bkp"
    fi
    echo "===>> ${current_path}"
    generate_auth_account ${use_sm}

    rm -rf BcosBuilder/pro/binary/

    mkdir -p BcosBuilder/pro/binary/
    # copy service binary
    mkdir -p BcosBuilder/pro/binary/BcosNodeService
    mkdir -p BcosBuilder/pro/binary/BcosGatewayService
    mkdir -p BcosBuilder/pro/binary/BcosRpcService

    cp -f ${fisco_bcos_service_path}/NodeService/pro/BcosNodeService BcosBuilder/pro/binary/BcosNodeService
    cp -f ${fisco_bcos_service_path}/GatewayService/main/BcosGatewayService BcosBuilder/pro/binary/BcosGatewayService
    cp -f ${fisco_bcos_service_path}/RpcService/main/BcosRpcService BcosBuilder/pro/binary/BcosRpcService
    rm -rf ${output_dir}

    python3 --version
    pip3 --version
    sudo pip3 install -r BcosBuilder/requirements.txt

    local not_use_sm="true"
    if [ "${use_sm}" == "true" ]; then
          not_use_sm="false"
    fi
    local rpc_sm_ssl="rpc_sm_ssl=${use_sm}"
    local gateway_sm_ssl="gateway_sm_ssl=${use_sm}"
    local sm_crypto="sm_crypto=${use_sm}"

    cd BcosBuilder/pro/
    ${sed_cmd} "s/init_auth_address=\"\"/init_auth_address=\"${auth_admin_account}\"/g" conf/config-build-example.toml
    ${sed_cmd} "s/rpc_sm_ssl=${not_use_sm}/${rpc_sm_ssl}/g" conf/config-build-example.toml
    ${sed_cmd} "s/gateway_sm_ssl=${not_use_sm}/${gateway_sm_ssl}/g" conf/config-build-example.toml
    ${sed_cmd} "s/sm_crypto=${not_use_sm}/${sm_crypto}/g" conf/config-build-example.toml
    python3  build_chain.py build -c conf/config-build-example.toml -O ${current_path}/${output_dir}
    cd ${current_path}

    cd ${output_dir}/127.0.0.1 && wait_and_start
}

check_consensus()
{
    cd ${current_path}/${output_dir}/127.0.0.1
    LOG_INFO "=== wait for the node to init, waitTime: 20s ====="
    sleep 20
    LOG_INFO "=== wait for the node to init finish ====="
    for node in ${node_list}
    do
        LOG_INFO "check_consensus for ${node}"
        result=$(cat ${node}/log/*log |grep -i reachN)
        if [[ -z "${result}" ]];
        then
            LOG_ERROR "checkView failed ******* cons info for ${node} *******"
            cat ${node}/log/* |grep -i cons
            LOG_ERROR "checkView failed ******* print log info for ${node} finish *******"
            exit_node "check_consensus for ${node} failed for not reachNewView"
        else
            LOG_INFO "check_consensus for ${node} success"
        fi
    done
}

expand_node()
{
    cd ${current_path}/BcosBuilder/pro
    rm -rf config-expand.toml
    LOG_INFO "expand node..."
    cp conf/config-build-example.toml config-expand.toml
    local sed_cmd="sed -i"
    local sed_cmd1="sed -i"
    if [ "$(uname)" == "Darwin" ];then
        sed_cmd="sed -i ''"
        sed_cmd1="sed -i .bkp"
    fi
    ${sed_cmd} 91,248d config-expand.toml
    ${sed_cmd1} 's/name = "agencyA"/name = "agencyE"/' config-expand.toml
    ${sed_cmd1} 's/peers=\[/peers=\["127.0.0.1:30304",/' config-expand.toml
    ${sed_cmd1} 's/listen_port=20200/listen_port=20204/' config-expand.toml
    ${sed_cmd1} 's/tars_listen_port=40400/tars_listen_port=40440/' config-expand.toml
    ${sed_cmd1} 's/listen_port=30300/listen_port=30304/' config-expand.toml
    ${sed_cmd1} 's/tars_listen_port=40401/tars_listen_port=40441/' config-expand.toml
    ${sed_cmd1} 's/tars_listen_port=40402/tars_listen_port=40442/' config-expand.toml
    python3  build_chain.py build -c config-expand.toml -O ${current_path}/${output_dir}
    LOG_INFO "start expandNode gateway..."
    bash ${current_path}/${output_dir}/127.0.0.1/gateway_30304/start.sh
    LOG_INFO "start expandNode rpc..."
    bash ${current_path}/${output_dir}/127.0.0.1/rpc_20204/start.sh
    LOG_INFO "start expandNode..."
    bash ${current_path}/${output_dir}/127.0.0.1/group0_node_40442/start.sh
    LOG_INFO "expand node success"
    # sleep 10
    # LOG_INFO "get new node nodeid..."
    # nodeid=$(cat ${current_path}/${output_dir}/127.0.0.1/group0_node_40442/conf/node.nodeid)
    # bash ${current_path}/console/console.sh addObserver ${nodeid}
}

clear_node()
{
    cd ${current_path}
    bash ${output_dir}/127.0.0.1/stop_all.sh
    rm -rf ${output_dir}
}

if [[ -n "${1}" ]]; then
     console_branch=${1}
fi

# non-sm test
LOG_INFO "======== check non-sm case ========"
init "false"
expand_node
pwd
bash ${current_path}/.ci/console_ci_test.sh ${console_branch} "false" "${current_path}/${output_dir}/127.0.0.1/rpc_20200/conf"
if [[ ${?} == "0" ]]; then
        LOG_INFO "console_integrationTest success"
    else
        echo "console_integrationTest error"
        exit 1
fi
bash ${current_path}/.ci/java_sdk_ci_test.sh ${console_branch} "false" "${current_path}/${output_dir}/127.0.0.1/rpc_20200/conf"
if [[ ${?} == "0" ]]; then
        LOG_INFO "java_sdk_integrationTest success"
    else
        echo "java_sdk_integrationTest error"
        exit 1
fi
stop_node
clear_node
LOG_INFO "======== check non-sm success ========"


LOG_INFO "======== check sm case ========"
init "true"
bash ${current_path}/.ci/console_ci_test.sh ${console_branch} "true" "${current_path}/${output_dir}/127.0.0.1/rpc_20200/conf"
if [[ ${?} == "0" ]]; then
        LOG_INFO "console_integrationTest success"
    else
        echo "console_integrationTest error"
        exit 1
fi
stop_node
clear_node
LOG_INFO "======== check sm case success ========"
