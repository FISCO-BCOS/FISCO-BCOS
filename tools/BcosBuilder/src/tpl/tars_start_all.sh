#!/bin/bash
dirpath="$(cd "$(dirname "$0")" && pwd)"
cd "${dirpath}"

dirs=($(ls -l ${dirpath} | awk '/^d/ {print $NF}'))
for dir in ${dirs[*]}
do
    if [[ -f "${dirpath}/${dir}/conf/config.ini" && -f "${dirpath}/${dir}/start.sh" ]];then
        echo "try to start ${dir}"
        bash ${dirpath}/${dir}/start.sh &
    fi
done
wait

