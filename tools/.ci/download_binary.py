#!/usr/bin/python
# -*- coding: UTF-8 -*-
import requests
import platform
import argparse

air_linux_binaries_list = ["fisco-bcos-linux.zip"]
air_macos_binaries_list = ["fisco-bcos-MacOS.zip"]
pro_linux_binaries_list = ["BcosRpcService.zip",
                           "BcosGatewayService.zip", "BcosNodeService.zip"]
pro_macos_binaries_list = ["BcosRpcService-MacOS.zip",
                           "BcosGatewayService-MacOS.zip", "BcosNodeService-MacOS.zip"]
artifacts_url = "https://api.github.com/repos/FISCO-BCOS/FISCO-BCOS/actions/artifacts"
# Note: set the token
token = ""


def is_macos():
    return platform.uname().system == "Darwin"


def download_binaries(token, binary_list):
    response = requests.get(artifacts_url, headers={
                            "Accept": "application/vnd.github.v3+json"})
    if response.status_code != 200:
        print("Download binary error, error message: %s" % response.content)
    result = response.json()
    if 'artifacts' not in result or len(result['artifacts']) == 0:
        print("Download binary error for empty artifacts, response: %s" %
              response.content)
    for binary in binary_list:
        for artifact in result['artifacts']:
            if artifact['name'] == binary:
                url = artifact['archive_download_url']
                print("Info: Download binary: %s, url: %s" % (binary, url))
                response = requests.get(url, headers={
                                        "Accept": "application/vnd.github.v3+json", "Authorization": "token %s" % token})
                f = open(binary, "wb")
                f.write(response.content)
                f.close()
                print("Info: Download binary success: %s, url: %s" %
                      (binary, url))
                break


def parse_command():
    parser = argparse.ArgumentParser(description='download_binary')
    parser.add_argument(
        '--command', help="[required]the command, support download_air and download_pro", required=True)
    args = parser.parse_args()
    return args


def main():
    if len(token) == 0:
        print("ERROR: must set token to access the github")
        return
    macOS = is_macos()
    args = parse_command()
    air_binaries_list = air_linux_binaries_list
    pro_binaries_list = pro_linux_binaries_list
    if macOS is True:
        air_binaries_list = air_macos_binaries_list
        pro_binaries_list = pro_macos_binaries_list

    if args.command == "download_air":
        download_binaries(token, air_binaries_list)
        return
    if args.command == "download_pro":
        download_binaries(token, pro_binaries_list)
        return
    print("ERROR: unsupported command: %s" % args.command)


if __name__ == "__main__":
    main()
