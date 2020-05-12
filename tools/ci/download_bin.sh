#!/bin/bash

org=FISCO-BCOS
repo=FISCO-BCOS
branch=master
output_dir=bin/
download_gm=
download_mini=

help() {
    echo $1
    cat << EOF
Usage:
    -b <Branch>                   Default master
    -o <Output Dir>               Default ./
    -h Help
e.g
    $0 -b master
EOF

exit 0
}

parse_params()
{
while getopts "b:o:hm" option;do
    case $option in
    o) output_dir=$OPTARG;;
    b) branch="$OPTARG";;
    m) download_mini="true";;
    h) help;;
    *) help;;
    esac
done
}

download_artifact()
{
    [ -f ${output_dir}/fisco-bcos ] && rm -rf ${output_dir}/fisco-bcos
    mkdir -p ${output_dir}
    build_num=$(curl https://circleci.com/api/v1.1/project/github/${org}/${repo}/tree/${branch}\?circle-token\=\&limit\=1\&offset\=0\&filter\=successful 2>/dev/null| grep build_num | sed "s/ //g"| cut -d ":" -f 2| sed "s/,//g" | sort -u | tail -n1)

    local response="$(curl https://circleci.com/api/v1.1/project/github/${org}/${repo}/${build_num}/artifacts?circle-token= 2>/dev/null)"
    if [ -z "${download_mini}" ];then
        link=$(echo ${response}| grep -o 'https://[^"]*' | grep -v 'mini')
    else
        link=$(echo ${response}| grep -o 'https://[^"]*' | grep 'mini')
    fi
    if [ -z "${link}" ];then
        echo "CircleCI build_num:${build_num} can't find artifacts."
        exit 1
    fi

    num=$(( build_num - 1 ))
    if [ -z "${download_mini}" ];then
        link=$(curl https://circleci.com/api/v1.1/project/github/${org}/${repo}/${num}/artifacts?circle-token= 2>/dev/null| grep -o 'https://[^"]*' | grep -v 'mini'| tail -n 1)
    else
        link=$(curl https://circleci.com/api/v1.1/project/github/${org}/${repo}/${num}/artifacts?circle-token= 2>/dev/null| grep -o 'https://[^"]*' | grep 'mini'| tail -n 1)
    fi

    echo -e "\033[32mDownloading binary from ${link} \033[0m"
    cd ${output_dir} && curl -LO ${link} && tar -zxf fisco*.tar.gz && rm fisco*.tar.gz
    result=$?
    if [[ "${result}" != "0" ]];then echo  -e "\033[31mDownload failed, please try again\033[0m" && exit 1;fi
    echo -e "\033[32mFinished. Please check ${output_dir}\033[0m"

}

parse_params $@
download_artifact

