#!/bin/bash

# This script is used to summary the performance of the chain
# author: xingqiangbai
# date: 2023/07/13

log_start_block=
log_end_block=
start_block=0
end_block=0
time_elapsed_seconds=0
tmp_log_dir=./.temp_log
analysis_log_prefix=

LOG_WARN() {
    local content=${1}
    echo -e "\033[31m[ERROR] ${content}\033[0m"
}

LOG_INFO() {
    local content=${1}
    echo -e "\033[32m[INFO] ${content}\033[0m"
}

LOG_FATAL() {
    local content=${1}
    echo -e "\033[31m[FATAL] ${content}\033[0m"
    exit 1
}

print_fixed_index_value() {
    local index=${1}
    local avg=${2}
    local max=${3}
    local min=${4}
    echo -e "${index}: avg=${avg}, max=${max}, min=${min}"
}

print_header() {
    echo -e "================================================"
    echo -e "${1}"
    echo -e "================================================"
}

prepare_tmp_log()
{
    local log_file=$1
    mkdir -p ${tmp_log_dir}
    local start_line=$(cat -n $log_file | grep -aiE "addPrepareCache.*,reqIndex=${start_block}" | head -n 1)
    # start_line=$(cat -n $log_file | grep -aiE "Generate proposal,index=${start_block}|Generating seal on,index=${start_block}" | head -n 1)
    start_line_number=$(echo "${start_line}"| awk '{print $1}')
    local start_block_timepoint=$(echo "${start_line}" | awk -F '|' '{print $2}'| awk -F '.' '{print $1}')
    local end_line=$(cat -n $log_file | grep -a "Report.*committedIndex=${end_block}," | tail -n 1)
    end_line_number=$(echo "${end_line}"| awk '{print $1}')
    local end_block_timepoint=$(echo "${end_line}" | awk -F '|' '{print $2}'| awk -F '.' '{print $1}')
    time_elapsed_seconds=$(echo $(date -d "${end_block_timepoint}" +%s)-$(date -d "${start_block_timepoint}" +%s) | bc)
    # prepare the log in request block number range
    analysis_log_prefix="${tmp_log_dir}/${start_block}_${end_block}"
    sed -n "${start_line_number},${end_line_number}p" $log_file > "${analysis_log_prefix}.log"
    echo "analysis log from line ${start_line_number} to line ${end_line_number}, from ${start_block_timepoint} to ${end_block_timepoint}, total time: ${time_elapsed_seconds}s"
}

clean_tmp_log()
{
    if [ -d ${tmp_log_dir} ]; then
        rm -rf ${tmp_log_dir}
    fi
}

