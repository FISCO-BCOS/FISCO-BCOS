#!/usr/bin/python
# -*- coding: UTF-8 -*-

from common import utilities
import requests
import sys
import os
import tarfile


class BinaryController:
    def __init__(self, version, binary_path, use_cdn, node_type):
        self.version = version
        self.mtail_version = "3.0.0-rc49"
        self.binary_path = binary_path
        self.binary_postfix = "-linux-x86_64.tgz"
        self.mtail_binary_name = "mtail_3.0.0-rc49_Linux_x86_64.tar.gz"
        self.cdn_link_header = "https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS"
        if node_type == "pro":
            self.binary_list = ["BcosRpcService",
                                "BcosGatewayService", "BcosNodeService"]
        elif node_type == "max":
            self.binary_list = ["BcosRpcService", "BcosGatewayService",
                                "BcosMaxNodeService", "BcosExecutorService"]
        else:
            utilities.log_error("Unsupported node_type %s" % node_type)
            sys.exit(-1)
        self.use_cdn = use_cdn
        self.last_percent = 0
        self.download_prefix = "https://github.com/FISCO-BCOS/FISCO-BCOS/releases/download/"
        self.mtail_download_url = "https://github.com/google/mtail/releases/download/v3.0.0-rc49/%s" % self.mtail_binary_name
        if self.use_cdn is True:
            self.download_prefix = "%s/FISCO-BCOS/releases/" % (
                self.cdn_link_header)
            self.mtail_download_url = "%s/FISCO-BCOS/tools/mtail/%s" % (
                self.cdn_link_header, self.mtail_binary_name)

    def download_all_binary(self):
        utilities.print_badage(
            "Download binary, use_cdn: %s, version: %s" % (self.use_cdn, self.version))
        for binary in self.binary_list:
            download_url = self.get_binary_download_url(binary)
            if self.download_binary(binary + ".tgz", download_url) is False:
                return False
        if self.download_binary(self.mtail_binary_name, self.mtail_download_url) is False:
            return False
        binary_file_path = os.path.join(
            self.binary_path, self.mtail_binary_name)
        self.un_tar_gz(binary_file_path)
        return True

    def get_binary_download_url(self, binary_name):
        return ("%s%s/%s%s") % (self.download_prefix, self.version, binary_name, self.binary_postfix)

    def get_required_binary_path(self, binary_name):
        return os.path.join(self.binary_path, binary_name)

    def get_downloaded_binary_path(self, binary_name):
        return binary_name + self.binary_postfix

    def download_binary(self, binary_name, download_url):
        if os.path.exists(self.binary_path) is False:
            utilities.mkdir(self.binary_path)
        binary_file_path = self.get_required_binary_path(binary_name)
        utilities.log_info("Download url: %s" % download_url)
        with open(binary_file_path, 'wb') as file:
            response = requests.get(download_url, stream=True)
            total = response.headers.get('content-length')
            if total is None or int(total) < 100000:
                utilities.log_error("Download binary %s failed, Please check the existence of the binary version %s" % (
                    binary_name, self.version))
                return False
            utilities.log_info("* Download %s from %s\n* size: %fMB, dst_path: %s" % (
                binary_name, download_url, float(total)/float(1000000), binary_file_path))
            downloaded = 0
            total = int(total)
            for data in response.iter_content(chunk_size=max(int(total/1000), 1024*1024)):
                downloaded += len(data)
                file.write(data)
                done = int(50*downloaded/total)
                utilities.log_info("Download percent: %d%%" %
                                   (downloaded/total * 100))
                sys.stdout.write('\r[{}{}]'.format(
                    '█' * done, '.' * (50-done)))
                sys.stdout.flush()
        sys.stdout.write('\n')
        utilities.log_info("* Download %s from %s success" %
                           (binary_name, download_url))
        return True

    def un_tar_gz(self, file_name):
        tar = tarfile.open(file_name)
        tar.extractall(self.binary_path)
        tar.close()
