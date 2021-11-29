#!/usr/bin/python
# -*- coding: UTF-8 -*-
from common import utilities


class NetworkManager:
    def create_sub_net(subnet, docker_network_name):
        """
        create the subnet
        """
        utilities.log_info("* create docker subnet %s, name: %s" %
                           (subnet, docker_network_name))
        command = "docker network create -d bridge --subnet=%s %s" % (
            subnet, docker_network_name)
        if utilities.execute_command(command) is False:
            utilities.log_error("create the docker subnet failed")
            return False
        return True

    def get_docker_network_id(docker_network_name):
        """
        get the docker network id
        """
        command = "docker network ls | grep -i \"%s\" | awk -F\' \' \'{print $1}\'" % docker_network_name
        (ret, result) = utilities.execute_command_and_getoutput(command)
        if ret is False:
            utilities.log_error(
                "* get docker network id for %s failed" % docker_network_name)
        utilities.log_info(
            "* get docker network id for %s success, id: %s" % (docker_network_name, result))
        return (ret, result)

    def create_bridge(docker_network_name, docker_vxlan_name, remote_ip):
        """
        add the bridge
        """
        dstport = 4789
        dev_name = "eth0"
        utilities.log_info("* set the bridge interconnection network, docker_network: %s, docker_vxlan_name: %s, remote_ip: %s, dstport: %s" %
                           (docker_network_name, docker_vxlan_name, remote_ip, dstport))
        (ret, network_id) = NetworkManager.get_docker_network_id(docker_network_name)
        basic_error_info = "Failed to set the bridge interconnection network"
        if ret is False:
            utilities.log_error("%s, please check the network name! remote ip: %s, network name: %s" % (
                basic_error_info, remote_ip, docker_network_name))
            return False
        # add ip link
        ip_link_command = "ip link add %s type vxlan id 200 remote %s dstport %d dev %s" % (
            docker_vxlan_name, remote_ip, dstport, dev_name)
        if utilities.execute_command(ip_link_command) is False:
            utilities.log_error("%s" % basic_error_info)
            return False
        # setup the network
        ip_set_up_command = "ip link set %s up" % docker_vxlan_name
        if utilities.execute_command(ip_set_up_command) is False:
            utilities.log_error("%s" % basic_error_info)
            return False
        # add bridge
        bridge_add_command = "brctl addif br-%s %s" % (
            network_id, docker_vxlan_name)
        if utilities.execute_command(bridge_add_command) is False:
            utilities.log_error("%s" % basic_error_info)
            return False
        return True
