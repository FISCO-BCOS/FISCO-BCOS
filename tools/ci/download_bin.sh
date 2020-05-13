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

download_artifact_linux()
{
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

    echo -e "\033[32mDownloading binary from ${link} \033[0m"
    cd ${output_dir} && curl -LO ${link} && tar -zxf fisco*.tar.gz && rm fisco*.tar.gz
    result=$?
    if [[ "${result}" != "0" ]];then echo  -e "\033[31mDownload failed, please try again\033[0m" && exit 1;fi
    echo -e "\033[32mFinished. Please check ${output_dir}\033[0m"

}

download_artifact_macOS(){
    echo "unsupported for now."
    exit 1
    # TODO: https://developer.github.com/v3/actions/artifacts/#download-an-artifact
    local workflow_artifacts_url="$(curl https://api.github.com/repos/FISCO-BCOS/FISCO-BCOS/actions/runs\?branch\=${branch}\&status\=success\&event\=push | grep artifacts_url | head -n 1 | cut -d \" -f 4)"
    local archive_download_url="$(curl ${workflow_artifacts_url} | grep archive_download_url | cut -d \" -f 4)"
    if [ -z "${archive_download_url}" ];then
        echo "GitHub atone ${workflow_artifacts_url} can't find artifact."
        exit 1
    fi
    echo -e "\033[32mDownloading macOS binary from ${archive_download_url} \033[0m"
    # FIXME: https://github.com/actions/upload-artifact/issues/51
    cd "${output_dir}" && curl -LO "${archive_download_url}" && tar -zxf fisco-bcos-macOS.tar.gz && rm fisco-bcos-macOS.tar.gz
}

main(){
    [ -f "${output_dir}/fisco-bcos" ] && rm -f "${output_dir}/fisco-bcos"
    mkdir -p "${output_dir}"
    if [ "$(uname)" == "Darwin" ];then
        echo -e "\033[32mDownloading binary of macOS \033[0m"
        download_artifact_macOS
    else
        echo -e "\033[32mDownloading binary of Linux \033[0m"
        download_artifact_linux
    fi
}

parse_params "$@"
main
