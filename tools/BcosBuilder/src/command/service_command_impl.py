#!/usr/bin/python
# -*- coding: UTF-8 -*-
from controller.service_controller import ServiceController
from common import utilities


class ServiceCommandImpl:
    def __init__(self, config, service_type, node_type):
        self.config = config
        self.service_type = service_type
        self.service_controller = ServiceController(
            config, service_type, node_type)

    def gen_service_config(self):
        utilities.print_split_info()
        utilities.print_badage("generate service config")
        ret = self.service_controller.gen_all_service_config()
        if ret is True:
            utilities.print_badage("generate service config success")
        else:
            utilities.log_error("generate service config failed")
        utilities.print_split_info()
        return ret

    def upload_service(self):
        # upload the generated_config
        utilities.print_split_info()
        utilities.print_badage("upload service, type: %s" % self.service_type)
        ret = self.service_controller.deploy_all()
        if ret is True:
            utilities.print_badage(
                "upload service success, type: %s" % self.service_type)
        else:
            utilities.log_error(
                "upload service failed, type: %s" % self.service_type)
        utilities.print_split_info()
        return ret

    def deploy_service(self):
        # generate_config
        utilities.print_split_info()
        utilities.print_badage("deploy service, type: %s" % self.service_type)
        ret = self.gen_service_config()
        if ret is False:
            utilities.log_error(
                "deploy service failed for generate config failed, type: %s" % self.service_type)
            return False
        ret = self.service_controller.deploy_all()
        if ret is True:
            utilities.print_badage(
                "deploy service success, type: %s" % self.service_type)
        else:
            utilities.log_error(
                "deploy service failed, type: %s" % self.service_type)
        utilities.print_split_info()
        return ret

    def delete_service(self):
        utilities.print_split_info()
        utilities.print_badage("delete service, type: %s" % self.service_type)
        ret = self.service_controller.undeploy_all()
        if ret is True:
            utilities.print_badage(
                "delete service success, type: %s" % self.service_type)
        else:
            utilities.log_error(
                "delete service failed, type: %s" % self.service_type)
        utilities.print_split_info()
        return ret

    def upgrade_service(self):
        utilities.print_split_info()
        utilities.print_badage("upgrade service, type: %s" % self.service_type)
        ret = self.service_controller.upgrade_all()
        if ret is True:
            utilities.print_badage(
                "upgrade service success, type: %s" % self.service_type)
        else:
            utilities.log_error(
                "upgrade service failed, type: %s" % self.service_type)
        utilities.print_split_info()
        return ret

    def start_service(self):
        utilities.print_split_info()
        utilities.print_badage("start service, type: %s" % self.service_type)
        ret = self.service_controller.start_all()
        if ret is True:
            utilities.print_badage(
                "start service success, type: %s" % self.service_type)
        else:
            utilities.log_error(
                "start service failed, type: %s" % self.service_type)
        utilities.print_split_info()
        return ret

    def stop_service(self):
        utilities.print_split_info()
        utilities.print_badage("stop service, type: %s" % self.service_type)
        ret = self.service_controller.stop_all()
        if ret is True:
            utilities.print_badage(
                "stop service success, type: %s" % self.service_type)
        else:
            utilities.log_error(
                "stop service failed, type: %s" % self.service_type)
        utilities.print_split_info()
        return ret

    def expand_service(self):
        utilities.print_split_info()
        utilities.print_badage("expand service, type: %s" % self.service_type)
        # generate config
        ret = self.gen_service_config()
        if ret is False:
            utilities.log_error(
                "expand service failed for generate config failed, type: %s" % self.service_type)
            return False
        # expand service
        utilities.print_badage("begin expand service")
        ret = self.service_controller.expand_all()
        if ret is True:
            utilities.print_badage(
                "expand service success, type: %s" % self.service_type)
        else:
            utilities.log_error(
                "expand service failed, type: %s" % self.service_type)
        utilities.print_split_info()
        return ret
