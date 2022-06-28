#!/usr/bin/python
# -*- coding: UTF-8 -*-
from common import utilities
import requests
import base64
import json
import time
import os


class KeyCenterService:
    """
    the key center service
    """

    def __init__(self, url, cipher_data_key):
        if url.startswith("http://"):
            self.url = url
        else:
            self.url = "http://%s" % url
        self.cipher_data_key = cipher_data_key

    def encrypt_file(self, file_path):
        utilities.log_info("encrypt %s with key center %s" %
                           (file_path, self.url))
        if os.path.exists(file_path) is False:
            utilities.log_error("The file %s not exists!" % (file_path))
            return False
        method = "encWithCipherKey"
        params = []
        # read the file
        with open(file_path) as cert_obj:
            cert_data = base64.b64encode(
                cert_obj.read().encode(encoding='utf-8'))
            params.append(str(cert_data))
        params.append(self.cipher_data_key)
        payload = {"jsonrpc": "2.0", "method": method,
                   "params": params, "id": 83}
        headers = {'content-type': "application/json"}
        try:
            response = requests.request(
                "POST", self.url, data=json.dumps(payload), headers=headers)
            response_data = response.json()
            if "result" not in response_data or "dataKey" not in response_data["result"]:
                utilities.log_error("encrypt %s with key center %s failed for error response: %s" % (
                    file_path, self.url, response_data))
                return False
            # backup the file_path
            backup_file_path = "%s_%d" % (file_path, time.time())
            utilities.log_info("backup the original file %s to %s" %
                               (file_path, backup_file_path))
            (ret, message) = utilities.execute_command_and_getoutput(
                "cp %s %s" % (file_path, backup_file_path))
            if ret is False:
                utilities.log_error("encrypt %s with key center %s failed for backup file failed, error: %s" % (
                    file_path, self.url, message))
                return False
            # write the encrypted content into file_path
            cipher_file_data = response_data["result"]["dataKey"]
            with open(file_path, "w") as f:
                f.write(cipher_file_data)
            utilities.log_info(
                "encrypt %s with key center %s success" % (file_path, self.url))
            return True

        except Exception as e:
            utilities.log_error("encrypt %s with key center %s failed, error info %s" % (
                file_path, self.url, e))
            return False