count_index()
{
    print_header "count indexes of block range [${start_block}, ${end_block}]"
    # process txpool log
    local log_file=$1
    local txpool_log="$(grep -aE "TXPOOL" $1)"
    local nofity_receipt_and_remove_tx_time_list="$(echo "${txpool_log}" | grep -aE "batchRemove txs success" | awk -F '=' '{print $5}' | awk -F ',' '{print $1}')"
    local average_nofity_receipt_and_remove_tx_time=$(echo "${nofity_receipt_and_remove_tx_time_list}" | awk '{sum+=$1} END {print sum/NR}')
    local max_nofity_receipt_and_remove_tx_time=$(echo "${nofity_receipt_and_remove_tx_time_list}" | awk 'max<$1 || NR==1{ max=$1 } END {print max}')
    local min_nofity_receipt_and_remove_tx_time=$(echo "${nofity_receipt_and_remove_tx_time_list}" | awk 'min>$1 || NR==1{ min=$1 } END {print min}')

    # process consensus and sync log
    grep -aE "CONSENSUS|BLOCK SYNC" ${log_file} > "${analysis_log_prefix}_executor_sync.log"
    local consensus_log_file="${analysis_log_prefix}_executor_sync.log"

    local execute_block_time_list="$(cat ${consensus_log_file} | grep -iaE "asyncExecuteBlock success,sysBlock|BlockSync: applyBlock success" | awk -F '=' '{print $9}' | awk -F ',' '{print $1}')"
    local average_execute_block_time=$(echo "${execute_block_time_list}" | awk '{sum+=$1} END {print sum/NR}')
    local max_execute_block_time=$(echo "${execute_block_time_list}" | awk 'max<$1 || NR==1{ max=$1 } END {print max}')
    local min_execute_block_time=$(echo "${execute_block_time_list}" | awk 'min>$1 || NR==1{ min=$1 } END {print min}')

    local execute_tx_time_list="$(cat ${consensus_log_file} | grep -iaE "asyncExecuteBlock success,sysBlock|BlockSync: applyBlock success" | awk -F '=' '{print $10}' | awk -F ',' '{print $1}')"
    local average_execute_tx_time=$(echo "${execute_tx_time_list}" | awk '{sum+=$1} END {print sum/NR}')
    local max_execute_tx_time=$(echo "${execute_tx_time_list}" | awk 'max<$1 || NR==1{ max=$1 } END {print max}')
    local min_execute_tx_time=$(echo "${execute_tx_time_list}" | awk 'min>$1 || NR==1{ min=$1 } END {print min}')

    local sealed_queue_length_list="$(cat ${consensus_log_file} | grep -iaE "Generate proposal" | awk -F '[,=]' '{print $3-$5}')"
    local average_sealed_queue_length=$(echo "${sealed_queue_length_list}" | awk '{sum+=$1} END {print sum/NR}')
    local max_sealed_queue_length=$(echo "${sealed_queue_length_list}" | awk 'max<$1 || NR==1{ max=$1 } END {print max}')
    local min_sealed_queue_length=$(echo "${sealed_queue_length_list}" | awk 'min>$1 || NR==1{ min=$1 } END {print min}')

    local txs_in_block_list=$(cat ${consensus_log_file} | grep -iaE "Report,sea" | awk -F '=' '{print $3}' |awk -F ',' '{print $1}')
    local total_txs=$(echo "${txs_in_block_list}" | awk '{sum+=$1} END {print sum}')
    local average_txs_in_block=$(echo "${txs_in_block_list}" | awk '{sum+=$1} END {print sum/NR}')
    local max_txs_in_block=$(echo "${txs_in_block_list}" | awk 'max<$1 || NR==1{ max=$1 } END {print max}')
    local min_txs_in_block=$(echo "${txs_in_block_list}" | awk 'min>$1 || NR==1{ min=$1 } END {print min}')
    local latest_report=$(cat ${consensus_log_file} | grep -iaE "Report,sea" | tail -n 1)
    local latest_block=$(echo "${latest_report}"| awk -F '=' '{print $4}'|awk -F ',' '{print $1}')
    local latest_consensus=$(echo "${latest_report}"| awk -F '=' '{print $5}'|awk -F ',' '{print $1}')
    local view=$(echo "${latest_report}"| awk -F '=' '{print $7}'|awk -F ',' '{print $1}')
    local unsealtxs=$(echo "${latest_report}"| awk -F '=' '{print $12}'|awk -F ',' '{print $1}')
    local consensus_timeout=$(echo "${latest_report}"| awk -F '=' '{print $15}'|awk -F ',' '{print $1}')

    # process executor and scheduler log
    grep -aE "SCHEDULER|EXECUTOR" ${log_file} > "${analysis_log_prefix}_executor.log"
    local executor_log="${analysis_log_prefix}_executor.log"
    local hash_time_list="$(cat ${executor_log} | grep -aE "GetTableHashes success" | awk -F '=' '{print $3}' | awk -F ',' '{print $1}')"
    local average_hash_time=$(echo "${hash_time_list}" | awk '{sum+=$1} END {print sum/NR}')
    local max_hash_time=$(echo "${hash_time_list}" | awk 'max<$1 || NR==1{ max=$1 } END {print max}')
    local min_hash_time=$(echo "${hash_time_list}" | awk 'min>$1 || NR==1{ min=$1 } END {print min}')

    local wait_time_list="$(cat ${executor_log} | grep -iaE "ExecuteBlock request.*waitT" | awk -F '=' '{print $8}' | awk -F ',' '{print $1}')"
    local average_wait_time=$(echo "${wait_time_list}" | awk '{sum+=$1} END {print sum/NR}')
    local max_wait_time=$(echo "${wait_time_list}" | awk 'max<$1 || NR==1{ max=$1 } END {print max}')
    local min_wait_time=$(echo "${wait_time_list}" | awk 'min>$1 || NR==1{ min=$1 } END {print min}')
    local latest_execute_request=$(cat ${executor_log} | grep -iaE "ExecuteBlock request.*waitT" | tail -n 1)
    local last_executed_block=$(echo "${latest_execute_request}" | awk -F ']' '{print $3}' | awk -F '-' '{print $2}')
    local latest_block_version=$(echo "${latest_execute_request}" | awk -F '=' '{print $7}' | awk -F ',' '{print $1}')

    local commit_block_time_list="$(cat ${executor_log} | grep -iaE "CommitBlock success" | awk -F '=' '{print $3}' | awk -F ',' '{print $1}')"
    local average_commit_block_time=$(echo "${commit_block_time_list}" | awk '{sum+=$1} END {print sum/NR}')
    local max_commit_block_time=$(echo "${commit_block_time_list}" | awk 'max<$1 || NR==1{ max=$1 } END {print max}')
    local min_commit_block_time=$(echo "${commit_block_time_list}" | awk 'min>$1 || NR==1{ min=$1 } END {print min}')

    local preExecute_time_list="$(cat ${log_file} | grep -iaE "preExeBlock success" | awk -F '=' '{print $4}' | awk -F ',' '{print $1}')"
    local average_preExecute_time=$(echo "${preExecute_time_list}" | awk '{sum+=$1} END {print sum/NR}')
    local max_preExecute_time=$(echo "${preExecute_time_list}" | awk 'max<$1 || NR==1{ max=$1 } END {print max}')
    local min_preExecute_time=$(echo "${preExecute_time_list}" | awk 'min>$1 || NR==1{ min=$1 } END {print min}')

    # process storage log
    grep -a "STORAGE" ${log_file} > "${analysis_log_prefix}_storage.log"
    local storage_log_file="${analysis_log_prefix}_storage.log"

    local prepare_encode_time_list="$(cat ${storage_log_file} |grep -iaE "asyncPrepare.*finished" | awk -F '=' '{print $6}' | awk -F ',' '{print $1}')"
    local average_prepare_encode_time=$(echo "${prepare_encode_time_list}" | awk '{sum+=$1} END {print sum/NR}')
    local max_prepare_encode_time=$(echo "${prepare_encode_time_list}" | awk 'max<$1 || NR==1{ max=$1 } END {print max}')
    local min_prepare_encode_time=$(echo "${prepare_encode_time_list}" | awk 'min>$1 || NR==1{ min=$1 } END {print min}')

    local prepare_time_list="$(cat ${storage_log_file} |grep -iaE "asyncPrepare.*finished" | awk -F '=' '{print $7}' | awk -F ',' '{print $1}')"
    local average_prepare_time=$(echo "${prepare_time_list}" | awk '{sum+=$1} END {print sum/NR}')
    local max_prepare_time=$(echo "${prepare_time_list}" | awk 'max<$1 || NR==1{ max=$1 } END {print max}')
    local min_prepare_time=$(echo "${prepare_time_list}" | awk 'min>$1 || NR==1{ min=$1 } END {print min}')

    local commit_time_list="$(cat ${storage_log_file} | grep -iaE "asyncCommit finished" | awk -F '=' '{print $4}' | awk -F ',' '{print $1}')"
    local average_commit_time=$(echo "${commit_time_list}" | awk '{sum+=$1} END {print sum/NR}')
    local max_commit_time=$(echo "${commit_time_list}" | awk 'max<$1 || NR==1{ max=$1 } END {print max}')
    local min_commit_time=$(echo "${commit_time_list}" | awk 'min>$1 || NR==1{ min=$1 } END {print min}')
    # commitTime=$(cat ${log_file} |grep -iaE "asyncCommit finished" |awk -F '=' '{print $4}' |awk -F ',' '{print $1}' |awk '{sum+=$1} END {print sum/NR}')

    local write_setrows_time_list="$(cat ${storage_log_file} | grep -iaE "setRows finished" | awk -F '=' '{print $4}' | awk -F ',' '{print $1}')"
    local average_setrows_time=$(echo "${write_setrows_time_list}" | awk '{sum+=$1} END {print sum/NR}')
    local max_setrows_time=$(echo "${write_setrows_time_list}" | awk 'max<$1 || NR==1{ max=$1 } END {print max}')
    local min_setrows_time=$(echo "${write_setrows_time_list}" | awk 'min>$1 || NR==1{ min=$1 } END {print min}')

    local write_txs_receipts_time_list="$(cat ${1} | grep -aE "storeTransactionsAndReceipts finished" | awk -F '=' '{print $5}' | awk -F ',' '{print $1}')"
    local average_write_txs_receipts_time=$(echo "${write_txs_receipts_time_list}" | awk '{sum+=$1} END {print sum/NR}')
    local max_write_txs_receipts_time=$(echo "${write_txs_receipts_time_list}" | awk 'max<$1 || NR==1{ max=$1 } END {print max}')
    local min_write_txs_receipts_time=$(echo "${write_txs_receipts_time_list}" | awk 'min>$1 || NR==1{ min=$1 } END {print min}')

    local result="module operation average(ms) max(ms) min(ms)\n"
    result="${result}consensus executeBlock ${average_execute_block_time} ${max_execute_block_time} ${min_execute_block_time}\n"
    result="${result}consensus executeTx ${average_execute_tx_time} ${max_execute_tx_time} ${min_execute_tx_time}\n"
    result="${result}consensus txsInBlock ${average_txs_in_block} ${max_txs_in_block} ${min_txs_in_block}\n"
    result="${result}consensus sealedQueue ${average_sealed_queue_length} ${max_sealed_queue_length} ${min_sealed_queue_length}\n"
    result="${result}scheduler wait ${average_wait_time} ${max_wait_time} ${min_wait_time}\n"
    result="${result}scheduler preExecute ${average_preExecute_time} ${max_preExecute_time} ${min_preExecute_time}\n"
    result="${result}scheduler commitBlock ${average_commit_block_time} ${max_commit_block_time} ${min_commit_block_time}\n"
    result="${result}executor hash ${average_hash_time} ${max_hash_time} ${min_hash_time}\n"
    result="${result}storage encode ${average_prepare_encode_time} ${max_prepare_encode_time} ${min_prepare_encode_time}\n"
    result="${result}storage prepare ${average_prepare_time} ${max_prepare_time} ${min_prepare_time}\n"
    result="${result}storage commit ${average_commit_time} ${max_commit_time} ${min_commit_time}\n"
    result="${result}storage setRows ${average_setrows_time} ${max_setrows_time} ${min_setrows_time}\n"
    result="${result}ledger storeTxsAndReceipts ${average_write_txs_receipts_time} ${max_write_txs_receipts_time} ${min_write_txs_receipts_time}\n"
    result="${result}txpool nofityReceipts ${average_nofity_receipt_and_remove_tx_time} ${max_nofity_receipt_and_remove_tx_time} ${min_nofity_receipt_and_remove_tx_time}\n"

    echo -e "${result}" | column -t

    print_header "chain info"
    # tps connection sync

    local chain_info="consensus commited=${latest_block} consensus=${latest_consensus} view=${view} unsealdTxs=${unsealtxs} timeout=${consensus_timeout}\n"
    local average_block_time=$(echo "${time_elapsed_seconds}*1000/(${end_block}-${start_block})"|bc)
    chain_info="${chain_info}executor last=${last_executed_block} version=${latest_block_version} totalTxs=${total_txs} timePerBlock=${average_block_time}ms\n"
    local average_block_time_used=$(echo "${average_execute_block_time}+${average_wait_time}"|bc)
    if [[ $(echo "${average_block_time_used} > 0"|bc) -eq 1 ]]; then
        local tps=$(echo "1000/($average_execute_block_time+$average_wait_time)*$average_txs_in_block" | bc)
        chain_info="${chain_info} tps=${tps}\n"
    else
        chain_info="${chain_info}\n"
    fi
    echo -e "${chain_info}" | column -t
}

