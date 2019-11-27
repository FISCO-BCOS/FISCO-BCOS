#!/bin/bash

set -e

scan_code_script="cobra/cobra.py -f json -o /tmp/report.json -t "
ignore_files=(tools/ EventLogFilterTest cmake LedgerTest.cpp FakeModule.h SyncMasterTest.cpp \
verifierMain.cpp consensus_main.cpp sync_main.cpp ExecuteResultTest.cpp trie.cpp CommonJS.cpp \
EthCoreCommonJS.cpp HostTest.cpp PeerWhitelist.cpp Common.cpp test_Service.cpp FakeSyncToolsSet.h \
test_ConsensusPrecompiled.cpp RpcTest.cpp .genesis test_PermissionPrecompiled.cpp)

LOG_ERROR() {
    content=${1}
    echo -e "\033[31m${content}\033[0m"
}

LOG_INFO() {
    content=${1}
    echo -e "\033[32m${content}\033[0m"
}

should_ignore()
{
    local file=${1}
    for ignore in ${ignore_files[*]}; do
        if echo ${file} | grep ${ignore} &>/dev/null; then
            echo "ignore ${file} ${ignore}"
            return 0
        fi
    done
    return 1
}

scan_code()
{
    # Redirect output to stderr.
    exec 1>&2
    local count=0
    for file in $(git diff-index --name-status HEAD^ | awk '{print $2}'); do
        if should_ignore ${file}; then continue; fi
        if [ ! -f ${file} ];then continue; fi
        LOG_INFO "check file ${file}"
        python ${scan_code_script} $file 
        trigger_rules=$(jq -r '.' /tmp/report.json | grep 'trigger_rules' | awk '{print $2}' | sed 's/,//g')
        count=$((count+trigger_rules))
        echo "trigger_rules is ${trigger_rules}"
        rm /tmp/report.json
    done
    if [ ${count} -ne 0 ]; then
        echo "######### ERROR: Scan code failed, please adjust them before commit"
        exit 1
    fi
}

install_cobra() {
   git clone https://github.com/WhaleShark-Team/cobra.git
   pip install -r cobra/requirements.txt
   cp cobra/config.template cobra/config
}

install_cobra
scan_code