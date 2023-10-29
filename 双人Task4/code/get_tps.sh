#!/bin/bash

# 用法
function Usage()
{
    echo "Usage:"
    # bash get_tps.sh ../log/log_2023102813.00.log 10:00:00 14:30:00
    echo "bash $0 log_file_path start_time end_time"
    exit 1
}

# 日志文件，如log_2021042606.00.log
log_file="${1}"

# $#表示所有参数的个数
if [ $# -eq 1 ];then
    if [ "${log_file}"="-h" ]||[ "${log_file}"="--help" ];then
       Usage
    fi
fi
if [ $# -lt 3 ];then
    Usage
fi

# 统计的开始时间，06:20:00
start_time="${2}"
# 统计的结束时间，06:28:00
end_time="${3}"

# grep Report，将有Report字符串的行筛选出来
#txList=$(cat "${log_file}" | grep Report | awk -F',' '{print $5}' | cut -d'=' -f2 )


# sort表示从小到大排序
# uniq表示删除文本文件中重复出现的行列
# 找出所有的时间
timeList=$(grep Report "${log_file}" | grep PBFT | awk -F' ' '{print $2}' | cut -d ' ' -f2 | cut -d'|' -f1 | sort | uniq)

# 交易总数
txTotal=0
statistic_end=${end_time}
statistic_start=${start_time}

# 标志是否为第一个
started="0"

for time in ${timeList}
do
   # \>表示大于
   if [ "${time}" \> "${start_time}" ]&&[ "${time}" \< "$end_time" ];then
	   #echo "====time:${time}"
      if [ "${started}" == "0" ];then
           started="1"
           # 真正开始的时间
           statistic_start="${time}"
      else
        # $()是将括号内命令的执行结果赋值给变量
        # grep match_pattern file_name
        # 每个时间可能对应多个交易
        # info|2021-06-11 14:27:08.818735|[g:1][CONSENSUS][PBFT]^^^^^^^^Report,num=8319,sealerIdx=2,hash=da376d36...,next=8320,tx=3,nodeIdx=2
   	  txList=$(grep Report "${log_file}" | grep PBFT| grep ${time} | awk -F',' '{print $6}' | cut -d'=' -f2 )
        # tx代表该时间段的交易数
   	  for tx in ${txList}
   	  do
  	       txTotal=$((txTotal+tx))
   	  done
        # 以最后一个时间段为真正结束的时间
        statistic_end=${time}
      fi
   fi
done


echo "statistic_end = ${statistic_end}"
echo "statistic_start = ${statistic_start}"

# awk -F':' '{print $1}',以":"分割字符串，打印第一个
start_hour=$(echo ${statistic_start} | awk -F':' '{print $1}')
start_min=$(echo ${statistic_start} | awk -F':' '{print $2}')
# cut -d ':' -f1，以':'分割，取第一个
start_s=$(echo ${statistic_start} | awk -F':' '{print $3}'| cut -d'.' -f1)
start_ms=$(echo ${statistic_start} | awk -F':' '{print $3}'| cut -d'.' -f2)
# 精确到毫秒，以毫秒表示开始时间
start_t=$(echo "$start_hour*3600000+ $start_min*60000 + $start_s*1000 + $start_ms/1000" | bc)

end_hour=$(echo ${statistic_end} | awk -F':' '{print $1}')
end_min=$(echo ${statistic_end} | awk -F':' '{print $2}')
end_s=$(echo ${statistic_end} | awk -F':' '{print $3}'| cut -d'.' -f1)
end_ms=$(echo ${statistic_end} | awk -F':' '{print $3}'| cut -d'.' -f2)
end_t=$(echo "$end_hour*3600000+ $end_min*60000 + $end_s*1000 + $end_ms/1000" | bc)


# 时间间隔
delta_time=$((end_t-start_t))

# bc表示将管道|前面的内容当作计算器的输入
tps=$(echo "${txTotal}*1000/$delta_time" | bc)


# scale=2 设小数位，2 代表保留两位:
#$(echo "scale=9; $interest_r/12 + 1.0" | bc)

echo "total transactions = ${txTotal}, execute_time = ${delta_time}ms, tps = ${tps} (tx/s)"