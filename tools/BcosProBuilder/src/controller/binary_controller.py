#!/usr/bin/python
# -*- coding: UTF-8 -*-

from common import utilities
import requests
import sys
import os

class BinaryController:
    def __init__(self, version, binary_path, use_cdn):
        self.version = version
        self.binary_path = binary_path
        self.binary_postfix = "-linux-x86_64.tgz"
        self.binary_list = ["BcosRpcService", "BcosGatewayService", "BcosNodeService"]
        self.use_cdn = use_cdn
        self.last_percent = 0
        self.download_prefix = "https://github.com/FISCO-BCOS/FISCO-BCOS/releases/download/"
        if self.use_cdn is True:
            self.download_prefix = "https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/" 

    def download_all_binary(self):
        utilities.print_badage("Download binary, use_cdn: %s, version: %s" % (self.use_cdn, self.version))
        for binary in self.binary_list:
            download_url = self.get_binary_download_url(binary)
            if self.download_binary(binary, download_url) is False:
                return False
        return True

    def get_binary_download_url(self, binary_name):
        return ("%s%s/%s%s") % (self.download_prefix, self.version, binary_name, self.binary_postfix)
    
    def get_required_binary_path(self, binary_name):
        return os.path.join(self.binary_path, binary_name + ".tgz")
    
    def get_downloaded_binary_path(self, binary_name):
        return binary_name + self.binary_postfix

    def download_binary(self, binary_name, download_url):
        if os.path.exists(self.binary_path) is False:
            utilities.mkdir(self.binary_path)
        binary_file_path = self.get_required_binary_path(binary_name)
        with open(binary_file_path, 'wb') as file:
            response = requests.get(download_url, stream=True)
            total = response.headers.get('content-length')
            if total is None or int(total) < 100000:
                utilities.log_error("Download binary %s failed, Please check the existence of the binary version %s" % (binary_name, self.version))
                return False
            utilities.log_info("* Download %s from %s\n* size: %fMB, dst_path: %s" % (binary_name, download_url, float(total)/float(1000000), binary_file_path))
            downloaded = 0
            total = int(total)
            for data in response.iter_content(chunk_size=max(int(total/1000), 1024*1024)):
                downloaded += len(data)
                file.write(data)
                done = int(50*downloaded/total)
                utilities.log_info("Download percent: %d%%" % (downloaded/total * 100))
                sys.stdout.write('\r[{}{}]'.format('█' * done, '.' * (50-done)))
                sys.stdout.flush()
        sys.stdout.write('\n')
        utilities.log_info("* Download %s from %s success" % (binary_name, download_url))
        return True