get_block_range()
{
    local log_file=$1
    # get the block range from log file
    report_log="$(grep -a Report $log_file | grep -a sealer)"
    log_start_block=$(echo "${report_log}" | head -n 1 | cut -d , -f 4 | cut -d = -f 2)
    log_end_block=$(echo "${report_log}" | tail -n 1 | cut -d , -f 4 | cut -d = -f 2)
}

max()
{
    local a=$1
    local b=$2
    if [ $a -gt $b ]; then
        echo $a
    else
        echo $b
    fi
}

main()
{
    if [ $# -lt 1 ]; then
        echo "Usage: $0 <log_file> [start_block] [end_block]"
        exit 1
    fi
    local log_file=$1
    if [ ! -f ${log_file} ]; then
        LOG_FATAL "file ${log_file} not exist"
    fi
    if [ $# -ge 2 ]; then
        start_block=$2
    fi
    if [ $# -ge 3 ]; then
        end_block=$3
    fi
    get_block_range ${log_file}
    if [[ $start_block -gt $log_end_block ]]; then
        start_block=$log_start_block
    else
        start_block=$(max $start_block $log_start_block)
    fi
    if [ $end_block -le 0 ];then
        end_block=$log_end_block
    elif [[ $end_block -gt $log_end_block || $end_block -lt $log_start_block ]];
    then
        end_block=$log_end_block
    fi
    echo -e "request block range is [${start_block}, ${end_block}], valid is \033[32m[${log_start_block}, ${log_end_block}]\033[0m, used is \033[32m[${start_block}, ${end_block}]\033[0m"

    prepare_tmp_log ${log_file}
    # do the analysis
    # count_consensus
    log="${analysis_log_prefix}.log"
    count_index ${log}
    clean_tmp_log
    # cat "${analysis_log_prefix}.log" |grep -iaE "tx_pool_in" |grep -v "lastQPS(request/s)=0" |awk -F '=' '{print $7}' |awk '{sum+=$1} END {print "inTps   \t", sum/NR}'
    # cat "${analysis_log_prefix}.log" |grep -iaE "tx_pool_seal" |grep -v "lastQPS(request/s)=0" |awk -F '=' '{print $7}' |awk '{sum+=$1} END {print "sealTps \t", sum/NR}'
    # cat "${analysis_log_prefix}.log" |grep -iaE "tx_pool_rm" |grep -v "lastQPS(request/s)=0" |awk -F '=' '{print $7}' |awk '{sum+=$1} END {print "rmTps   \t", sum/NR}'
}

main "$@"
