#!/bin/bash

org=FISCO-BCOS
repo=FISCO-BCOS
branch=release-2.0.1
output_dir=bin/
download_gm=

help() {
    echo $1
    cat << EOF
Usage:
    -b <Branch>                   Default release-2.0.1
    -o <Output Dir>               Default ./
    -g <Generate guomi nodes>     Default no
    -h Help
e.g 
    $0 -b master
EOF

exit 0
}

parse_params()
{
while getopts "b:o:hg" option;do
    case $option in
    o) output_dir=$OPTARG;;
    b) branch="$OPTARG";;
    g) download_gm="yes";;
    h) help;;
    esac
done
}

download_artifact()
{
    [ -f ${output_dir}/fisco-bcos ] && rm -rf ${output_dir}/fisco-bcos
    mkdir -p ${output_dir}
    local build_num=$(curl https://circleci.com/api/v1.1/project/github/${org}/${repo}/tree/${branch}\?circle-token\=\&limit\=1\&offset\=0\&filter\=successful 2>/dev/null| grep build_num | head -2 | tail -1 |sed "s/ //g"| cut -d ":" -f 2| sed "s/,//g")
    # echo "build num : ${build_num}"
    local response="$(curl https://circleci.com/api/v1.1/project/github/${org}/${repo}/${build_num}/artifacts?circle-token= 2>/dev/null)"

    link=$(echo ${response}| grep -o 'https://[^"]*')

    is_gm="$(echo "${response}"| grep -o 'gm')"
    if [ ! -z "${download_gm}" -a -z "${is_gm}" ] || [ -z "${download_gm}" -a ! -z "${is_gm}" ]
    then
        num=$(( build_num - 1 ))
        link=$(curl https://circleci.com/api/v1.1/project/github/${org}/${repo}/${num}/artifacts?circle-token= 2>/dev/null| grep -o 'https://[^"]*' | tail -n 1)
    fi
    echo -e "\033[32mDownloading binary from ${link} \033[0m"
    cd ${output_dir} && curl -LO ${link} && tar -zxf fisco*.tar.gz && rm fisco*.tar.gz
    result=$?
    if [[ "${result}" != "0" ]];then echo  -e "\033[31mDownload failed, please try again\033[0m" && exit 1;fi 
    echo -e "\033[32mFinished. Please check ${output_dir}\033[0m"

}

parse_params $@
download_artifact

