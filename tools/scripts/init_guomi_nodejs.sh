# "Copyright [2018] <fisco-bcos>"
# @ function : one-click install shell script(appliable for centos and ubuntu)
# @ Require  : yum or apt, git are ready
# @ attention: if dependecies are downloaded failed, 
#              please fetch packages into "deps/src" from https://github.com/bcosorg/lib manually
#              and execute this shell script later
# @ author   : yujiechen  
# @ file     : init_guomi_nodejs.sh
# @ date     : 2018
#!/bin/bashsh
current_dir=`pwd`"/../.."
LOG_ERROR()
{
    content=${1}
    echo -e "\033[31m"${content}"\033[0m"
}

LOG_INFO()
{
    content=${1}
    echo -e "\033[32m"${content}"\033[0m"
}

execute_cmd()
{
    command="${1}"
    #LOG_INFO "RUN: ${command}"
    eval ${command}
    ret=$?
    if [ $ret -ne 0 ];then
        LOG_ERROR "FAILED execution of command: ${command}"
        exit 1
    else
        LOG_INFO "SUCCESS execution of command: ${command}"
    fi
}

update_configjs()
{
    execute_cmd "cd ${current_dir}/web3lib"
    sed -i 's/var encryptType = 0;/var encryptType = 1;/g' config.js
    execute_cmd "cd ${current_dir}/tools/web3lib"
    sed -i 's/var encryptType = 0;/var encryptType = 1;/g' config.js
}

install_base_nodejs()
{
    cd ${current_dir}
    execute_cmd "cd ${current_dir}/web3lib && cnpm install"
    execute_cmd "cd ${current_dir}/tool && cnpm install"
    execute_cmd "cd ${current_dir}/systemcontract && cnpm install"
    
    execute_cmd "cd ${current_dir}/tools/contract && cnpm install"
    execute_cmd "cd ${current_dir}/tools/systemcontract && cnpm install"
    execute_cmd "cd ${current_dir}/tools/web3lib && cnpm install"
}

init_guomi_nodejs()
{
    cd ${current_dir}
    execute_cmd "cd ${current_dir}/web3lib && bash guomi.sh"
    execute_cmd "cd ${current_dir}/tool && bash guomi.sh"
    execute_cmd "cd ${current_dir}/systemcontract && bash guomi.sh"
    
    execute_cmd "cd ${current_dir}/tools/contract && bash guomi.sh"
    execute_cmd "cd ${current_dir}/tools/systemcontract && bash guomi.sh"
    execute_cmd "cd ${current_dir}/tools/web3lib && bash guomi.sh"
    update_configjs
}

update_configjs
install_base_nodejs
init_guomi_nodejs
