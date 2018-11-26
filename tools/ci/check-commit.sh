# "Copyright [2018] <fisco-bcos>"
# @ function: check code format of {.h, .hpp and .cpp} files
# @ require : Make sure your machine is linux (centos/ubuntu), yum or apt is ready 
# @ author  : wheatli
# @ file    : check-commit.sh
# @ date    : 2018

# !/bin/bash
check_script=/usr/bin/run-clang-format.py
Ubuntu_Platform=0
Centos_Platform=1

LOG_ERROR() {
    content=${1}
    echo -e "\033[31m"${content}"\033[0m"
}

LOG_INFO() {
    content=${1}
    echo -e "\033[32m"${content}"\033[0m"
}

execute_cmd() {
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

# get platform: now support debain/ubuntu, fedora/centos, oracle
get_platform() {
    uname -v > /dev/null 2>&1 || { echo >&2 "ERROR - FISCO-BCOS requires 'uname' to identify the platform."; exit 1; }
    case $(uname -s) in
    Darwin)
        LOG_ERROR "FISCO-BCOS V2.0 Don't Support MAC OS Yet!"
        exit 1;;
    FreeBSD)
        LOG_ERROR "FISCO-BCOS V2.0 Don't Support FreeBSD Yet!"
        exit 1;;
    Linux)
        if [ -f "/etc/arch-release" ]; then
            LOG_ERROR "FISCO-BCOS V2.0 Don't Support arch-linux Yet!"
        elif [ -f "/etc/os-release" ];then
            DISTRO_NAME=$(. /etc/os-release; echo $NAME)
            case $DISTRO_NAME in
            Debian*|Ubuntu)
                LOG_INFO "Debian*|Ubuntu Platform"
                return ${Ubuntu_Platform};; #ubuntu type
            Fedora|CentOS*|Oracle*)
                LOG_INFO "Fedora|CentOS* Platform"
                return ${Centos_Platform};; #centos type
            esac
        else
            LOG_ERROR "Unsupported Platform"
        fi
    esac
}

function install_clang_format() 
{
    get_platform
    platform=`echo $?`
    if [ ${platform} -eq ${Ubuntu_Platform} ];then
        execute_cmd "sudo apt-get update"
        #execute_cmd "sudo apt-get install clang"
    elif [ ${platform} -eq ${Centos_Platform} ];then
        execute_cmd "sudo yum upgrade"
        #execute_cmd "sudo yum install clang"
    else
        LOG_ERROR "Unsupported platform!"
    fi
}

function deploy_check_script() 
{
    execute_cmd "sudo cp ./tools/ci/run-clang-format.py /usr/bin/run-clang-format.py"
    execute_cmd "sudo chmod a+x /usr/bin/run-clang-format.py"
}

function check()
{
    #install_clang_format
    deploy_check_script
    if git rev-parse --verify HEAD >/dev/null 2>&1;then
        against=HEAD^
    else
        # Initial commit: diff against an empty tree object
        against=4b825dc642cb6eb9a060e54bf8d69288fbee4904
    fi
    LOG_INFO "against: ${against}"
    # Redirect output to stderr.
    exec 1>&2
    sum=0
    git diff-index --name-status $against -- | grep -v D | grep -E '\.[ch](pp)?$' | awk '{print $2}'        
    # for check-script
    for file in $(git diff-index --name-status $against -- | grep -v D | grep -E '\.[ch](pp)?$' | awk '{print $2}'); do
        execute_cmd "$check_script $file"
        LOG_INFO "=== file: ${file}" 
        sum=$(expr ${sum} + $?)
    done
        
    if [ ${sum} -eq 0 ]; then
        exit 0
    else
        echo "######### ERROR: Format check failed, please adjust them before commit"
        exit 1
    fi
}

check