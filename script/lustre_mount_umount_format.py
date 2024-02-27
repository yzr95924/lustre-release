#!/usr/bin/python3
# -*- coding: utf-8 -*-
"""
mount/umount/format Lustre in a single machine
"""

import os
import getopt
import sys
import errno
import socket

sys.path.append("../common_include")
_g_proc_name = "single_mount"

from my_py import logger
from my_py import setup
from my_py import os_util
from my_py import cmd_handler

_g_is_dry_run = True
_g_is_debug = False
_g_logger = logger.get_logger(name=_g_proc_name)
_g_cmd_handler = cmd_handler.CmdHandler(handler_name=_g_proc_name)
_g_is_umount = False
_g_is_format = False
_g_ost_mount_point_prefix = "/mnt/ost"
_g_mdt_mount_point_prefix = "/mnt/mdt"
_g_mgt_mount_point = "/mnt/mgt"
_g_client_mount_point = "/mnt/l_lfs"
_g_lustre_fs_name = "l_lfs"
_g_mkfs_cmd = "mkfs.lustre"

_g_ost_dev_list= [
    "/dev/nvme0n5",
    "/dev/nvme0n6",
]
_g_mdt_dev_list = [
    "/dev/nvme0n3",
    "/dev/nvme0n4",
]
_g_mgt_dev_list = [
    "/dev/nvme0n2",
]

def usage():
    print("Usage: python3 {} -r -d -u".format(_g_proc_name))
    print("-r (optional): dry run")
    print("-d (optional): debug mode")
    print("-u (optional): umount mode")
    print("-f (optional): reformat mode")


def format_ost_mdt(tgt_type: str, index: int, dev_path: str, mount_opt=""):
    """format ost/mdt dev and mkdir mount point

    Args:
        tgt_type (str): mdt/ost
        index (int): index number
        dev_path (str): dev path
        mount_opt (str, optional): mount options. Defaults to "".

    Returns:
        ret: ret code
    """
    global _g_logger, _g_cmd_handler
    if (tgt_type not in ["ost", "mdt"]):
        _g_logger.error("format tgt_type invalid: {}", tgt_type)
        return errno.EINVAL

    mgs_nid = os_util.Network.get_ip_address() + "@tcp"
    cmd = _g_mkfs_cmd + " " + "--fsname=" + _g_lustre_fs_name + " " \
        + "--" + tgt_type + " " \
        + "--servicenode=" + mgs_nid + " " \
        + "--mgsnode=" + mgs_nid + " " \
        + "--reformat" + " " \
        + "--index=" + str(index) + " " \
        + "--mkfsoptions=" + "\"" + mount_opt + "\"" + " " \
        + dev_path
    _, _, ret = _g_cmd_handler.run_shell(cmd=cmd,
                                         is_dry_run=_g_is_dry_run,
                                         is_debug=_g_is_debug,
                                         is_verbose=True)
    if (ret != 0):
        _g_logger.error("format {}: {}, {} failed: {}".format(
            tgt_type, index, dev_path,
            os_util.translate_linux_err_code(ret)))
    else:
        _g_logger.info("format {}: {}, {} successful".format(
            tgt_type, index, dev_path))
    return ret

def format_mgt(dev_path: str, mount_opt=""):
    """format mgt dev and mkdir mount point

    Args:
        dev_path (str): dev path
        mount_opt (str, optional): mount options. Defaults to "".

    Returns:
        ret: ret code
    """
    global _g_logger, _g_cmd_handler
    mgs_nid = os_util.Network.get_ip_address() + "@tcp"

    cmd = _g_mkfs_cmd + " " + "--fsname=" + _g_lustre_fs_name + " " \
        + "--mgs" + " " \
        + "--servicenode=" + mgs_nid + " " \
        + "--reformat" + " " \
        + "--mkfsoptions=" + "\"" + mount_opt + "\"" + " " \
        + dev_path

    _, _, ret = _g_cmd_handler.run_shell(cmd=cmd,
                                         is_dry_run=_g_is_dry_run,
                                         is_debug=_g_is_debug,
                                         is_verbose=True)

    if (ret != 0):
        _g_logger.error("format mgt: {}, failed: {}".format(
            dev_path, os_util.translate_linux_err_code(ret)))
    else:
        _g_logger.info("format mgt: {} successful".format(dev_path))
    return ret

def mount_umount_format_ost(is_umount: bool):
    pass

def mount_umount_format_mdt(is_umount: bool):
    pass

def mount_umount_format_mgt(is_mount: bool):
    pass


if __name__ == "__main__":
    short_options = "rdhuf"
    ret = 0

    try:
        opts, args = getopt.getopt(sys.argv[1:], shortopts=short_options)
    except getopt.GetoptError as err:
        _g_logger.error(str(err))
        sys.exit(errno.EINVAL)

    for opt, arg in opts:
        if (opt == "-h"):
            usage()
            sys.exit(0)
        elif (opt == "-r"):
            _g_is_dry_run = True
        elif (opt == "-d"):
            _g_is_debug = True
        elif (opt == "u"):
            _g_is_umount = True
        elif (opt == "f"):
            _g_is_format = True
        else:
            _g_logger.error("wrong opt")
            usage()
            sys.exit(errno.EINVAL)
    setup.init_dry_run_debug_flag(is_dry_run=_g_is_dry_run,
                                  is_debug=_g_is_debug)

    if (_g_is_format):
        #TODO: check whether lustre is running?
        _g_logger.info("start to format Lustre")
        pass

    if (_g_is_umount):
        _g_logger.info("start to umount Lustre")
        pass
    else:
        _g_logger.info("start to mount Lustre")
        pass

    print("ip: {}".format(os_util.Network.get_ip_address()))